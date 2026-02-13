/**
 * @file   ui_face.c
 * @brief  Debi Face Rendering Engine v2 - Squared-Eye Bold Style
 *
 * LVGL 8.4 custom draw callback implementation.
 * Squared rounded-rect eyes with white highlights, heart eyes,
 * crescent happy eyes, police-flash alerts, floating heart bubbles.
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
#include <string.h>

static const char *TAG = "ui_face";

static lv_obj_t  *s_face_obj  = NULL;
static lv_timer_t *s_tick_timer = NULL;
static face_params_t s_cur;
static face_params_t s_tgt;
static float    s_trans_progress = 1.0f;
static float    s_trans_speed    = 0.04f;
static int      s_blink_timer = 120;
static int      s_blink_phase = 0;
#define BLINK_HALF_TICKS 6
static int      s_wander_timer = 60;
static uint32_t s_frame = 0;

static inline float lerpf(float a, float b, float t) { return a + (b - a) * t; }
static inline float ease_in_out(float t) {
    if (t < 0.5f) return 2.0f * t * t;
    return 1.0f - powf(-2.0f * t + 2.0f, 2) / 2.0f;
}
static inline float clampf(float v, float lo, float hi) {
    return v < lo ? lo : (v > hi ? hi : v);
}
static inline uint8_t r_of(uint32_t c) { return (c >> 16) & 0xFF; }
static inline uint8_t g_of(uint32_t c) { return (c >> 8)  & 0xFF; }
static inline uint8_t b_of(uint32_t c) { return  c        & 0xFF; }
static lv_color_t color_from_hex(uint32_t hex) {
    return lv_color_make(r_of(hex), g_of(hex), b_of(hex));
}
static void qbezier(float x0, float y0, float cx, float cy,
                    float x1, float y1, float t, float *ox, float *oy) {
    float u = 1.0f - t;
    *ox = u * u * x0 + 2.0f * u * t * cx + t * t * x1;
    *oy = u * u * y0 + 2.0f * u * t * cy + t * t * y1;
}

static void face_animate_step(void) {
    if (s_trans_progress < 1.0f) {
        s_trans_progress += s_trans_speed;
        if (s_trans_progress > 1.0f) s_trans_progress = 1.0f;
        float t = ease_in_out(s_trans_progress);
        s_cur.eye_openness   = lerpf(s_cur.eye_openness,   s_tgt.eye_openness,   t);
        s_cur.eye_size       = lerpf(s_cur.eye_size,       s_tgt.eye_size,       t);
        s_cur.gaze.x         = lerpf(s_cur.gaze.x,         s_tgt.gaze.x,         t);
        s_cur.gaze.y         = lerpf(s_cur.gaze.y,         s_tgt.gaze.y,         t);
        s_cur.mouth_smile    = lerpf(s_cur.mouth_smile,    s_tgt.mouth_smile,    t);
        s_cur.mouth_open     = lerpf(s_cur.mouth_open,     s_tgt.mouth_open,     t);
        s_cur.mouth_width    = lerpf(s_cur.mouth_width,    s_tgt.mouth_width,    t);
        s_cur.happiness      = lerpf(s_cur.happiness,      s_tgt.happiness,      t);
        s_cur.brightness     = lerpf(s_cur.brightness,     s_tgt.brightness,     t);
        s_cur.glow_intensity = lerpf(s_cur.glow_intensity, s_tgt.glow_intensity, t);
    }
    s_cur.eye_style       = s_tgt.eye_style;
    s_cur.pulse           = s_tgt.pulse;
    s_cur.pulse_speed     = s_tgt.pulse_speed;
    s_cur.flash           = s_tgt.flash;
    s_cur.talking         = s_tgt.talking;
    s_cur.sparkle         = s_tgt.sparkle;
    s_cur.love_bubbles    = s_tgt.love_bubbles;
    s_cur.alert_police    = s_tgt.alert_police;
    s_cur.show_alert_text = s_tgt.show_alert_text;
    s_cur.primary_color   = s_tgt.primary_color;
    s_cur.secondary_color = s_tgt.secondary_color;
    if (s_cur.eye_style == EYE_STYLE_SQUARED || s_cur.eye_style == EYE_STYLE_WORRIED) {
        s_blink_timer--;
        if (s_blink_timer <= 0 && s_blink_phase == 0) {
            s_blink_phase = BLINK_HALF_TICKS * 2;
            s_blink_timer = 180 + (rand() % 300);
        }
    }
    if (s_blink_phase > 0) s_blink_phase--;
    s_wander_timer--;
    if (s_wander_timer <= 0) {
        s_tgt.gaze.x = ((float)(rand() % 100) / 100.0f - 0.5f) * 6.0f;
        s_tgt.gaze.y = ((float)(rand() % 100) / 100.0f - 0.5f) * 3.0f;
        s_wander_timer = 120 + (rand() % 240);
    }
}

static float blink_mul(void) {
    if (s_blink_phase <= 0) return 1.0f;
    if (s_blink_phase > BLINK_HALF_TICKS)
        return lerpf(1.0f, 0.0f, (float)(BLINK_HALF_TICKS * 2 - s_blink_phase) / BLINK_HALF_TICKS);
    else
        return lerpf(0.0f, 1.0f, (float)(BLINK_HALF_TICKS - s_blink_phase) / BLINK_HALF_TICKS);
}

static void draw_rounded_rect(lv_draw_ctx_t *dc, lv_coord_t x, lv_coord_t y,
    lv_coord_t w, lv_coord_t h, lv_coord_t r, lv_color_t c, lv_opa_t o) {
    lv_draw_rect_dsc_t d; lv_draw_rect_dsc_init(&d);
    d.bg_color = c; d.bg_opa = o; d.radius = r; d.border_width = 0;
    lv_area_t a = {x, y, x+w-1, y+h-1}; lv_draw_rect(dc, &d, &a);
}

static void draw_filled_circle(lv_draw_ctx_t *dc, lv_coord_t cx, lv_coord_t cy,
    lv_coord_t r, lv_color_t c, lv_opa_t o) {
    lv_draw_rect_dsc_t d; lv_draw_rect_dsc_init(&d);
    d.bg_color = c; d.bg_opa = o; d.radius = LV_RADIUS_CIRCLE; d.border_width = 0;
    lv_area_t a = {cx-r, cy-r, cx+r, cy+r}; lv_draw_rect(dc, &d, &a);
}

static void draw_qbezier(lv_draw_ctx_t *dc, float x0, float y0, float cpx, float cpy,
    float x1, float y1, lv_color_t c, lv_opa_t o, lv_coord_t w) {
    lv_draw_line_dsc_t d; lv_draw_line_dsc_init(&d);
    d.color = c; d.opa = o; d.width = w; d.round_start = 1; d.round_end = 1;
    float px, py, nx, ny;
    qbezier(x0,y0,cpx,cpy,x1,y1, 0, &px, &py);
    for (int i = 1; i <= FACE_CURVE_SEGS; i++) {
        qbezier(x0,y0,cpx,cpy,x1,y1, (float)i/FACE_CURVE_SEGS, &nx, &ny);
        lv_point_t p1 = {(lv_coord_t)px,(lv_coord_t)py};
        lv_point_t p2 = {(lv_coord_t)nx,(lv_coord_t)ny};
        lv_draw_line(dc, &d, &p1, &p2); px=nx; py=ny;
    }
}

static void draw_line_seg(lv_draw_ctx_t *dc, lv_coord_t x0, lv_coord_t y0,
    lv_coord_t x1, lv_coord_t y1, lv_color_t c, lv_opa_t o, lv_coord_t w) {
    lv_draw_line_dsc_t d; lv_draw_line_dsc_init(&d);
    d.color = c; d.opa = o; d.width = w; d.round_start = 1; d.round_end = 1;
    lv_point_t p1 = {x0,y0}; lv_point_t p2 = {x1,y1};
    lv_draw_line(dc, &d, &p1, &p2);
}

static void draw_heart_shape(lv_draw_ctx_t *dc, float hx, float hy, float size,
    lv_color_t col, lv_opa_t opa) {
    float r = size * 0.32f, off = size * 0.28f;
    draw_filled_circle(dc, (lv_coord_t)(hx-off), (lv_coord_t)(hy-size*0.12f),
        (lv_coord_t)r, col, opa);
    draw_filled_circle(dc, (lv_coord_t)(hx+off), (lv_coord_t)(hy-size*0.12f),
        (lv_coord_t)r, col, opa);
    draw_rounded_rect(dc, (lv_coord_t)(hx-off), (lv_coord_t)(hy-size*0.12f),
        (lv_coord_t)(off*2), (lv_coord_t)(size*0.5f), 2, col, opa);
    for (int i = 0; i < 6; i++) {
        float frac = (float)i / 6.0f;
        float w = off * 2 * (1.0f - frac);
        float y = hy + size*0.12f + size*0.5f*frac;
        if (w < 3) w = 3;
        draw_rounded_rect(dc, (lv_coord_t)(hx-w/2), (lv_coord_t)y,
            (lv_coord_t)w, (lv_coord_t)(size*0.09f+2), 1, col, opa);
    }
    draw_filled_circle(dc, (lv_coord_t)(hx-off*0.7f), (lv_coord_t)(hy-size*0.28f),
        (lv_coord_t)(r*0.45f), lv_color_white(), (lv_opa_t)(opa*45/100));
}

static void draw_heart_eyes(lv_draw_ctx_t *dc) {
    float b = s_cur.brightness, sz = s_cur.eye_size;
    float gx = s_cur.gaze.x * 2.0f, gy = s_cur.gaze.y * 1.5f;
    lv_color_t col = color_from_hex(s_cur.secondary_color);
    float pulse = 1.0f + 0.08f * sinf(s_frame * 0.08f);
    float hsz = 72.0f * sz * pulse;
    for (int side = -1; side <= 1; side += 2) {
        float ex = FACE_CX + side * FACE_EYE_SPACING * sz + gx;
        float ey = FACE_CY + FACE_EYE_Y_OFF + 4 + gy;
        draw_heart_shape(dc, ex, ey, hsz, col, (lv_opa_t)(b*245));
    }
}

static void draw_closed_eyes(lv_draw_ctx_t *dc) {
    float b = s_cur.brightness, sz = s_cur.eye_size;
    lv_color_t col = color_from_hex(s_cur.primary_color);
    for (int side = -1; side <= 1; side += 2) {
        float ex = FACE_CX + side * FACE_EYE_SPACING * sz;
        float ey = FACE_CY + FACE_EYE_Y_OFF + 6;
        draw_qbezier(dc, ex-34*sz, ey, ex, ey+18*sz, ex+34*sz, ey,
            col, (lv_opa_t)(b*200), (lv_coord_t)(7*sz));
    }
    float t = s_frame * 0.033f;
    for (int i = 0; i < 3; i++) {
        float phase = fmodf(t*0.35f + i*0.8f, 3.0f);
        if (phase > 2.5f) continue;
        float alpha = 1.0f;
        if (phase < 0.3f) alpha = phase / 0.3f;
        if (phase > 2.0f) alpha = (2.5f - phase) / 0.5f;
        if (alpha < 0.02f) continue;
        float zx = FACE_CX + 90 + phase*25;
        float zy = FACE_CY - 45 - phase*35;
        lv_coord_t zs = 10 + i*4;
        lv_opa_t za = (lv_opa_t)(alpha * b * 180);
        draw_line_seg(dc, (lv_coord_t)zx, (lv_coord_t)zy,
            (lv_coord_t)(zx+zs), (lv_coord_t)zy, col, za, 3);
        draw_line_seg(dc, (lv_coord_t)(zx+zs), (lv_coord_t)zy,
            (lv_coord_t)zx, (lv_coord_t)(zy+zs), col, za, 3);
        draw_line_seg(dc, (lv_coord_t)zx, (lv_coord_t)(zy+zs),
            (lv_coord_t)(zx+zs), (lv_coord_t)(zy+zs), col, za, 3);
    }
}

static void draw_police_eyes(lv_draw_ctx_t *dc) {
    float b = s_cur.brightness, sz = s_cur.eye_size;
    int fc = (s_frame / 6) % 2;
    uint32_t cl = fc ? DEBI_COLOR_RED : DEBI_COLOR_POLICE_BLUE;
    uint32_t cr = fc ? DEBI_COLOR_POLICE_BLUE : DEBI_COLOR_RED;
    lv_coord_t ew = (lv_coord_t)(FACE_EYE_W*sz), eh = (lv_coord_t)(FACE_EYE_H*sz);
    lv_coord_t er = (lv_coord_t)(FACE_EYE_R*sz);
    for (int side = -1; side <= 1; side += 2) {
        float ex = FACE_CX + side * FACE_EYE_SPACING * sz;
        float ey = FACE_CY + FACE_EYE_Y_OFF;
        uint32_t ec = (side < 0) ? cl : cr;
        draw_rounded_rect(dc, (lv_coord_t)(ex-ew/2), (lv_coord_t)(ey-eh/2),
            ew, eh, er, color_from_hex(ec), (lv_opa_t)(b*245));
        draw_rounded_rect(dc, (lv_coord_t)(ex-ew/2-6), (lv_coord_t)(ey-eh/2-6),
            ew+12, eh+12, er+4, color_from_hex(ec), (lv_opa_t)(b*40));
        lv_coord_t hs = (lv_coord_t)(FACE_HL_SIZE*sz);
        draw_rounded_rect(dc, (lv_coord_t)(ex-ew/2+8*sz), (lv_coord_t)(ey-eh/2+8*sz),
            hs, hs, (lv_coord_t)(FACE_HL_R*sz), lv_color_white(), (lv_opa_t)(b*200));
    }
}

static void draw_police_flash_bg(lv_draw_ctx_t *dc) {
    if (!s_cur.alert_police) return;
    float b = s_cur.brightness;
    int fc = (s_frame / 6) % 2;
    float strobe = (0.5f + 0.5f * sinf(s_frame * 0.25f)) * 0.25f;
    uint32_t fcol = fc ? DEBI_COLOR_RED : DEBI_COLOR_POLICE_BLUE;
    draw_filled_circle(dc, fc ? FACE_CX-60 : FACE_CX+60, FACE_CY-30,
        FACE_RADIUS-20, color_from_hex(fcol), (lv_opa_t)(b*strobe*255));
    lv_draw_arc_dsc_t d; lv_draw_arc_dsc_init(&d);
    d.color = color_from_hex(fcol); d.opa = (lv_opa_t)(b*0.45f*255);
    d.width = 6; d.rounded = 0;
    lv_point_t cen = {FACE_CX, FACE_CY};
    lv_draw_arc(dc, &d, &cen, FACE_RADIUS-4, 0, 360);
}

static void draw_alert_text(lv_draw_ctx_t *dc) {
    if (!s_cur.show_alert_text) return;
    float b = s_cur.brightness;
    int fc = (s_frame / 6) % 2;
    uint32_t tc = fc ? DEBI_COLOR_RED : DEBI_COLOR_POLICE_BLUE;
    lv_color_t col = color_from_hex(tc);
    lv_opa_t opa = (lv_opa_t)(b * 240);
    lv_coord_t y0 = FACE_CY + 56, lh = 28, lw = 5, sp = 22;
    lv_coord_t xs = FACE_CX - sp*3 + 4, x;
    x = xs;
    draw_line_seg(dc, x, y0+lh, x+8, y0, col, opa, lw);
    draw_line_seg(dc, x+8, y0, x+16, y0+lh, col, opa, lw);
    draw_line_seg(dc, x+4, y0+lh/2, x+12, y0+lh/2, col, opa, lw-1);
    x += sp;
    draw_line_seg(dc, x, y0, x, y0+lh, col, opa, lw);
    draw_line_seg(dc, x, y0+lh, x+14, y0+lh, col, opa, lw);
    x += sp;
    draw_line_seg(dc, x, y0, x, y0+lh, col, opa, lw);
    draw_line_seg(dc, x, y0, x+14, y0, col, opa, lw);
    draw_line_seg(dc, x, y0+lh/2, x+11, y0+lh/2, col, opa, lw-1);
    draw_line_seg(dc, x, y0+lh, x+14, y0+lh, col, opa, lw);
    x += sp;
    draw_line_seg(dc, x, y0, x, y0+lh, col, opa, lw);
    draw_qbezier(dc, x, y0, x+16, y0+2, x+4, y0+lh/2, col, opa, lw);
    draw_line_seg(dc, x+6, y0+lh/2, x+14, y0+lh, col, opa, lw);
    x += sp;
    draw_line_seg(dc, x, y0, x+16, y0, col, opa, lw);
    draw_line_seg(dc, x+8, y0, x+8, y0+lh, col, opa, lw);
    x += sp;
    draw_line_seg(dc, x+4, y0, x+4, y0+lh-10, col, opa, lw);
    draw_filled_circle(dc, x+4, y0+lh-2, 3, col, opa);
    draw_rounded_rect(dc, xs-10, y0-6, sp*6+20, lh+12, 8,
        color_from_hex(tc), (lv_opa_t)(b*30));
}

static void draw_glow_ring(lv_draw_ctx_t *dc) {
    float g = s_cur.glow_intensity * s_cur.brightness;
    if (s_cur.pulse) {
        float spd = s_cur.pulse_speed > 0 ? s_cur.pulse_speed : 1;
        g *= (0.3f+0.7f*(0.5f+0.5f*sinf(s_frame*spd*0.05f)));
    } else {
        g *= (0.6f+0.4f*(0.5f+0.5f*sinf(s_frame*0.02f)));
    }
    if (g < 0.01f) return;
    lv_color_t col = color_from_hex(s_cur.primary_color);
    for (int i = 3; i >= 0; i--) {
        lv_draw_arc_dsc_t d; lv_draw_arc_dsc_init(&d);
        d.color = col; d.opa = (lv_opa_t)(g*0.08f*(4-i)*255);
        d.width = 8+i*5; d.rounded = 0;
        lv_point_t c = {FACE_CX, FACE_CY};
        lv_draw_arc(dc, &d, &c, FACE_RADIUS-4-i*7, 0, 360);
    }
}

static void draw_squared_eye(lv_draw_ctx_t *dc, int side, float bk) {
    float b = s_cur.brightness;
    float sz = s_cur.eye_size * 40.0f;
    float openness = s_cur.eye_openness * bk;
    if (openness < 0.02f) { draw_closed_eyes(dc); return; }
    float ps = s_cur.pupil_size;
    lv_coord_t cx = FACE_CX + side * 52 + (lv_coord_t)(s_cur.gaze.x * 15);
    lv_coord_t cy = FACE_CY - 18 + (lv_coord_t)(s_cur.gaze.y * 10);
    lv_coord_t hw = (lv_coord_t)(sz * 0.55f);
    lv_coord_t hh = (lv_coord_t)(sz * 0.45f * openness);
    /* outer eye rounded rect */
    uint32_t pc = s_cur.primary_color;
    lv_color_t col = color_from_hex(pc);
    lv_opa_t opa = (lv_opa_t)(b * 255);
    draw_rounded_rect(dc, cx - hw, cy - hh, cx + hw, cy + hh, 10, col, opa);
    /* pupil - smaller inner rect */
    lv_coord_t pw = (lv_coord_t)(hw * ps * 0.6f);
    lv_coord_t ph = (lv_coord_t)(hh * ps * 0.7f);
    lv_coord_t px = cx + (lv_coord_t)(s_cur.gaze.x * 8);
    lv_coord_t py = cy + (lv_coord_t)(s_cur.gaze.y * 5);
    lv_color_t bg = color_from_hex(DEBI_COLOR_BG);
    draw_rounded_rect(dc, px - pw, py - ph, px + pw, py + ph, 6, bg, opa);
    /* white highlight square */
    lv_coord_t hs = (lv_coord_t)(sz * 0.14f);
    lv_coord_t hlx = cx - hw/2 + 2;
    lv_coord_t hly = cy - hh/2 + 2;
    draw_rounded_rect(dc, hlx, hly, hlx + hs, hly + hs, 2,
                      lv_color_white(), (lv_opa_t)(b * 200));
}

static void draw_crescent_eyes(lv_draw_ctx_t *dc) {
    float b = s_cur.brightness;
    lv_color_t col = color_from_hex(s_cur.primary_color);
    lv_opa_t opa = (lv_opa_t)(b * 255);
    for (int side = -1; side <= 1; side += 2) {
        lv_coord_t cx = FACE_CX + side * 52 + (lv_coord_t)(s_cur.gaze.x * 15);
        lv_coord_t cy = FACE_CY - 18;
        /* Draw crescent as thick arc */
        lv_draw_arc_dsc_t d; lv_draw_arc_dsc_init(&d);
        d.color = col; d.opa = opa;
        d.width = 8; d.rounded = 1;
        lv_point_t c = {cx, cy + 6};
        lv_draw_arc(dc, &d, &c, 18, 200, 340);
    }
}

static void draw_worried_eyes(lv_draw_ctx_t *dc, float bk) {
    float b = s_cur.brightness;
    float openness = s_cur.eye_openness * bk;
    if (openness < 0.05f) { draw_closed_eyes(dc); return; }
    lv_color_t col = color_from_hex(s_cur.primary_color);
    lv_opa_t opa = (lv_opa_t)(b * 255);
    for (int side = -1; side <= 1; side += 2) {
        lv_coord_t cx = FACE_CX + side * 52;
        lv_coord_t cy = FACE_CY - 18;
        lv_coord_t hw = 22, hh = (lv_coord_t)(18 * openness);
        /* Slightly tilted worried eyes */
        lv_coord_t tilt = side * 4;
        draw_rounded_rect(dc, cx - hw, cy - hh + tilt, cx + hw, cy + hh + tilt,
                          8, col, opa);
        /* Inner dark */
        lv_color_t bg = color_from_hex(DEBI_COLOR_BG);
        lv_coord_t pw = (lv_coord_t)(hw * 0.55f);
        lv_coord_t ph = (lv_coord_t)(hh * 0.65f);
        draw_rounded_rect(dc, cx - pw, cy - ph + tilt, cx + pw, cy + ph + tilt,
                          5, bg, opa);
        /* Worried brow line */
        draw_line_seg(dc, cx - hw - 4, cy - hh - 8 - side*3,
                      cx + hw + 4, cy - hh - 8 + side*3,
                      col, opa, 4);
    }
}


static void draw_love_bubbles(lv_draw_ctx_t *dc) {
    if (!s_cur.love_bubbles) return;
    float b = s_cur.brightness;
    lv_color_t col = color_from_hex(s_cur.secondary_color);
    float t = s_frame * 0.033f;
    for (int i = 0; i < FACE_MAX_HEARTS; i++) {
        float seed = i * 1.618f;
        float period = 2.5f + (i % 4) * 0.5f;
        float phase = fmodf(t / period + seed, 1.0f);
        if (phase > 0.92f) continue;
        float bx = FACE_CX + sinf(seed*5.7f)*130.0f + sinf(t*1.4f+seed*4)*20.0f;
        float by = (FACE_CY+145) + ((FACE_CY-170)-(FACE_CY+145)) * phase;
        float bs = 1.0f;
        if (phase < 0.1f) bs = phase / 0.1f;
        else if (phase > 0.7f) { float pp=(phase-0.7f)/0.22f; bs=1.0f-pp*pp; }
        float hsz = (14.0f + (i%5)*6.0f) * (bs > 0 ? bs : 0);
        if (hsz < 3) continue;
        float al = 1.0f;
        if (phase < 0.08f) al = phase / 0.08f;
        if (phase > 0.75f) { al = (0.92f-phase)/0.17f; if(al<0) al=0; }
        draw_heart_shape(dc, bx, by, hsz, col, (lv_opa_t)(al * b * 140));
    }
}

static void draw_mouth(lv_draw_ctx_t *dc) {
    float b = s_cur.brightness;
    if (b < 0.02f) return;
    float gx = s_cur.gaze.x * 1.5f;
    float sm = s_cur.mouth_smile, op = s_cur.mouth_open, mw = s_cur.mouth_width;
    lv_color_t col = color_from_hex(s_cur.primary_color);
    lv_color_t sec = color_from_hex(s_cur.secondary_color);
    float mY = FACE_CY + FACE_MOUTH_Y_OFF;
    float mW = FACE_MOUTH_BASE_W * mw;
    if (s_cur.talking) op = 0.15f + 0.45f * fabsf(sinf(s_frame * 0.14f));
    eye_style_t st = s_cur.eye_style;
    if (st == EYE_STYLE_CRESCENT) {
        float gW = mW*1.4f, gH = 42.0f*mw;
        draw_line_seg(dc, (lv_coord_t)(FACE_CX-gW+gx), (lv_coord_t)mY,
            (lv_coord_t)(FACE_CX+gW+gx), (lv_coord_t)mY, col, (lv_opa_t)(b*240), 4);
        for (int r = 0; r < 10; r++) {
            float f = (float)r/10.0f;
            float w = gW*2*(1.0f - f*f*0.6f);
            draw_rounded_rect(dc, (lv_coord_t)(FACE_CX+gx-w/2), (lv_coord_t)(mY+gH*f),
                (lv_coord_t)w, (lv_coord_t)(gH/10+2), 4, col, (lv_opa_t)(b*235));
        }
        float cs = FACE_EYE_SPACING * s_cur.eye_size;
        draw_filled_circle(dc, (lv_coord_t)(FACE_CX-cs-24), (lv_coord_t)(mY-10),
            20, sec, (lv_opa_t)(b*115));
        draw_filled_circle(dc, (lv_coord_t)(FACE_CX+cs+24), (lv_coord_t)(mY-10),
            20, sec, (lv_opa_t)(b*115));
    } else if (st == EYE_STYLE_HEART) {
        draw_qbezier(dc, FACE_CX-28+gx, mY+6, FACE_CX+gx, mY-14,
            FACE_CX+28+gx, mY+6, sec, (lv_opa_t)(b*200), 5);
        float cs = FACE_EYE_SPACING * s_cur.eye_size;
        draw_filled_circle(dc, (lv_coord_t)(FACE_CX-cs-30), (lv_coord_t)(mY-14),
            20, sec, (lv_opa_t)(b*90));
        draw_filled_circle(dc, (lv_coord_t)(FACE_CX+cs+30), (lv_coord_t)(mY-14),
            20, sec, (lv_opa_t)(b*90));
    } else if (st == EYE_STYLE_WORRIED) {
        draw_qbezier(dc, FACE_CX-mW*0.7f+gx, mY+10, FACE_CX+gx, mY+28,
            FACE_CX+mW*0.7f+gx, mY+10, col, (lv_opa_t)(b*200), 5);
    } else if (st == EYE_STYLE_CLOSED || st == EYE_STYLE_POLICE) {
        /* no mouth */
    } else if (op > 0.05f) {
        float mh = 12.0f + op*32.0f, mrw = 18.0f + op*14.0f;
        draw_rounded_rect(dc, (lv_coord_t)(FACE_CX+gx-mrw), (lv_coord_t)(mY-mh/2),
            (lv_coord_t)(mrw*2), (lv_coord_t)mh,
            (lv_coord_t)(mh/2.5f > mrw ? mrw : mh/2.5f), col, (lv_opa_t)(b*230));
    } else {
        draw_qbezier(dc, FACE_CX-mW*0.6f+gx, mY, FACE_CX+gx, mY+sm*-22.0f,
            FACE_CX+mW*0.6f+gx, mY, col, (lv_opa_t)(b*210), 5);
    }
}

static void face_draw_cb(lv_event_t *e) {
    lv_draw_ctx_t *dc = lv_event_get_draw_ctx(e);
    { lv_draw_rect_dsc_t bg; lv_draw_rect_dsc_init(&bg);
      bg.bg_color = color_from_hex(DEBI_COLOR_BG);
      bg.bg_opa = (lv_opa_t)(s_cur.brightness * 255);
      bg.radius = LV_RADIUS_CIRCLE; bg.border_width = 0;
      lv_area_t a = {0,0,DEBI_DISPLAY_WIDTH-1,DEBI_DISPLAY_HEIGHT-1};
      lv_draw_rect(dc, &bg, &a); }
    draw_police_flash_bg(dc);
    draw_glow_ring(dc);
    float bk = blink_mul();
    switch (s_cur.eye_style) {
        case EYE_STYLE_SQUARED:  draw_squared_eye(dc,-1,bk); draw_squared_eye(dc,+1,bk); break;
        case EYE_STYLE_HEART:    draw_heart_eyes(dc); break;
        case EYE_STYLE_CRESCENT: draw_crescent_eyes(dc); break;
        case EYE_STYLE_CLOSED:   draw_closed_eyes(dc); break;
        case EYE_STYLE_WORRIED:  draw_worried_eyes(dc,bk); break;
        case EYE_STYLE_POLICE:   draw_police_eyes(dc); break;
    }
    draw_mouth(dc);
    draw_love_bubbles(dc);
    draw_alert_text(dc);
}

static void face_tick_cb(lv_timer_t *timer) {
    (void)timer; s_frame++;
    face_animate_step();
    if (s_face_obj && !lv_obj_has_flag(s_face_obj, LV_OBJ_FLAG_HIDDEN))
        lv_obj_invalidate(s_face_obj);
}

static const face_params_t DEFAULT_PARAMS = {
    .eye_openness=0.75f, .eye_size=1.0f, .pupil_size=0.85f,
    .gaze={0,0}, .eye_style=EYE_STYLE_SQUARED,
    .mouth_smile=0.4f, .mouth_open=0.0f, .mouth_width=1.05f,
    .happiness=0.3f, .brightness=1.0f,
    .primary_color=DEBI_COLOR_CYAN, .secondary_color=DEBI_COLOR_PINK,
    .glow_intensity=0.45f,
    .pulse=false, .pulse_speed=1.0f, .flash=false,
    .talking=false, .sparkle=false,
    .love_bubbles=false, .alert_police=false, .show_alert_text=false,
};

void ui_face_init(lv_obj_t *parent) {
    ESP_LOGI(TAG, "Init face v2 (squared bold, %dx%d)", DEBI_DISPLAY_WIDTH, DEBI_DISPLAY_HEIGHT);
    s_cur = DEFAULT_PARAMS; s_tgt = DEFAULT_PARAMS;
    s_trans_progress = 1.0f; s_frame = 0;
    s_face_obj = lv_obj_create(parent);
    lv_obj_set_size(s_face_obj, DEBI_DISPLAY_WIDTH, DEBI_DISPLAY_HEIGHT);
    lv_obj_set_pos(s_face_obj, 0, 0);
    lv_obj_set_style_bg_opa(s_face_obj, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(s_face_obj, 0, 0);
    lv_obj_set_style_pad_all(s_face_obj, 0, 0);
    lv_obj_clear_flag(s_face_obj, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_event_cb(s_face_obj, face_draw_cb, LV_EVENT_DRAW_MAIN_END, NULL);
    s_tick_timer = lv_timer_create(face_tick_cb, 33, NULL);
    ESP_LOGI(TAG, "Face engine v2 ready");
}

void ui_face_deinit(void) {
    if (s_tick_timer) { lv_timer_del(s_tick_timer); s_tick_timer = NULL; }
    if (s_face_obj) { lv_obj_del(s_face_obj); s_face_obj = NULL; }
}

void ui_face_set_params(const face_params_t *p, uint32_t ms) {
    if (!p) return;
    if (ms == 0) { s_cur = *p; s_tgt = *p; s_trans_progress = 1.0f; }
    else { s_tgt = *p; s_trans_progress = 0.0f;
           float tk = (float)ms / 33.0f;
           s_trans_speed = tk > 0 ? (1.0f/tk) : 1.0f; }
}

const face_params_t *ui_face_get_params(void) { return &s_cur; }
void ui_face_blink(void) { s_blink_phase = BLINK_HALF_TICKS * 2; }
void ui_face_look(float x, float y) {
    s_tgt.gaze.x = clampf(x, -15, 15);
    s_tgt.gaze.y = clampf(y, -10, 10);
}
void ui_face_show(bool v) {
    if (!s_face_obj) return;
    if (v) lv_obj_clear_flag(s_face_obj, LV_OBJ_FLAG_HIDDEN);
    else   lv_obj_add_flag(s_face_obj, LV_OBJ_FLAG_HIDDEN);
}
bool ui_face_is_visible(void) {
    return s_face_obj && !lv_obj_has_flag(s_face_obj, LV_OBJ_FLAG_HIDDEN);
}

