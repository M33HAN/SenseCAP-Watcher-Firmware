/**
 * @file ui_face_states.c
 * @brief Debi Face State Machine — 14 Expression Presets
 *
 * Each preset matches the HTML simulator v3 exactly.
 * happiness > 0.6 triggers crescent-arc happy eyes.
 *
 * Copyright (c) 2026 Debi Guardian
 */

#include "ui_face_states.h"
#include "esp_log.h"

#ifndef DEBI_COLOR_TEAL
#define DEBI_COLOR_TEAL       0x0D9488
#define DEBI_COLOR_TEAL_LIGHT 0x5EEAD4
#define DEBI_COLOR_TEAL_DIM   0x064E3B
#define DEBI_COLOR_AMBER      0xF59E0B
#define DEBI_COLOR_ORANGE     0xF97316
#define DEBI_COLOR_RED        0xEF4444
#define DEBI_COLOR_WHITE      0xFFFFFF
#define DEBI_COLOR_GREY       0x94A3B8
#endif

#ifndef DEBI_FACE_TRANSITION_MS
#define DEBI_FACE_TRANSITION_MS 400
#endif

static const char *TAG = "ui_face_states";

static face_state_t s_state = FACE_STATE_IDLE;

static const char *STATE_NAMES[] = {
    [FACE_STATE_IDLE]         = "Idle",
    [FACE_STATE_PRESENCE]     = "Presence",
    [FACE_STATE_HAPPY]        = "Happy",
    [FACE_STATE_LISTENING]    = "Listening",
    [FACE_STATE_CONCERNED]    = "Concerned",
    [FACE_STATE_ALERT_FALL]   = "Alert:Fall",
    [FACE_STATE_ALERT_STILL]  = "Alert:Still",
    [FACE_STATE_ALERT_BABY]   = "Alert:Baby",
    [FACE_STATE_ALERT_HEART]  = "Alert:Heart",
    [FACE_STATE_NIGHT]        = "Night",
    [FACE_STATE_TALKING]      = "Talking",
    [FACE_STATE_BOOT]         = "Boot",
    [FACE_STATE_SETUP]        = "Setup",
    [FACE_STATE_ERROR]        = "Error",
};

/* ── Presets matching simulator v3 ── */
static const face_params_t PRESETS[] = {

    /* IDLE — Gentle smile, relaxed, teal glow */
    [FACE_STATE_IDLE] = {
        .eye_openness = 0.75f, .eye_size = 1.0f, .pupil_size = 0.85f,
        .gaze = {0, 0},
        .mouth_smile = 0.4f, .mouth_open = 0.0f, .mouth_width = 1.05f,
        .happiness = 0.3f,
        .brightness = 1.0f, .primary_color = DEBI_COLOR_TEAL,
        .glow_intensity = 0.45f,
        .pulse = false, .pulse_speed = 1.0f,
        .flash = false, .talking = false, .sparkle = false,
    },

    /* PRESENCE — Eyes widen, warm smile, "Hello there!" */
    [FACE_STATE_PRESENCE] = {
        .eye_openness = 0.92f, .eye_size = 1.12f, .pupil_size = 0.95f,
        .gaze = {0, 0},
        .mouth_smile = 0.55f, .mouth_open = 0.0f, .mouth_width = 1.15f,
        .happiness = 0.5f,
        .brightness = 1.0f, .primary_color = DEBI_COLOR_TEAL,
        .glow_intensity = 0.65f,
        .pulse = false, .pulse_speed = 1.0f,
        .flash = false, .talking = false, .sparkle = false,
    },

    /* HAPPY — Big smile, crescent eyes, sparkle */
    [FACE_STATE_HAPPY] = {
        .eye_openness = 0.3f, .eye_size = 1.3f, .pupil_size = 1.0f,
        .gaze = {0, 0},
        .mouth_smile = 1.0f, .mouth_open = 0.15f, .mouth_width = 1.45f,
        .happiness = 1.0f,
        .brightness = 1.0f, .primary_color = DEBI_COLOR_TEAL_LIGHT,
        .glow_intensity = 0.95f,
        .pulse = false, .pulse_speed = 1.0f,
        .flash = false, .talking = false, .sparkle = true,
    },

    /* LISTENING — Curious, pulsing ring */
    [FACE_STATE_LISTENING] = {
        .eye_openness = 1.0f, .eye_size = 1.15f, .pupil_size = 1.0f,
        .gaze = {0, 1},
        .mouth_smile = 0.15f, .mouth_open = 0.05f, .mouth_width = 0.9f,
        .happiness = 0.15f,
        .brightness = 1.0f, .primary_color = DEBI_COLOR_WHITE,
        .glow_intensity = 0.9f,
        .pulse = true, .pulse_speed = 1.5f,
        .flash = false, .talking = false, .sparkle = false,
    },

    /* CONCERNED — Worried, amber */
    [FACE_STATE_CONCERNED] = {
        .eye_openness = 0.85f, .eye_size = 1.0f, .pupil_size = 0.7f,
        .gaze = {0, 3},
        .mouth_smile = -0.3f, .mouth_open = 0.0f, .mouth_width = 0.85f,
        .happiness = 0.0f,
        .brightness = 1.0f, .primary_color = DEBI_COLOR_AMBER,
        .glow_intensity = 0.6f,
        .pulse = true, .pulse_speed = 0.8f,
        .flash = false, .talking = false, .sparkle = false,
    },

    /* ALERT: FALL — Wide eyes, urgent, red + klaxon */
    [FACE_STATE_ALERT_FALL] = {
        .eye_openness = 1.0f, .eye_size = 1.3f, .pupil_size = 0.45f,
        .gaze = {0, 4},
        .mouth_smile = -0.5f, .mouth_open = 0.35f, .mouth_width = 1.0f,
        .happiness = 0.0f,
        .brightness = 1.0f, .primary_color = DEBI_COLOR_RED,
        .glow_intensity = 1.0f,
        .pulse = true, .pulse_speed = 3.0f,
        .flash = true, .talking = false, .sparkle = false,
    },

    /* ALERT: STILL — Inactivity warning, orange */
    [FACE_STATE_ALERT_STILL] = {
        .eye_openness = 0.9f, .eye_size = 1.1f, .pupil_size = 0.6f,
        .gaze = {0, 3},
        .mouth_smile = -0.35f, .mouth_open = 0.1f, .mouth_width = 0.9f,
        .happiness = 0.0f,
        .brightness = 1.0f, .primary_color = DEBI_COLOR_ORANGE,
        .glow_intensity = 0.8f,
        .pulse = true, .pulse_speed = 1.2f,
        .flash = false, .talking = false, .sparkle = false,
    },

    /* ALERT: BABY — Breathing anomaly, red + klaxon */
    [FACE_STATE_ALERT_BABY] = {
        .eye_openness = 1.0f, .eye_size = 1.2f, .pupil_size = 0.55f,
        .gaze = {0, -2},
        .mouth_smile = -0.55f, .mouth_open = 0.25f, .mouth_width = 0.9f,
        .happiness = 0.0f,
        .brightness = 1.0f, .primary_color = DEBI_COLOR_RED,
        .glow_intensity = 1.0f,
        .pulse = true, .pulse_speed = 4.0f,
        .flash = true, .talking = false, .sparkle = false,
    },

    /* ALERT: HEART — Cardiac event, max urgency */
    [FACE_STATE_ALERT_HEART] = {
        .eye_openness = 1.0f, .eye_size = 1.4f, .pupil_size = 0.35f,
        .gaze = {0, 0},
        .mouth_smile = -0.65f, .mouth_open = 0.45f, .mouth_width = 1.1f,
        .happiness = 0.0f,
        .brightness = 1.0f, .primary_color = DEBI_COLOR_RED,
        .glow_intensity = 1.0f,
        .pulse = true, .pulse_speed = 6.0f,
        .flash = true, .talking = false, .sparkle = false,
    },

    /* NIGHT — Sleeping, minimal */
    [FACE_STATE_NIGHT] = {
        .eye_openness = 0.0f, .eye_size = 0.8f, .pupil_size = 0.0f,
        .gaze = {0, 0},
        .mouth_smile = 0.2f, .mouth_open = 0.0f, .mouth_width = 0.7f,
        .happiness = 0.2f,
        .brightness = 0.15f, .primary_color = DEBI_COLOR_TEAL_DIM,
        .glow_intensity = 0.1f,
        .pulse = false, .pulse_speed = 1.0f,
        .flash = false, .talking = false, .sparkle = false,
    },

    /* TALKING — Mouth auto-animates */
    [FACE_STATE_TALKING] = {
        .eye_openness = 0.8f, .eye_size = 1.05f, .pupil_size = 0.85f,
        .gaze = {0, 0},
        .mouth_smile = 0.3f, .mouth_open = 0.5f, .mouth_width = 1.1f,
        .happiness = 0.3f,
        .brightness = 1.0f, .primary_color = DEBI_COLOR_TEAL,
        .glow_intensity = 0.5f,
        .pulse = false, .pulse_speed = 1.0f,
        .flash = false, .talking = true, .sparkle = false,
    },

    /* BOOT — All zeroed, used during boot animation */
    [FACE_STATE_BOOT] = {
        .eye_openness = 0.0f, .eye_size = 0.0f, .pupil_size = 0.0f,
        .gaze = {0, 0},
        .mouth_smile = 0.0f, .mouth_open = 0.0f, .mouth_width = 0.0f,
        .happiness = 0.0f,
        .brightness = 0.0f, .primary_color = DEBI_COLOR_TEAL,
        .glow_intensity = 0.0f,
        .pulse = false, .pulse_speed = 1.0f,
        .flash = false, .talking = false, .sparkle = false,
    },

    /* SETUP — Excited, looking around */
    [FACE_STATE_SETUP] = {
        .eye_openness = 1.0f, .eye_size = 1.2f, .pupil_size = 1.0f,
        .gaze = {5, 0},
        .mouth_smile = 0.55f, .mouth_open = 0.08f, .mouth_width = 1.2f,
        .happiness = 0.5f,
        .brightness = 1.0f, .primary_color = DEBI_COLOR_TEAL_LIGHT,
        .glow_intensity = 0.7f,
        .pulse = true, .pulse_speed = 1.0f,
        .flash = false, .talking = false, .sparkle = false,
    },

    /* ERROR — Confused, grey */
    [FACE_STATE_ERROR] = {
        .eye_openness = 0.6f, .eye_size = 0.9f, .pupil_size = 0.5f,
        .gaze = {-3, 2},
        .mouth_smile = -0.15f, .mouth_open = 0.05f, .mouth_width = 0.8f,
        .happiness = 0.0f,
        .brightness = 0.7f, .primary_color = DEBI_COLOR_GREY,
        .glow_intensity = 0.2f,
        .pulse = false, .pulse_speed = 1.0f,
        .flash = false, .talking = false, .sparkle = false,
    },
};

/* ══════════════════════════════════════════════════ */

void ui_face_states_init(void)
{
    ESP_LOGI(TAG, "Face states ready (%d presets)", FACE_STATE_COUNT);
    s_state = FACE_STATE_IDLE;
}

void ui_face_set_state(face_state_t state)
{
    if (state >= FACE_STATE_COUNT) return;
    if (state == s_state) return;

    ESP_LOGI(TAG, "%s -> %s", STATE_NAMES[s_state], STATE_NAMES[state]);
    s_state = state;

    /* Pick transition speed */
    uint32_t ms = DEBI_FACE_TRANSITION_MS;
    if (state >= FACE_STATE_ALERT_FALL && state <= FACE_STATE_ALERT_HEART) {
        ms = 200;    /* Urgent → snappy */
    } else if (state == FACE_STATE_NIGHT) {
        ms = 1000;   /* Night → gentle fade */
    } else if (state == FACE_STATE_HAPPY) {
        ms = 350;    /* Happy → slightly slower for warmth */
    }

    ui_face_set_params(&PRESETS[state], ms);
}

face_state_t ui_face_get_state(void)
{
    return s_state;
}

const char *ui_face_state_name(face_state_t state)
{
    if (state >= FACE_STATE_COUNT) return "Unknown";
    return STATE_NAMES[state];
}
