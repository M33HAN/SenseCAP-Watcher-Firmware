/**
 * @file debi_face_bridge.h
 * @brief Sensor → Face State Wiring Module
 *
 * Subscribes to AI camera inference events on the app event loop
 * and maps detected objects to Debi face states:
 *
 *   person detected    → FACE_STATE_PRESENCE
 *   pet detected       → FACE_STATE_HAPPY
 *   no motion timeout  → FACE_STATE_IDLE
 *   prolonged stillness→ FACE_STATE_CONCERNED
 *
 * Copyright (c) 2026 Debi Guardian
 */

#ifndef DEBI_FACE_BRIDGE_H
#define DEBI_FACE_BRIDGE_H

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ── Configurable timeouts (seconds) ── */
#ifndef DEBI_BRIDGE_IDLE_TIMEOUT_S
#define DEBI_BRIDGE_IDLE_TIMEOUT_S      120   /* 2 min no detection → IDLE */
#endif

#ifndef DEBI_BRIDGE_CONCERNED_TIMEOUT_S
#define DEBI_BRIDGE_CONCERNED_TIMEOUT_S 1800  /* 30 min person still → CONCERNED */
#endif

#ifndef DEBI_BRIDGE_MIN_SCORE
#define DEBI_BRIDGE_MIN_SCORE           50    /* Ignore low-confidence boxes */
#endif

/**
 * @brief Initialise the face bridge.
 *
 * Registers event handlers on `app_event_loop_handle` for:
 *   - VIEW_EVENT_AI_CAMERA_PREVIEW  (inference results)
 *   - VIEW_EVENT_TASK_FLOW_START_BY_LOCAL (local detections)
 * Creates a periodic timer for idle / concerned timeout checks.
 *
 * Call once from app_init() after app_taskflow_init().
 */
void debi_face_bridge_init(void);

/**
 * @brief Tear down the face bridge (unregister handlers, stop timer).
 */
void debi_face_bridge_deinit(void);

/**
 * @brief Force a particular face state from outside the bridge.
 *
 * Useful when other subsystems (voice, alerts) need to override
 * the sensor-driven state temporarily.  The bridge resumes
 * automatic control after the next inference event.
 */
void debi_face_bridge_override(int state);

/**
 * @brief Check whether the bridge is currently driving the face.
 */
bool debi_face_bridge_is_active(void);

#ifdef __cplusplus
}
#endif
#endif /* DEBI_FACE_BRIDGE_H */
