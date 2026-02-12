/**
 * @file debi_voice.h
 * @brief Debi Guardian — Voice & audio alert module
 *
 * Plays audio cues when the face/monitoring state changes:
 *   - Person detected  → short chime (echo_en_ok.wav)
 *   - Pet detected     → double-beep (Hi.mp3)
 *   - Idle timeout     → silence (no sound)
 *   - Concerned        → alarm tone (alarm-di.wav, repeating)
 *   - Alert states     → urgent alarm (alarm-di.wav, fast repeat)
 *   - Night mode enter → soft chime (echo_en_end.wav)
 *   - Error            → error tone (networkError.mp3)
 *
 * Volume and mute are controllable via MQTT from the hub.
 *
 * Copyright (c) 2026 Debi Guardian
 */

#ifndef DEBI_VOICE_H
#define DEBI_VOICE_H

#include <stdint.h>
#include <stdbool.h>
#include "ui_face_states.h"

#ifdef __cplusplus
extern "C" {
#endif

/* —— Audio file paths (SPIFFS) —— */
#define DEBI_AUDIO_CHIME_PRESENCE   "/spiffs/echo_en_ok.wav"
#define DEBI_AUDIO_CHIME_HAPPY      "/spiffs/Hi.mp3"
#define DEBI_AUDIO_CHIME_NIGHT      "/spiffs/echo_en_end.wav"
#define DEBI_AUDIO_ALARM            "/spiffs/alarm-di.wav"
#define DEBI_AUDIO_ERROR            "/spiffs/networkError.mp3"
#define DEBI_AUDIO_BOOT             "/spiffs/echo_en_wake.wav"

/* —— Timing —— */
#ifndef DEBI_VOICE_ALARM_REPEAT_MS
#define DEBI_VOICE_ALARM_REPEAT_MS  3000   /* Repeat alarm every 3 s */
#endif

#ifndef DEBI_VOICE_ALERT_REPEAT_MS
#define DEBI_VOICE_ALERT_REPEAT_MS  1500   /* Urgent alert every 1.5 s */
#endif

#ifndef DEBI_VOICE_COOLDOWN_MS
#define DEBI_VOICE_COOLDOWN_MS      5000   /* Min gap between same-state chimes */
#endif

/* —— Volume —— */
#ifndef DEBI_VOICE_DEFAULT_VOLUME
#define DEBI_VOICE_DEFAULT_VOLUME   80     /* 0-100 */
#endif

/**
 * @brief Initialise the voice module.
 *
 * Must be called after app_audio_player_init() and
 * debi_face_bridge_init().
 */
void debi_voice_init(void);

/**
 * @brief Shut down the voice module.
 */
void debi_voice_deinit(void);

/**
 * @brief Notify the voice module of a face state change.
 *
 * Called by debi_face_bridge when the face transitions.
 * This is the main trigger for playing audio cues.
 *
 * @param prev  Previous face state
 * @param next  New face state
 */
void debi_voice_on_face_change(face_state_t prev, face_state_t next);

/**
 * @brief Set volume (0-100).
 *
 * 0 = mute, 100 = max.  Persists until changed.
 */
void debi_voice_set_volume(int volume);

/**
 * @brief Get current volume (0-100).
 */
int  debi_voice_get_volume(void);

/**
 * @brief Mute / unmute audio alerts.
 *
 * When muted, state transitions are still tracked but no
 * audio is played.
 */
void debi_voice_set_mute(bool mute);

/**
 * @brief Check if audio is currently muted.
 */
bool debi_voice_is_muted(void);

/**
 * @brief Stop any currently playing alert immediately.
 */
void debi_voice_stop(void);

/**
 * @brief Play a one-shot audio file (non-blocking).
 *
 * Utility for other modules that need to play a sound.
 * Respects mute setting.
 *
 * @param filepath  SPIFFS path e.g. "/spiffs/alarm-di.wav"
 */
void debi_voice_play_file(const char *filepath);

#ifdef __cplusplus
}
#endif

#endif /* DEBI_VOICE_H */
