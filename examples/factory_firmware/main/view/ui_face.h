/**
 * @file   ui_face.h
 * @brief  Debi Face Rendering Engine v2 — Squared-Eye Bold Style
 *
 * Vector-based face drawn with LVGL 8.4 draw callbacks.
 * Squared rounded-rect eyes, heart eyes, police-flash alerts.
 *
 * Copyright (c) 2026 Debi Guardian
 */
#ifndef UI_FACE_H
#define UI_FACE_H

#include "lvgl.h"
#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ── Display geometry ── */
#ifndef DEBI_DISPLAY_WIDTH
#define DEBI_DISPLAY_WIDTH  466
#endif
#ifndef DEBI_DISPLAY_HEIGHT
#define DEBI_DISPLAY_HEIGHT 466
#endif

#define FACE_CX            (DEBI_DISPLAY_WIDTH  / 2)
#define FACE_CY            (DEBI_DISPLAY_HEIGHT / 2)
#define FACE_RADIUS        (DEBI_DISPLAY_WIDTH  / 2)

/* ── Face layout (tuned for 466px round display, squared-eye style) ── */
#define FACE_EYE_SPACING   82      /* Half-dist between eye centres       */
#define FACE_EYE_Y_OFF     (-30)   /* Eyes above centre                   */
#define FACE_EYE_W         80      /* Squared eye width                   */
#define FACE_EYE_H         86      /* Squared eye height                  */
#define FACE_EYE_R         18      /* Corner radius of rounded rect eyes  */
#define FACE_EYE_BASE_R    36      /* Legacy base eye radius (compat)     */
#define FACE_MOUTH_Y_OFF   80      /* Mouth below centre                  */
#define FACE_MOUTH_BASE_W  58      /* Base mouth half-width               */
#define FACE_CURVE_SEGS    16      /* Segments for bezier approximation   */

/* ── Highlight geometry ── */
#define FACE_HL_SIZE       18      /* Main white highlight square size    */
#define FACE_HL_R          5       /* Highlight corner radius             */
#define FACE_HL2_SIZE      11      /* Secondary highlight size            */

/* ── Heart bubble particles ── */
#define FACE_MAX_HEARTS    10      /* Max floating heart bubbles          */

/* ── Colour palette ── */
#define DEBI_COLOR_CYAN      0x38BDF8
#define DEBI_COLOR_TEAL      0x2DD4BF
#define DEBI_COLOR_CORAL     0xFB923C
#define DEBI_COLOR_BLUE      0x60A5FA
#define DEBI_COLOR_LAVENDER  0xA78BFA
#define DEBI_COLOR_WHITE     0xE2E8F0
#define DEBI_COLOR_PINK      0xF472B6
#define DEBI_COLOR_RED       0xFF3333
#define DEBI_COLOR_POLICE_BLUE  0x3366FF
#define DEBI_COLOR_BG        0x0F172A
#define DEBI_COLOR_AMBER     0xF59E0B
#define DEBI_COLOR_ORANGE    0xF97316
#define DEBI_COLOR_GREY      0x94A3B8

/* ── Eye style enum ── */
typedef enum {
    EYE_STYLE_SQUARED = 0,   /* Default: rounded-rect with highlights */
    EYE_STYLE_HEART,         /* Filled heart shapes (Love)            */
    EYE_STYLE_CRESCENT,      /* Happy arcs ^_^                        */
    EYE_STYLE_CLOSED,        /* Sleep: gentle curves                  */
    EYE_STYLE_WORRIED,       /* Tilted squared + worried brows        */
    EYE_STYLE_POLICE,        /* Alert: squared eyes flash red/blue    */
} eye_style_t;

/* ── Gaze ── */
typedef struct {
    float x;
    float y;
} face_gaze_t;

/* ── Face parameters (interpolated during transitions) ── */
typedef struct {
    /* Eyes */
    float        eye_openness;    /* 0=closed 1=fully open               */
    float        eye_size;        /* Scale (1.0 = normal)                */
    float        pupil_size;      /* 0=tiny 1=normal (unused in v2)      */
    face_gaze_t  gaze;
    eye_style_t  eye_style;       /* NEW: which eye shape to draw        */

    /* Mouth */
    float        mouth_smile;     /* -1=frown 0=neutral 1=big smile      */
    float        mouth_open;      /* 0=closed 1=wide open                */
    float        mouth_width;     /* Scale factor                        */

    /* Emotion */
    float        happiness;       /* 0..1                                */

    /* Overall */
    float        brightness;      /* 0..1 master brightness              */
    uint32_t     primary_color;   /* 0xRRGGBB main feature colour        */
    uint32_t     secondary_color; /* 0xRRGGBB accent (cheeks, hearts)    */
    float        glow_intensity;  /* 0..1 ring glow                      */

    /* Effects (NOT interpolated — snapped from target) */
    bool         pulse;
    float        pulse_speed;
    bool         flash;           /* Legacy red flash overlay             */
    bool         talking;
    bool         sparkle;
    bool         love_bubbles;    /* NEW: floating heart particles        */
    bool         alert_police;    /* NEW: red/blue police flash mode      */
    bool         show_alert_text; /* NEW: display "ALERT!" text           */
} face_params_t;

/* ── Public API ── */
void  ui_face_init(lv_obj_t *parent);
void  ui_face_deinit(void);
void  ui_face_set_params(const face_params_t *params, uint32_t transition_ms);
const face_params_t *ui_face_get_params(void);
void  ui_face_blink(void);
void  ui_face_look(float x, float y);
void  ui_face_show(bool visible);
bool  ui_face_is_visible(void);

#ifdef __cplusplus
}
#endif

#endif /* UI_FACE_H */
