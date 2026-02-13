/**
 * @file   ui_face_states.h
 * @brief  Debi Face State Machine v2 — Expression Presets
 *
 * Copyright (c) 2026 Debi Guardian
 */
#ifndef UI_FACE_STATES_H
#define UI_FACE_STATES_H

#include "ui_face.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    FACE_STATE_IDLE = 0,
    FACE_STATE_PRESENCE,
    FACE_STATE_HAPPY,
    FACE_STATE_LOVE,          /* NEW: heart eyes + floating hearts */
    FACE_STATE_LISTENING,
    FACE_STATE_CONCERNED,
    FACE_STATE_ALERT_FALL,
    FACE_STATE_ALERT_STILL,
    FACE_STATE_ALERT_BABY,
    FACE_STATE_ALERT_HEART,
    FACE_STATE_NIGHT,
    FACE_STATE_TALKING,
    FACE_STATE_BOOT,
    FACE_STATE_SETUP,
    FACE_STATE_ERROR,
    FACE_STATE_COUNT,
} face_state_t;

void         ui_face_states_init(void);
void         ui_face_set_state(face_state_t state);
face_state_t ui_face_get_state(void);
const char  *ui_face_state_name(face_state_t state);

#ifdef __cplusplus
}
#endif

#endif /* UI_FACE_STATES_H */
