/**
 * @file ui_face.c
 * @brief Debi Face Rendering Engine — LVGL 8.4 Implementation
 *
 * Uses LV_EVENT_DRAW_MAIN callback for custom vector rendering.
 * All face elements drawn with lv_draw_rect, lv_draw_arc, lv_draw_line.
 * Bezier curves approximated with polylines for mouth and crescent eyes.
 *
 * Frame budget: ~30 FPS on ESP32-S3 @ 240 MHz.
 *
 * Copyright (c) 2026 Debi Guardian
 */

#include "ui_face.h"
#include "esp_log.h"
#include "esp_timer.h"
#include <math.h>
#include <stdlib.h>

static const char *TAG = "ui_face";

/* ══════════════════════════════════════════════════
 * Internal state
 * ══════════════════════════════════════════════════ */

static lv_obj_t   *s_face_obj = NULL;       /* Full-screen draw target      */
static lv_timer_t  *s_tick_timer = NULL;     /* 33ms tick (≈30 FPS)          */

/* Current (rendered) and target params */
static face_params_t s_cur;
static face_params_t s_tgt;

/* Transition */
static float    s_trans_progress = 1.0f;     /* 0→1, 1=done                  */
static float    s_trans_speed    = 0.04f;    /* Progress per tick            */

/* Blink */
static int      s_blink_timer   = 120;      /* Ticks until next blink       */
static int      s_blink_phase   = 0;        /* >0 = blinking (counts down)  */
#define BLINK_HALF_TICKS 6                   /* 6 ticks close + 6 open       */

/* Wander (idle gaze drift) */
static int      s_wander_timer  = 60;

/* Frame counter for animations */
static uint32_t s_frame = 0;

/* ══════════════════════════════════════════════════
 * Helpers
 * ══════════════════════════════════════════════════ */

static inline float lerpf(float a, float b, float t) {
    return a + (b - a) * t;
}

static inline float ease_in_out(float t) {
    /* Smooth cubic ease-in-out */
    if (t < 0.5f) return 2.0f * t * t;
    return 1.0f - powf(-2.0f * t + 2.0f, 2) / 2.0f;
}

static inline float clampf(float v, float lo, float hi) {
    return v < lo ? lo : (v > hi ? hi : v);
}

static inline uint32_t millis(void) {
    return (uint32_t)(esp_timer_get_time() / 1000);
}

static inline uint8_t r_of(uint32_t c) { return (c >> 16) & 0xFF; }
static inline uint8_t g_of(uint32_t c) { return (c >>  8) & 0xFF; }
static inline uint8_t b_of(uint32_t c) { return  c        & 0xFF; }

static lv_color_t color_from_hex(uint32_t hex) {
    return lv_color_make(r_of(hex), g_of(hex), b_of(hex));
}

/* Lerp two 0xRRGGBB colours, return lv_color_t */
static lv_color_t color_lerp(uint32_t a, uint32_t b, float t) {
    uint8_t r = (uint8_t)lerpf(r_of(a), r_of(b), t);
    uint8_t g = (uint8_t)lerpf(g_of(a), g_of(b), t);
    uint8_t bl = (uint8_t)lerpf(b_of(a), b_of(b), t);
    return lv_color_make(r, g, bl);
}

/* Quadratic bezier: compute point at parameter t ∈ [0,1] */
static void qbezier(float x0, float y0, float cx, float cy,
                    float x1, float y1, float t,
                    float *ox, float *oy) {
    float u = 1.0f - t;
    *ox = u * u * x0 + 2.0f * u * t * cx + t * t * x1;
    *oy = u * u * y0 + 2.0f * u * t * cy + t * t * y1;
}

/* ══════════════════════════════════════════════════
 * Animation step (called each tick before draw)
 * ══════════════════════════════════════════════════ */

static void face_animate_step(void)
{
    /* --- Interpolate smoothable params toward target --- */
    if (s_trans_progress < 1.0f) {
        s_trans_progress += s_trans_speed;
        if (s_trans_progress > 1.0f) s_trans_progress = 1.0f;
        float t = ease_in_out(s_trans_progress);

        s_cur.eye_openness  = lerpf(s_cur.eye_openness,  s_tgt.eye_openness,  t);
        s_cur.eye_size      = lerpf(s_cur.eye_size,      s_tgt.eye_size,      t);
        s_cur.pupil_size    = lerpf(s_cur.pupil_size,    s_tgt.pupil_size,    t);
        s_cur.gaze.x        = lerpf(s_cur.gaze.x,        s_tgt.gaze.x,        t);
        s_cur.gaze.y        = lerpf(s_cur.gaze.y,        s_tgt.gaze.y,        t);
        s_cur.mouth_smile   = lerpf(s_cur.mouth_smile,   s_tgt.mouth_smile,   t);
        s_cur.mouth_open    = lerpf(s_cur.mouth_open,    s_tgt.mouth_open,    t);
        s_cur.mouth_width   = lerpf(s_cur.mouth_width,   s_tgt.mouth_width,   t);
        s_cur.happiness     = lerpf(s_cur.happiness,     s_tgt.happiness,     t);
        s_cur.brightness    = lerpf(s_cur.brightness,    s_tgt.brightness,    t);
        s_cur.glow_intensity = lerpf(s_cur.glow_intensity, s_tgt.glow_intensity, t);
    }

    /* Snap non-interpolated effect flags */
    s_cur.pulse       = s_tgt.pulse;
    s_cur.pulse_speed = s_tgt.pulse_speed;
    s_cur.flash       = s_tgt.flash;
    s_cur.talking     = s_tgt.talking;
    s_cur.sparkle     = s_tgt.sparkle;

    /* --- Blink --- */
    if (s_cur.eye_openness > 0.05f && s_cur.happiness <= 0.7f) {
        s_blink_timer--;
        if (s_blink_timer <= 0 && s_blink_phase == 0) {
            s_blink_phase = BLINK_HALF_TICKS * 2;
            s_blink_timer = 180 + (rand() % 300);
        }
    }
    if (s_blink_phase > 0) s_blink_phase--;

    /* --- Idle gaze wander --- */
    s_wander_timer--;
    if (s_wander_timer <= 0) {
        /* Small random gaze drift in idle */
        s_tgt.gaze.x = ((float)(rand() % 100) / 100.0f - 0.5f) * 4.0f;
        s_tgt.gaze.y = ((float)(rand() % 100) / 100.0f - 0.5f) * 2.0f;
        s_wander_timer = 120 + (rand() % 240);
    }
}

/* Blink multiplier: 1.0 = normal, 0.0 = fully closed */
static float blink_mul(void)
{
    if (s_blink_phase <= 0) return 1.0f;
    if (s_blink_phase > BLINK_HALF_TICKS)
        return lerpf(1.0f, 0.0f, (float)(BLINK_HALF_TICKS * 2 - s_blink_phase) / BLINK_HALF_TICKS);
    else
        return lerpf(0.0f, 1.0f, (float)(BLINK_HALF_TICKS - s_blink_phase) / BLINK_HALF_TICKS);
}

/* ══════════════════════════════════════════════════
 * Drawing helpers
 * ══════════════════════════════════════════════════ */

/* Draw a filled circle at (cx,cy) with radius r, color, opacity 0-255 */
static void draw_filled_circle(lv_draw_ctx_t *draw_ctx,
                               lv_coord_t cx, lv_coord_t cy,
                               lv_coord_t r, lv_color_t color, lv_opa_t opa)
{
    lv_draw_rect_dsc_t dsc;
    lv_draw_rect_dsc_init(&dsc);
    dsc.bg_color = color;
    dsc.bg_opa = opa;
    dsc.radius = LV_RADIUS_CIRCLE;
    dsc.border_width = 0;

    lv_area_t area;
    area.x1 = cx - r;
    area.y1 = cy - r;
    area.x2 = cx + r;
    area.y2 = cy + r;
    lv_draw_rect(draw_ctx, &dsc, &area);
}

/* Draw a filled ellipse at (cx,cy) with radii (rx,ry) */
static void draw_filled_ellipse(lv_draw_ctx_t *draw_ctx,
                                lv_coord_t cx, lv_coord_t cy,
                                lv_coord_t rx, lv_coord_t ry,
                                lv_color_t color, lv_opa_t opa)
{
    lv_draw_rect_dsc_t dsc;
    lv_draw_rect_dsc_init(&dsc);
    dsc.bg_color = color;
    dsc.bg_opa = opa;
    dsc.radius = LV_RADIUS_CIRCLE;
    dsc.border_width = 0;

    lv_area_t area;
    area.x1 = cx - rx;
    area.y1 = cy - ry;
    area.x2 = cx + rx;
    area.y2 = cy + ry;
    lv_draw_rect(draw_ctx, &dsc, &area);
}

/* Draw a quadratic bezier curve as a polyline */
static void draw_qbezier(lv_draw_ctx_t *draw_ctx,
                         float x0, float y0,
                         float cpx, float cpy,
                         float x1, float y1,
                         lv_color_t color, lv_opa_t opa,
                         lv_coord_t width)
{
    lv_draw_line_dsc_t dsc;
    lv_draw_line_dsc_init(&dsc);
    dsc.color = color;
    dsc.opa = opa;
    dsc.width = width;
    dsc.round_start = 1;
    dsc.round_end = 1;

    float px, py, nx, ny;
    qbezier(x0, y0, cpx, cpy, x1, y1, 0.0f, &px, &py);

    for (int i = 1; i <= FACE_CURVE_SEGS; i++) {
        float t = (float)i / FACE_CURVE_SEGS;
        qbezier(x0, y0, cpx, cpy, x1, y1, t, &nx, &ny);

        lv_point_t p1 = {(lv_coord_t)px, (lv_coord_t)py};
        lv_point_t p2 = {(lv_coord_t)nx, (lv_coord_t)ny};
        lv_draw_line(draw_ctx, &dsc, &p1, &p2);

        px = nx;
        py = ny;
    }
}

/* ══════════════════════════════════════════════════
 * Draw: Glow Ring
 * ══════════════════════════════════════════════════ */

static void draw_glow_ring(lv_draw_ctx_t *draw_ctx)
{
    float g = s_cur.glow_intensity * s_cur.brightness;

    /* Pulse modulation */
    if (s_cur.pulse) {
        float spd = s_cur.pulse_speed > 0.0f ? s_cur.pulse_speed : 1.0f;
        g *= (0.3f + 0.7f * (0.5f + 0.5f * sinf(s_frame * spd * 0.05f)));
    } else {
        /* Gentle breathing in idle */
        g *= (0.6f + 0.4f * (0.5f + 0.5f * sinf(s_frame * 0.02f)));
    }

    if (g < 0.01f) return;

    lv_color_t col = color_from_hex(s_cur.primary_color);

    /* Multiple concentric glow rings (outer → inner, increasing opacity) */
    for (int i = 4; i >= 0; i--) {
        lv_draw_arc_dsc_t dsc;
        lv_draw_arc_dsc_init(&dsc);
        dsc.color = col;
        dsc.opa = (lv_opa_t)(g * 0.12f * (5 - i) * 255.0f);
        dsc.width = 12 + i * 7;
        dsc.rounded = 0;

        lv_point_t center = {FACE_CX, FACE_CY};
        lv_coord_t r = FACE_RADIUS - 2 - i * 9;
        lv_draw_arc(draw_ctx, &dsc, &center, r, 0, 360);
    }

    /* Sharp inner ring */
    {
        lv_draw_arc_dsc_t dsc;
        lv_draw_arc_dsc_init(&dsc);
        dsc.color = col;
        dsc.opa = (lv_opa_t)(g * 0.8f * 255.0f);
        dsc.width = 3;
        dsc.rounded = 0;

        lv_point_t center = {FACE_CX, FACE_CY};
        lv_draw_arc(draw_ctx, &dsc, &center, FACE_RADIUS - 8, 0, 360);
    }
}

/* ══════════════════════════════════════════════════
 * Draw: Eyes
 * ══════════════════════════════════════════════════ */

static void draw_eye(lv_draw_ctx_t *draw_ctx, int side)
{
    /* side: -1=left, +1=right */
    float b = s_cur.brightness;
    if (b < 0.02f) return;

    float sp = FACE_EYE_SPACING * s_cur.eye_size;
    float ey = FACE_CY + FACE_EYE_Y_OFF;
    float br = FACE_EYE_BASE_R * s_cur.eye_size;
    float op = s_cur.eye_openness * blink_mul();
    float gx = s_cur.gaze.x * 2.2f;
    float gy = s_cur.gaze.y * 2.2f;
    float hap = s_cur.happiness;
    lv_color_t col = color_from_hex(s_cur.primary_color);
    lv_opa_t master_opa = (lv_opa_t)(b * 255.0f);

    float ex = FACE_CX + side * sp + gx;
    float eyy = ey + gy;

    /* ── HAPPY CRESCENT EYES (happiness > 0.6) ── */
    if (hap > 0.6f) {
        float crescH = 12.0f + hap * 8.0f;
        float crescW = br * 1.2f;

        /* Soft glow underneath */
        draw_filled_circle(draw_ctx,
            (lv_coord_t)ex, (lv_coord_t)eyy,
            (lv_coord_t)(crescW * 2),
            col, (lv_opa_t)(b * 0.12f * 255.0f));

        /* Upper crescent curve (the "happy arc") */
        draw_qbezier(draw_ctx,
            ex - crescW, eyy + 4,       /* start         */
            ex, eyy - crescH * 1.8f,    /* control point */
            ex + crescW, eyy + 4,       /* end           */
            lv_color_white(), (lv_opa_t)(b * 0.92f * 255), 4);

        /* Lower crescent curve (closes the shape) */
        draw_qbezier(draw_ctx,
            ex + crescW, eyy + 4,
            ex, eyy - crescH * 0.5f,
            ex - crescW, eyy + 4,
            lv_color_white(), (lv_opa_t)(b * 0.6f * 255), 3);

        /* Fill between the curves with a thick center stroke */
        draw_qbezier(draw_ctx,
            ex - crescW * 0.8f, eyy + 3,
            ex, eyy - crescH * 1.2f,
            ex + crescW * 0.8f, eyy + 3,
            lv_color_white(), (lv_opa_t)(b * 0.5f * 255), 8);

        /* Little sparkle dot on each eye */
        float sparkle_opa = 0.5f + 0.3f * sinf(s_frame * 0.08f);
        draw_filled_circle(draw_ctx,
            (lv_coord_t)(ex + side * 8), (lv_coord_t)(eyy - crescH * 0.8f),
            3, lv_color_white(), (lv_opa_t)(b * sparkle_opa * 255));

        return;
    }

    /* ── CLOSED EYES (sleeping / blink) ── */
    if (op < 0.08f) {
        /* Single curved line */
        float curve_dir = (hap > 0.1f) ? -8.0f : 8.0f;
        draw_qbezier(draw_ctx,
            ex - br * 1.1f, eyy,
            ex, eyy + curve_dir,
            ex + br * 1.1f, eyy,
            col, (lv_opa_t)(b * 0.5f * 255), 3);
        return;
    }

    /* ── NORMAL OPEN EYES ── */
    lv_coord_t eW = (lv_coord_t)(br * 1.05f);
    lv_coord_t eH = (lv_coord_t)(br * op);

    /* Soft glow behind eye */
    draw_filled_circle(draw_ctx,
        (lv_coord_t)ex, (lv_coord_t)eyy,
        eW * 2, col, (lv_opa_t)(b * 0.1f * 255));

    /* Eye shape — filled ellipse (white with colour tint edge) */
    draw_filled_ellipse(draw_ctx,
        (lv_coord_t)ex, (lv_coord_t)eyy,
        eW, eH,
        lv_color_white(), (lv_opa_t)(b * 0.95f * 255));

    /* Colour tint ring around the eye */
    {
        lv_draw_arc_dsc_t arc;
        lv_draw_arc_dsc_init(&arc);
        arc.color = col;
        arc.opa = (lv_opa_t)(b * 0.2f * 255);
        arc.width = 2;
        lv_point_t center = {(lv_coord_t)ex, (lv_coord_t)eyy};
        lv_draw_arc(draw_ctx, &arc, &center, (eW + eH) / 2, 0, 360);
    }

    /* Pupil */
    float pr = br * s_cur.pupil_size * 0.45f;
    float px = gx * 0.25f;
    float py = gy * 0.25f;
    if (pr > 1.0f) {
        /* Dark pupil */
        draw_filled_circle(draw_ctx,
            (lv_coord_t)(ex + px), (lv_coord_t)(eyy + py),
            (lv_coord_t)pr,
            lv_color_make(8, 12, 30), master_opa);

        /* Highlight dots */
        lv_coord_t pr_i = (lv_coord_t)pr;
        draw_filled_circle(draw_ctx,
            (lv_coord_t)(ex + px - pr * 0.3f),
            (lv_coord_t)(eyy + py - pr * 0.35f),
            pr_i * 32 / 100,
            lv_color_white(), (lv_opa_t)(b * 0.8f * 255));

        draw_filled_circle(draw_ctx,
            (lv_coord_t)(ex + px + pr * 0.2f),
            (lv_coord_t)(eyy + py + pr * 0.2f),
            pr_i * 15 / 100,
            lv_color_white(), (lv_opa_t)(b * 0.35f * 255));
    }
}

/* ── Eyebrows ── */
static void draw_eyebrows(lv_draw_ctx_t *draw_ctx)
{
    float b = s_cur.brightness;
    if (b < 0.02f) return;
    float sp = FACE_EYE_SPACING * s_cur.eye_size;
    float ey = FACE_CY + FACE_EYE_Y_OFF;
    float br = FACE_EYE_BASE_R * s_cur.eye_size;
    float gx = s_cur.gaze.x * 2.2f;
    float gy = s_cur.gaze.y * 2.2f;
    lv_color_t col = color_from_hex(s_cur.primary_color);

    /* Friendly raised brows for smiling states */
    if (s_cur.mouth_smile > 0.35f && s_cur.happiness < 0.6f) {
        for (int side = -1; side <= 1; side += 2) {
            float bx = FACE_CX + side * sp + gx;
            float by = ey - br * 1.25f + gy;
            draw_qbezier(draw_ctx,
                bx - br * 0.6f, by + 3,
                bx, by - 8,
                bx + br * 0.6f, by + 3,
                col, (lv_opa_t)(b * 0.35f * 255), 3);
        }
    }

    /* Worried brows for alert states */
    if (s_cur.mouth_smile < -0.2f && s_cur.happiness < 0.1f) {
        for (int side = -1; side <= 1; side += 2) {
            float bx = FACE_CX + side * sp + gx;
            float by = ey - br * 1.15f + gy;
            draw_qbezier(draw_ctx,
                bx - br * 0.5f * side, by - 6,
                bx, by + 3,
                bx + br * 0.4f * side, by + 7,
                col, (lv_opa_t)(b * 0.45f * 255), 3);
        }
    }
}

/* ══════════════════════════════════════════════════
 * Draw: Mouth
 * ══════════════════════════════════════════════════ */

static void draw_mouth(lv_draw_ctx_t *draw_ctx)
{
    float b = s_cur.brightness;
    if (b < 0.02f) return;

    float mY = FACE_CY + FACE_MOUTH_Y_OFF;
    float mW = FACE_MOUTH_BASE_W * s_cur.mouth_width;
    float sm = s_cur.mouth_smile;
    float op = s_cur.mouth_open;
    float hap = s_cur.happiness;
    lv_color_t col = color_from_hex(s_cur.primary_color);

    /* Talking auto-animation */
    if (s_cur.talking) {
        op = 0.15f + 0.45f * fabsf(sinf(s_frame * 0.14f));
    }

    if (op > 0.05f) {
        /* Open mouth — upper lip and lower lip as two bezier curves */

        /* Upper lip */
        draw_qbezier(draw_ctx,
            FACE_CX - mW, mY,
            FACE_CX, mY - sm * 22 - op * 10,
            FACE_CX + mW, mY,
            col, (lv_opa_t)(b * 0.8f * 255), 4);

        /* Lower lip */
        draw_qbezier(draw_ctx,
            FACE_CX - mW * 0.85f, mY + 2,
            FACE_CX, mY + op * 30 + sm * -6,
            FACE_CX + mW * 0.85f, mY + 2,
            col, (lv_opa_t)(b * 0.5f * 255), 3);

        /* Dark mouth interior */
        float mH = 5.0f > 16 * op * s_cur.mouth_width ? 5.0f : 16 * op * s_cur.mouth_width;
        draw_filled_ellipse(draw_ctx,
            FACE_CX, (lv_coord_t)(mY + sm * -12),
            (lv_coord_t)(mW * 0.55f), (lv_coord_t)mH,
            col, (lv_opa_t)(b * 0.18f * 255));
    } else {
        /* Closed mouth — single smile curve */
        float depth = sm * -26.0f;
        draw_qbezier(draw_ctx,
            FACE_CX - mW, mY,
            FACE_CX, mY + depth,
            FACE_CX + mW, mY,
            col, (lv_opa_t)(b * 0.8f * 255), 4);

        /* Cute upward flicks at corners for big smiles */
        if (sm > 0.6f) {
            for (int side = -1; side <= 1; side += 2) {
                draw_qbezier(draw_ctx,
                    FACE_CX + side * mW, mY,
                    FACE_CX + side * (mW + 10), mY - 8,
                    FACE_CX + side * (mW + 6), mY - 14,
                    col, (lv_opa_t)(b * 0.5f * 255), 3);
            }
        }
    }

    /* Rosy cheeks for happy / smiling states */
    if (sm > 0.3f || hap > 0.3f) {
        float ch_alpha = (sm > hap ? sm : hap) * 0.3f;
        lv_color_t pink = lv_color_make(251, 181, 197); /* DEBI_COLOR_PINK */
        for (int side = -1; side <= 1; side += 2) {
            draw_filled_circle(draw_ctx,
                (lv_coord_t)(FACE_CX + side * (mW + 20)),
                (lv_coord_t)(mY - 10),
                20,
                pink, (lv_opa_t)(b * ch_alpha * 255));
        }
    }
}

/* ══════════════════════════════════════════════════
 * Draw: Alert flash overlay
 * ══════════════════════════════════════════════════ */

static void draw_flash_overlay(lv_draw_ctx_t *draw_ctx)
{
    if (!s_cur.flash) return;

    float alpha = 0.12f + 0.18f * sinf(s_frame * 0.12f);
    lv_draw_rect_dsc_t dsc;
    lv_draw_rect_dsc_init(&dsc);
    dsc.bg_color = lv_color_make(239, 68, 68);
    dsc.bg_opa = (lv_opa_t)(alpha * 255);
    dsc.radius = 0;
    dsc.border_width = 0;

    lv_area_t area = {0, 0, DEBI_DISPLAY_WIDTH - 1, DEBI_DISPLAY_HEIGHT - 1};
    lv_draw_rect(draw_ctx, &dsc, &area);
}

/* ══════════════════════════════════════════════════
 * Main draw callback — called by LVGL on invalidation
 * ══════════════════════════════════════════════════ */

static void face_draw_cb(lv_event_t *e)
{
    lv_draw_ctx_t *draw_ctx = lv_event_get_draw_ctx(e);

    /* Dark background */
    {
        lv_draw_rect_dsc_t bg;
        lv_draw_rect_dsc_init(&bg);
        bg.bg_color = lv_color_make(6, 9, 18);
        bg.bg_opa = (lv_opa_t)(s_cur.brightness * 255);
        bg.radius = LV_RADIUS_CIRCLE;
        bg.border_width = 0;

        lv_area_t area = {0, 0, DEBI_DISPLAY_WIDTH - 1, DEBI_DISPLAY_HEIGHT - 1};
        lv_draw_rect(draw_ctx, &bg, &area);
    }

    /* Flash overlay (under face) */
    draw_flash_overlay(draw_ctx);

    /* Glow ring */
    draw_glow_ring(draw_ctx);

    /* Eyes */
    draw_eye(draw_ctx, -1);
    draw_eye(draw_ctx, +1);
    draw_eyebrows(draw_ctx);

    /* Mouth */
    draw_mouth(draw_ctx);
}

/* ══════════════════════════════════════════════════
 * Timer tick — drives animation + triggers redraw
 * ══════════════════════════════════════════════════ */

static void face_tick_cb(lv_timer_t *timer)
{
    (void)timer;
    s_frame++;
    face_animate_step();

    /* Invalidate the face object to trigger redraw */
    if (s_face_obj && !lv_obj_has_flag(s_face_obj, LV_OBJ_FLAG_HIDDEN)) {
        lv_obj_invalidate(s_face_obj);
    }
}

/* ══════════════════════════════════════════════════
 * Public API
 * ══════════════════════════════════════════════════ */

/* Default idle state */
static const face_params_t DEFAULT_PARAMS = {
    .eye_openness   = 0.75f,
    .eye_size       = 1.0f,
    .pupil_size     = 0.85f,
    .gaze           = {0, 0},
    .mouth_smile    = 0.4f,
    .mouth_open     = 0.0f,
    .mouth_width    = 1.05f,
    .happiness      = 0.3f,
    .brightness     = 1.0f,
    .primary_color  = 0x0D9488, /* DEBI_COLOR_TEAL */
    .glow_intensity = 0.45f,
    .pulse          = false,
    .pulse_speed    = 1.0f,
    .flash          = false,
    .talking        = false,
    .sparkle        = false,
};

void ui_face_init(lv_obj_t *parent)
{
    ESP_LOGI(TAG, "Initialising face engine (display %dx%d)",
             DEBI_DISPLAY_WIDTH, DEBI_DISPLAY_HEIGHT);

    /* Set initial state */
    s_cur = DEFAULT_PARAMS;
    s_tgt = DEFAULT_PARAMS;
    s_trans_progress = 1.0f;
    s_frame = 0;

    /* Create full-screen draw object */
    s_face_obj = lv_obj_create(parent);
    lv_obj_set_size(s_face_obj, DEBI_DISPLAY_WIDTH, DEBI_DISPLAY_HEIGHT);
    lv_obj_set_pos(s_face_obj, 0, 0);
    lv_obj_set_style_bg_opa(s_face_obj, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(s_face_obj, 0, 0);
    lv_obj_set_style_pad_all(s_face_obj, 0, 0);
    lv_obj_clear_flag(s_face_obj, LV_OBJ_FLAG_SCROLLABLE);

    /* Hook draw callback */
    lv_obj_add_event_cb(s_face_obj, face_draw_cb, LV_EVENT_DRAW_MAIN_END, NULL);

    /* Start animation timer at ~30 FPS */
    s_tick_timer = lv_timer_create(face_tick_cb, 33, NULL);

    ESP_LOGI(TAG, "Face engine ready");
}

void ui_face_deinit(void)
{
    if (s_tick_timer) {
        lv_timer_del(s_tick_timer);
        s_tick_timer = NULL;
    }
    if (s_face_obj) {
        lv_obj_del(s_face_obj);
        s_face_obj = NULL;
    }
    ESP_LOGI(TAG, "Face engine destroyed");
}

void ui_face_set_params(const face_params_t *params, uint32_t transition_ms)
{
    if (!params) return;

    if (transition_ms == 0) {
        /* Instant snap */
        s_cur = *params;
        s_tgt = *params;
        s_trans_progress = 1.0f;
    } else {
        s_tgt = *params;
        s_trans_progress = 0.0f;
        /* Convert ms to speed: at 30fps, 400ms = 12 ticks → speed ≈ 0.083 */
        float ticks = (float)transition_ms / 33.0f;
        s_trans_speed = ticks > 0.0f ? (1.0f / ticks) : 1.0f;
    }
}

const face_params_t *ui_face_get_params(void)
{
    return &s_cur;
}

void ui_face_blink(void)
{
    s_blink_phase = BLINK_HALF_TICKS * 2;
}

void ui_face_look(float x, float y)
{
    s_tgt.gaze.x = clampf(x, -15.0f, 15.0f);
    s_tgt.gaze.y = clampf(y, -10.0f, 10.0f);
}

void ui_face_show(bool visible)
{
    if (!s_face_obj) return;
    if (visible) {
        lv_obj_clear_flag(s_face_obj, LV_OBJ_FLAG_HIDDEN);
    } else {
        lv_obj_add_flag(s_face_obj, LV_OBJ_FLAG_HIDDEN);
    }
}

bool ui_face_is_visible(void)
{
    if (!s_face_obj) return false;
    return !lv_obj_has_flag(s_face_obj, LV_OBJ_FLAG_HIDDEN);
}
