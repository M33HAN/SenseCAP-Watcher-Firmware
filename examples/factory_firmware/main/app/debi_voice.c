/**
 * @file debi_voice.c
 * @brief Debi Guardian — Voice & audio alert module
 *
 * Plays audio cues in response to face state transitions driven
 * by the debi_face_bridge.  Uses the existing app_audio_player
 * infrastructure and SPIFFS-stored WAV/MP3 files.
 *
 * Key behaviours:
 *   - One-shot chimes for transient states (presence, happy)
 *   - Repeating alarms for urgent states (concerned, alert_*)
 *   - Cooldown to avoid spamming the same chime
 *   - Mute / volume controllable via API (wired to MQTT in debi_os)
 *   - Night mode suppresses non-critical audio
 *
 * Copyright (c) 2026 Debi Guardian
 */

#include "debi_voice.h"
#include "app_audio_player.h"
#include "ui_face_states.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"

#include <string.h>

static const char *TAG = "debi_voice";

/* ------------------------------------------------------------------ */
/*  Internal state                                                     */
/* ------------------------------------------------------------------ */

typedef struct {
    bool          initialised;
    bool          muted;
    int           volume;          /* 0-100 */
    face_state_t  current_state;   /* last notified state */
    int64_t       last_play_us;    /* esp_timer_get_time() of last play */
    bool          alarm_active;    /* repeating alarm running? */
    esp_timer_handle_t alarm_timer;
    SemaphoreHandle_t  lock;
} debi_voice_ctx_t;

static debi_voice_ctx_t s_voice = {
    .initialised  = false,
    .muted        = false,
    .volume       = DEBI_VOICE_DEFAULT_VOLUME,
    .current_state = FACE_STATE_IDLE,
    .last_play_us = 0,
    .alarm_active = false,
    .alarm_timer  = NULL,
    .lock         = NULL,
};

/* ------------------------------------------------------------------ */
/*  Forward declarations                                               */
/* ------------------------------------------------------------------ */

static void alarm_timer_cb(void *arg);
static void start_alarm_repeat(uint32_t interval_ms);
static void stop_alarm_repeat(void);
static void play_audio(const char *path);
static bool cooldown_ok(void);
static bool is_alert_state(face_state_t s);
static bool is_night_quiet(face_state_t next);

/* ------------------------------------------------------------------ */
/*  Public API                                                         */
/* ------------------------------------------------------------------ */

void debi_voice_init(void)
{
    if (s_voice.initialised) {
        ESP_LOGW(TAG, "already initialised");
        return;
    }

    s_voice.lock = xSemaphoreCreateMutex();
    if (!s_voice.lock) {
        ESP_LOGE(TAG, "failed to create mutex");
        return;
    }

    /* Create a one-shot timer that we restart for repeating alarms */
    const esp_timer_create_args_t timer_args = {
        .callback = alarm_timer_cb,
        .arg      = NULL,
        .name     = "debi_voice_alarm",
    };
    esp_err_t err = esp_timer_create(&timer_args, &s_voice.alarm_timer);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "timer create failed: %s", esp_err_to_name(err));
        vSemaphoreDelete(s_voice.lock);
        s_voice.lock = NULL;
        return;
    }

    s_voice.initialised = true;
    ESP_LOGI(TAG, "voice module ready  vol=%d  mute=%d",
             s_voice.volume, s_voice.muted);
}

void debi_voice_deinit(void)
{
    if (!s_voice.initialised) return;

    stop_alarm_repeat();

    if (s_voice.alarm_timer) {
        esp_timer_delete(s_voice.alarm_timer);
        s_voice.alarm_timer = NULL;
    }
    if (s_voice.lock) {
        vSemaphoreDelete(s_voice.lock);
        s_voice.lock = NULL;
    }

    s_voice.initialised = false;
    ESP_LOGI(TAG, "voice module stopped");
}

/**
 * Main entry point — called by debi_face_bridge on every
 * face state transition.
 */
void debi_voice_on_face_change(face_state_t prev, face_state_t next)
{
    if (!s_voice.initialised) return;

    xSemaphoreTake(s_voice.lock, portMAX_DELAY);

    s_voice.current_state = next;

    /* If leaving an alarm state, stop the repeater */
    if (s_voice.alarm_active && !is_alert_state(next) &&
        next != FACE_STATE_CONCERNED) {
        stop_alarm_repeat();
    }

    /* In night mode, suppress non-critical audio */
    if (is_night_quiet(next)) {
        xSemaphoreGive(s_voice.lock);
        return;
    }

    /* Decide what to play based on new state */
    switch (next) {

    case FACE_STATE_PRESENCE:
        if (cooldown_ok()) {
            play_audio(DEBI_AUDIO_CHIME_PRESENCE);
        }
        break;

    case FACE_STATE_HAPPY:
        if (cooldown_ok()) {
            play_audio(DEBI_AUDIO_CHIME_HAPPY);
        }
        break;

    case FACE_STATE_CONCERNED:
        /* Start repeating alarm at slow rate */
        play_audio(DEBI_AUDIO_ALARM);
        start_alarm_repeat(DEBI_VOICE_ALARM_REPEAT_MS);
        break;

    case FACE_STATE_ALERT_FALL:
    case FACE_STATE_ALERT_STILL:
    case FACE_STATE_ALERT_BABY:
    case FACE_STATE_ALERT_HEART:
        /* Urgent — fast repeating alarm */
        play_audio(DEBI_AUDIO_ALARM);
        start_alarm_repeat(DEBI_VOICE_ALERT_REPEAT_MS);
        break;

    case FACE_STATE_NIGHT:
        /* Entering night mode — soft chime */
        play_audio(DEBI_AUDIO_CHIME_NIGHT);
        break;

    case FACE_STATE_BOOT:
        play_audio(DEBI_AUDIO_BOOT);
        break;

    case FACE_STATE_ERROR:
        play_audio(DEBI_AUDIO_ERROR);
        break;

    case FACE_STATE_IDLE:
    case FACE_STATE_LISTENING:
    case FACE_STATE_TALKING:
    case FACE_STATE_SETUP:
        /* No audio for these transitions */
        break;

    default:
        break;
    }

    xSemaphoreGive(s_voice.lock);
}

void debi_voice_set_volume(int volume)
{
    if (volume < 0)   volume = 0;
    if (volume > 100) volume = 100;
    s_voice.volume = volume;
    ESP_LOGI(TAG, "volume set to %d", volume);
}

int debi_voice_get_volume(void)
{
    return s_voice.volume;
}

void debi_voice_set_mute(bool mute)
{
    s_voice.muted = mute;
    ESP_LOGI(TAG, "mute %s", mute ? "ON" : "OFF");

    if (mute) {
        /* Stop any active alarm and current playback */
        stop_alarm_repeat();
        app_audio_player_stop();
    }
}

bool debi_voice_is_muted(void)
{
    return s_voice.muted;
}

void debi_voice_stop(void)
{
    stop_alarm_repeat();
    app_audio_player_stop();
    ESP_LOGI(TAG, "playback stopped");
}

void debi_voice_play_file(const char *filepath)
{
    if (!filepath) return;
    play_audio(filepath);
}

/* ------------------------------------------------------------------ */
/*  Internal helpers                                                   */
/* ------------------------------------------------------------------ */

/**
 * Play an audio file if not muted and the player is idle.
 */
static void play_audio(const char *path)
{
    if (s_voice.muted) {
        ESP_LOGD(TAG, "muted — skipping %s", path);
        return;
    }

    if (s_voice.volume == 0) {
        ESP_LOGD(TAG, "volume 0 — skipping %s", path);
        return;
    }

    if (app_audio_player_status_get() != AUDIO_PLAYER_STATUS_IDLE) {
        ESP_LOGD(TAG, "player busy — skipping %s", path);
        return;
    }

    ESP_LOGI(TAG, "playing: %s", path);
    s_voice.last_play_us = esp_timer_get_time();
    app_audio_player_file((void *)path);
}

/**
 * Enforce cooldown between same-category chimes to
 * prevent rapid-fire repeats on detection flicker.
 */
static bool cooldown_ok(void)
{
    int64_t now = esp_timer_get_time();
    int64_t elapsed_us = now - s_voice.last_play_us;
    return (elapsed_us >= (int64_t)DEBI_VOICE_COOLDOWN_MS * 1000);
}

/**
 * Timer callback for repeating alarm tones.
 */
static void alarm_timer_cb(void *arg)
{
    (void)arg;

    if (!s_voice.alarm_active) return;
    if (s_voice.muted) return;

    /* Re-trigger the alarm sound */
    if (app_audio_player_status_get() == AUDIO_PLAYER_STATUS_IDLE) {
        ESP_LOGI(TAG, "alarm repeat: %s", DEBI_AUDIO_ALARM);
        s_voice.last_play_us = esp_timer_get_time();
        app_audio_player_file((void *)DEBI_AUDIO_ALARM);
    }
}

/**
 * Start the repeating alarm timer.
 */
static void start_alarm_repeat(uint32_t interval_ms)
{
    if (s_voice.alarm_active) {
        /* Already running — restart with new interval */
        esp_timer_stop(s_voice.alarm_timer);
    }

    s_voice.alarm_active = true;
    esp_err_t err = esp_timer_start_periodic(s_voice.alarm_timer,
                                              (uint64_t)interval_ms * 1000);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "alarm timer start failed: %s", esp_err_to_name(err));
        s_voice.alarm_active = false;
    } else {
        ESP_LOGI(TAG, "alarm repeating every %lu ms", (unsigned long)interval_ms);
    }
}

/**
 * Stop the repeating alarm timer.
 */
static void stop_alarm_repeat(void)
{
    if (!s_voice.alarm_active) return;

    esp_timer_stop(s_voice.alarm_timer);
    s_voice.alarm_active = false;
    ESP_LOGI(TAG, "alarm repeat stopped");
}

/**
 * Check if a face state is one of the urgent alert states.
 */
static bool is_alert_state(face_state_t s)
{
    return (s == FACE_STATE_ALERT_FALL  ||
            s == FACE_STATE_ALERT_STILL ||
            s == FACE_STATE_ALERT_BABY  ||
            s == FACE_STATE_ALERT_HEART);
}

/**
 * In night mode, suppress audio for non-critical transitions.
 * Only allow alerts and errors through.
 */
static bool is_night_quiet(face_state_t next)
{
    if (s_voice.current_state != FACE_STATE_NIGHT &&
        next != FACE_STATE_NIGHT) {
        return false;  /* not in night mode */
    }

    /* Allow alerts and errors even in night mode */
    if (is_alert_state(next) || next == FACE_STATE_CONCERNED ||
        next == FACE_STATE_ERROR) {
        return false;
    }

    /* Everything else is suppressed in night mode */
    if (s_voice.current_state == FACE_STATE_NIGHT && next != FACE_STATE_NIGHT) {
        ESP_LOGD(TAG, "night mode — suppressing audio for %d", next);
        return true;
    }

    return false;
}
