/**
 * @file ui_face.h
 * @brief Debi Face Rendering Engine — LVGL 8.4
 *
 * Vector-based face drawn with LVGL draw callbacks (arcs, rects, polylines).
 * No bitmaps — everything is scalable and smooth.
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
#define DEBI_DISPLAY_WIDTH   466
#endif
#ifndef DEBI_DISPLAY_HEIGHT
#define DEBI_DISPLAY_HEIGHT  466
#endif

#define FACE_CX             (DEBI_DISPLAY_WIDTH / 2)
#define FACE_CY             (DEBI_DISPLAY_HEIGHT / 2)
#define FACE_RADIUS         (DEBI_DISPLAY_WIDTH / 2)

/* ── Face layout (tuned for 466px round display) ── */
#define FACE_EYE_SPACING    82      /* Half-dist between eye centres    */
#define FACE_EYE_Y_OFF      (-48)   /* Eyes above centre                */
#define FACE_EYE_BASE_R     36      /* Base eye radius before scaling   */
#define FACE_MOUTH_Y_OFF    62      /* Mouth below centre               */
#define FACE_MOUTH_BASE_W   58      /* Base mouth half-width            */
#define FACE_CURVE_SEGS     16      /* Segments for bezier approx       */

/* ── Gaze ── */
typedef struct {
    float x;
    float y;
} face_gaze_t;

/* ── Face parameters (interpolated during transitions) ── */
typedef struct {
    /* Eyes */
    float   eye_openness;       /* 0=closed  1=fully open              */
    float   eye_size;           /* Scale (1.0 = normal)                */
    float   pupil_size;         /* 0=tiny  1=normal                    */
    face_gaze_t gaze;

    /* Mouth */
    float   mouth_smile;        /* -1=frown  0=neutral  1=big smile    */
    float   mouth_open;         /* 0=closed  1=wide open               */
    float   mouth_width;        /* Scale factor                        */

    /* Emotion */
    float   happiness;          /* 0..1  >0.6 triggers crescent eyes   */

    /* Overall */
    float   brightness;         /* 0..1  master brightness             */
    uint32_t primary_color;     /* 0xRRGGBB                            */
    float   glow_intensity;     /* 0..1  ring glow                     */

    /* Effects (NOT interpolated — snapped from target) */
    bool    pulse;
    float   pulse_speed;
    bool    flash;
    bool    talking;
    bool    sparkle;
} face_params_t;

/* ── Public API ── */

void ui_face_init(lv_obj_t *parent);
void ui_face_deinit(void);

void ui_face_set_params(const face_params_t *params, uint32_t transition_ms);
const face_params_t *ui_face_get_params(void);

void ui_face_blink(void);
void ui_face_look(float x, float y);
void ui_face_show(bool visible);
bool ui_face_is_visible(void);

#ifdef __cplusplus
}
#endif
#endif /* UI_FACE_H */
