/**
 * @file   ui_face_states.c
 * @brief  Debi Face State Machine v2 - 15 Expression Presets
 *
 * Squared-eye bold style with police-flash alerts and love hearts.
 *
 * Copyright (c) 2026 Debi Guardian
 */
#include "ui_face_states.h"
#include "esp_log.h"

#ifndef DEBI_FACE_TRANSITION_MS
#define DEBI_FACE_TRANSITION_MS 400
#endif

static const char *TAG = "ui_face_states";
static face_state_t s_state = FACE_STATE_IDLE;

static const char *STATE_NAMES[] = {
    [FACE_STATE_IDLE]        = "Idle",
    [FACE_STATE_PRESENCE]    = "Presence",
    [FACE_STATE_HAPPY]       = "Happy",
    [FACE_STATE_LOVE]        = "Love",
    [FACE_STATE_LISTENING]   = "Listening",
    [FACE_STATE_CONCERNED]   = "Concerned",
    [FACE_STATE_ALERT_FALL]  = "Alert:Fall",
    [FACE_STATE_ALERT_STILL] = "Alert:Still",
    [FACE_STATE_ALERT_BABY]  = "Alert:Baby",
    [FACE_STATE_ALERT_HEART] = "Alert:Heart",
    [FACE_STATE_NIGHT]       = "Night",
    [FACE_STATE_TALKING]     = "Talking",
    [FACE_STATE_BOOT]        = "Boot",
    [FACE_STATE_SETUP]       = "Setup",
    [FACE_STATE_ERROR]       = "Error",
};

static const face_params_t PRESETS[] = {
    /* IDLE - Squared eyes, gentle smile, cyan */
    [FACE_STATE_IDLE] = {
        .eye_openness=0.75f, .eye_size=1.0f, .pupil_size=0.85f,
        .gaze={0,0}, .eye_style=EYE_STYLE_SQUARED,
        .mouth_smile=0.4f, .mouth_open=0.0f, .mouth_width=1.05f,
        .happiness=0.3f, .brightness=1.0f,
        .primary_color=DEBI_COLOR_CYAN, .secondary_color=DEBI_COLOR_PINK,
        .glow_intensity=0.45f,
        .pulse=false, .pulse_speed=1.0f, .flash=false,
        .talking=false, .sparkle=false,
        .love_bubbles=false, .alert_police=false, .show_alert_text=false,
    },
    /* PRESENCE - Eyes widen, warm smile */
    [FACE_STATE_PRESENCE] = {
        .eye_openness=0.92f, .eye_size=1.12f, .pupil_size=0.95f,
        .gaze={0,0}, .eye_style=EYE_STYLE_SQUARED,
        .mouth_smile=0.55f, .mouth_open=0.0f, .mouth_width=1.15f,
        .happiness=0.5f, .brightness=1.0f,
        .primary_color=DEBI_COLOR_CYAN, .secondary_color=DEBI_COLOR_PINK,
        .glow_intensity=0.65f,
        .pulse=false, .pulse_speed=1.0f, .flash=false,
        .talking=false, .sparkle=false,
        .love_bubbles=false, .alert_police=false, .show_alert_text=false,
    },
    /* HAPPY - Crescent eyes, big D-grin, cheeks */
    [FACE_STATE_HAPPY] = {
        .eye_openness=0.3f, .eye_size=1.3f, .pupil_size=1.0f,
        .gaze={0,0}, .eye_style=EYE_STYLE_CRESCENT,
        .mouth_smile=1.0f, .mouth_open=0.15f, .mouth_width=1.45f,
        .happiness=1.0f, .brightness=1.0f,
        .primary_color=DEBI_COLOR_CYAN, .secondary_color=DEBI_COLOR_PINK,
        .glow_intensity=0.85f,
        .pulse=false, .pulse_speed=1.0f, .flash=false,
        .talking=false, .sparkle=true,
        .love_bubbles=false, .alert_police=false, .show_alert_text=false,
    },
    /* LOVE - Heart eyes, floating hearts, pink accent */
    [FACE_STATE_LOVE] = {
        .eye_openness=1.0f, .eye_size=1.1f, .pupil_size=1.0f,
        .gaze={0,0}, .eye_style=EYE_STYLE_HEART,
        .mouth_smile=0.6f, .mouth_open=0.0f, .mouth_width=1.0f,
        .happiness=0.9f, .brightness=1.0f,
        .primary_color=DEBI_COLOR_CYAN, .secondary_color=DEBI_COLOR_PINK,
        .glow_intensity=0.7f,
        .pulse=false, .pulse_speed=1.0f, .flash=false,
        .talking=false, .sparkle=false,
        .love_bubbles=true, .alert_police=false, .show_alert_text=false,
    },
    /* LISTENING - Pulsing glow, wide eyes */
    [FACE_STATE_LISTENING] = {
        .eye_openness=1.0f, .eye_size=1.15f, .pupil_size=1.0f,
        .gaze={0,1}, .eye_style=EYE_STYLE_SQUARED,
        .mouth_smile=0.15f, .mouth_open=0.05f, .mouth_width=0.9f,
        .happiness=0.15f, .brightness=1.0f,
        .primary_color=DEBI_COLOR_WHITE, .secondary_color=DEBI_COLOR_PINK,
        .glow_intensity=0.9f,
        .pulse=true, .pulse_speed=1.5f, .flash=false,
        .talking=false, .sparkle=false,
        .love_bubbles=false, .alert_police=false, .show_alert_text=false,
    },
    /* CONCERNED - Worried eyes with brows, amber */
    [FACE_STATE_CONCERNED] = {
        .eye_openness=0.85f, .eye_size=1.0f, .pupil_size=0.7f,
        .gaze={0,3}, .eye_style=EYE_STYLE_WORRIED,
        .mouth_smile=-0.3f, .mouth_open=0.0f, .mouth_width=0.85f,
        .happiness=0.0f, .brightness=1.0f,
        .primary_color=DEBI_COLOR_AMBER, .secondary_color=DEBI_COLOR_PINK,
        .glow_intensity=0.6f,
        .pulse=true, .pulse_speed=0.8f, .flash=false,
        .talking=false, .sparkle=false,
        .love_bubbles=false, .alert_police=false, .show_alert_text=false,
    },
    /* ALERT: FALL - Police flash, ALERT! text */
    [FACE_STATE_ALERT_FALL] = {
        .eye_openness=1.0f, .eye_size=1.2f, .pupil_size=0.45f,
        .gaze={0,0}, .eye_style=EYE_STYLE_POLICE,
        .mouth_smile=-0.5f, .mouth_open=0.0f, .mouth_width=1.0f,
        .happiness=0.0f, .brightness=1.0f,
        .primary_color=DEBI_COLOR_RED, .secondary_color=DEBI_COLOR_PINK,
        .glow_intensity=1.0f,
        .pulse=true, .pulse_speed=3.0f, .flash=false,
        .talking=false, .sparkle=false,
        .love_bubbles=false, .alert_police=true, .show_alert_text=true,
    },
    /* ALERT: STILL */
    [FACE_STATE_ALERT_STILL] = {
        .eye_openness=0.9f, .eye_size=1.1f, .pupil_size=0.6f,
        .gaze={0,0}, .eye_style=EYE_STYLE_POLICE,
        .mouth_smile=-0.35f, .mouth_open=0.0f, .mouth_width=0.9f,
        .happiness=0.0f, .brightness=1.0f,
        .primary_color=DEBI_COLOR_ORANGE, .secondary_color=DEBI_COLOR_PINK,
        .glow_intensity=0.8f,
        .pulse=true, .pulse_speed=1.2f, .flash=false,
        .talking=false, .sparkle=false,
        .love_bubbles=false, .alert_police=true, .show_alert_text=true,
    },
    /* ALERT: BABY */
    [FACE_STATE_ALERT_BABY] = {
        .eye_openness=1.0f, .eye_size=1.2f, .pupil_size=0.55f,
        .gaze={0,0}, .eye_style=EYE_STYLE_POLICE,
        .mouth_smile=-0.55f, .mouth_open=0.0f, .mouth_width=0.9f,
        .happiness=0.0f, .brightness=1.0f,
        .primary_color=DEBI_COLOR_RED, .secondary_color=DEBI_COLOR_PINK,
        .glow_intensity=1.0f,
        .pulse=true, .pulse_speed=4.0f, .flash=false,
        .talking=false, .sparkle=false,
        .love_bubbles=false, .alert_police=true, .show_alert_text=true,
    },
    /* ALERT: HEART */
    [FACE_STATE_ALERT_HEART] = {
        .eye_openness=1.0f, .eye_size=1.3f, .pupil_size=0.35f,
        .gaze={0,0}, .eye_style=EYE_STYLE_POLICE,
        .mouth_smile=-0.65f, .mouth_open=0.0f, .mouth_width=1.1f,
        .happiness=0.0f, .brightness=1.0f,
        .primary_color=DEBI_COLOR_RED, .secondary_color=DEBI_COLOR_PINK,
        .glow_intensity=1.0f,
        .pulse=true, .pulse_speed=6.0f, .flash=false,
        .talking=false, .sparkle=false,
        .love_bubbles=false, .alert_police=true, .show_alert_text=true,
    },
    /* NIGHT - Sleep */
    [FACE_STATE_NIGHT] = {
        .eye_openness=0.0f, .eye_size=0.8f, .pupil_size=0.0f,
        .gaze={0,0}, .eye_style=EYE_STYLE_CLOSED,
        .mouth_smile=0.2f, .mouth_open=0.0f, .mouth_width=0.7f,
        .happiness=0.2f, .brightness=0.15f,
        .primary_color=DEBI_COLOR_CYAN, .secondary_color=DEBI_COLOR_PINK,
        .glow_intensity=0.1f,
        .pulse=false, .pulse_speed=1.0f, .flash=false,
        .talking=false, .sparkle=false,
        .love_bubbles=false, .alert_police=false, .show_alert_text=false,
    },
    /* TALKING */
    [FACE_STATE_TALKING] = {
        .eye_openness=0.8f, .eye_size=1.05f, .pupil_size=0.85f,
        .gaze={0,0}, .eye_style=EYE_STYLE_SQUARED,
        .mouth_smile=0.3f, .mouth_open=0.5f, .mouth_width=1.1f,
        .happiness=0.3f, .brightness=1.0f,
        .primary_color=DEBI_COLOR_CYAN, .secondary_color=DEBI_COLOR_PINK,
        .glow_intensity=0.5f,
        .pulse=false, .pulse_speed=1.0f, .flash=false,
        .talking=true, .sparkle=false,
        .love_bubbles=false, .alert_police=false, .show_alert_text=false,
    },
    /* BOOT */
    [FACE_STATE_BOOT] = {
        .eye_openness=0.0f, .eye_size=0.0f, .pupil_size=0.0f,
        .gaze={0,0}, .eye_style=EYE_STYLE_SQUARED,
        .mouth_smile=0.0f, .mouth_open=0.0f, .mouth_width=0.0f,
        .happiness=0.0f, .brightness=0.0f,
        .primary_color=DEBI_COLOR_CYAN, .secondary_color=DEBI_COLOR_PINK,
        .glow_intensity=0.0f,
        .pulse=false, .pulse_speed=1.0f, .flash=false,
        .talking=false, .sparkle=false,
        .love_bubbles=false, .alert_police=false, .show_alert_text=false,
    },
    /* SETUP */
    [FACE_STATE_SETUP] = {
        .eye_openness=1.0f, .eye_size=1.2f, .pupil_size=1.0f,
        .gaze={5,0}, .eye_style=EYE_STYLE_SQUARED,
        .mouth_smile=0.55f, .mouth_open=0.08f, .mouth_width=1.2f,
        .happiness=0.5f, .brightness=1.0f,
        .primary_color=DEBI_COLOR_CYAN, .secondary_color=DEBI_COLOR_PINK,
        .glow_intensity=0.7f,
        .pulse=true, .pulse_speed=1.0f, .flash=false,
        .talking=false, .sparkle=false,
        .love_bubbles=false, .alert_police=false, .show_alert_text=false,
    },
    /* ERROR */
    [FACE_STATE_ERROR] = {
        .eye_openness=0.6f, .eye_size=0.9f, .pupil_size=0.5f,
        .gaze={-3,2}, .eye_style=EYE_STYLE_WORRIED,
        .mouth_smile=-0.15f, .mouth_open=0.05f, .mouth_width=0.8f,
        .happiness=0.0f, .brightness=0.7f,
        .primary_color=DEBI_COLOR_GREY, .secondary_color=DEBI_COLOR_PINK,
        .glow_intensity=0.2f,
        .pulse=false, .pulse_speed=1.0f, .flash=false,
        .talking=false, .sparkle=false,
        .love_bubbles=false, .alert_police=false, .show_alert_text=false,
    },
};

void ui_face_states_init(void) {
    ESP_LOGI(TAG, "Face states v2 ready (%d presets)", FACE_STATE_COUNT);
    s_state = FACE_STATE_IDLE;
}

void ui_face_set_state(face_state_t state) {
    if (state >= FACE_STATE_COUNT) return;
    if (state == s_state) return;
    ESP_LOGI(TAG, "%s -> %s", STATE_NAMES[s_state], STATE_NAMES[state]);
    s_state = state;
    uint32_t ms = DEBI_FACE_TRANSITION_MS;
    if (state >= FACE_STATE_ALERT_FALL && state <= FACE_STATE_ALERT_HEART)
        ms = 150;
    else if (state == FACE_STATE_NIGHT) ms = 1000;
    else if (state == FACE_STATE_HAPPY) ms = 350;
    else if (state == FACE_STATE_LOVE)  ms = 400;
    ui_face_set_params(&PRESETS[state], ms);
}

face_state_t ui_face_get_state(void) { return s_state; }

const char *ui_face_state_name(face_state_t state) {
    if (state >= FACE_STATE_COUNT) return "Unknown";
    return STATE_NAMES[state];
}
