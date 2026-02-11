/**
 * @file debi_face_bridge.c
 * @brief Sensor -> Face State Wiring Module
 *
 * Listens to AI camera inference events and maps detections to
 * Debi face states.  Uses an esp_timer for idle/concerned timeouts.
 *
 * Detection mapping:
 *   - person (target 0 on LOCAL_PERSON model) -> FACE_STATE_PRESENCE
 *   - pet    (local task id 1 / PET)          -> FACE_STATE_HAPPY
 *   - no detections for IDLE_TIMEOUT          -> FACE_STATE_IDLE
 *   - person present > CONCERNED_TIMEOUT      -> FACE_STATE_CONCERNED
 *
 * Copyright (c) 2026 Debi Guardian
 */

#include "debi_face_bridge.h"

#include <string.h>
#include <time.h>

#include "esp_log.h"
#include "esp_event.h"
#include "esp_timer.h"

#include "event_loops.h"
#include "data_defs.h"
#include "view/ui_face.h"
#include "view/ui_face_states.h"
#include "task_flow_module/tf_module_ai_camera.h"
#include "task_flow_module/common/tf_module_data_type.h"

static const char *TAG = "debi_face_bridge";

/* ── Internal state ── */
typedef struct {
    bool             active;              /* bridge is running              */
    bool             overridden;          /* external override in effect    */
    face_state_t     current_state;       /* last state we set              */
    time_t           last_detection_time; /* any object seen                */
    time_t           person_first_seen;   /* start of current person streak */
    bool             person_present;      /* person in view right now       */
    esp_timer_handle_t timer_handle;      /* periodic check timer           */
} bridge_state_t;

static bridge_state_t s_bridge = {
    .active            = false,
    .overridden        = false,
    .current_state     = FACE_STATE_IDLE,
    .last_detection_time = 0,
    .person_first_seen = 0,
    .person_present    = false,
    .timer_handle      = NULL,
};

/* ── Forward declarations ── */
static void bridge_set_face(face_state_t state);
static void on_ai_camera_preview(void *handler_arg, esp_event_base_t base,
                                  int32_t id, void *event_data);
static void on_local_detection(void *handler_arg, esp_event_base_t base,
                                int32_t id, void *event_data);
static void timeout_check_cb(void *arg);

/* ────────────────────────────────────────────────────
 *  Public API
 * ──────────────────────────────────────────────────── */

void debi_face_bridge_init(void)
{
    if (s_bridge.active) {
        ESP_LOGW(TAG, "already initialised");
        return;
    }

    ESP_LOGI(TAG, "init  idle=%ds  concerned=%ds  min_score=%d",
             DEBI_BRIDGE_IDLE_TIMEOUT_S,
             DEBI_BRIDGE_CONCERNED_TIMEOUT_S,
             DEBI_BRIDGE_MIN_SCORE);

    s_bridge.last_detection_time = time(NULL);
    s_bridge.person_first_seen  = 0;
    s_bridge.person_present     = false;
    s_bridge.overridden         = false;
    s_bridge.current_state      = FACE_STATE_IDLE;

    /* Register for AI camera preview events (inference results) */
    ESP_ERROR_CHECK(esp_event_handler_register_with(
        app_event_loop_handle,
        VIEW_EVENT_BASE,
        VIEW_EVENT_AI_CAMERA_PREVIEW,
        on_ai_camera_preview,
        NULL));

    /* Register for local detection events (person / pet / gesture) */
    ESP_ERROR_CHECK(esp_event_handler_register_with(
        app_event_loop_handle,
        VIEW_EVENT_BASE,
        VIEW_EVENT_TASK_FLOW_START_BY_LOCAL,
        on_local_detection,
        NULL));

    /* Create periodic timer for idle / concerned checks (every 5 s) */
    const esp_timer_create_args_t timer_args = {
        .callback = timeout_check_cb,
        .arg      = NULL,
        .name     = "debi_bridge_timer",
    };
    ESP_ERROR_CHECK(esp_timer_create(&timer_args, &s_bridge.timer_handle));
    ESP_ERROR_CHECK(esp_timer_start_periodic(s_bridge.timer_handle,
                                              5 * 1000 * 1000)); /* 5 s */

    s_bridge.active = true;
    ESP_LOGI(TAG, "face bridge active");
}

void debi_face_bridge_deinit(void)
{
    if (!s_bridge.active) return;

    esp_event_handler_unregister_with(app_event_loop_handle,
        VIEW_EVENT_BASE, VIEW_EVENT_AI_CAMERA_PREVIEW,
        on_ai_camera_preview);

    esp_event_handler_unregister_with(app_event_loop_handle,
        VIEW_EVENT_BASE, VIEW_EVENT_TASK_FLOW_START_BY_LOCAL,
        on_local_detection);

    if (s_bridge.timer_handle) {
        esp_timer_stop(s_bridge.timer_handle);
        esp_timer_delete(s_bridge.timer_handle);
        s_bridge.timer_handle = NULL;
    }

    s_bridge.active = false;
    ESP_LOGI(TAG, "face bridge stopped");
}

void debi_face_bridge_override(int state)
{
    if (state < 0 || state >= FACE_STATE_COUNT) return;
    s_bridge.overridden = true;
    bridge_set_face((face_state_t)state);
    ESP_LOGI(TAG, "override -> %s", ui_face_state_name((face_state_t)state));
}

bool debi_face_bridge_is_active(void)
{
    return s_bridge.active && !s_bridge.overridden;
}

/* ────────────────────────────────────────────────────
 *  Internal helpers
 * ──────────────────────────────────────────────────── */

/**
 * Set face state if it differs from current.
 */
static void bridge_set_face(face_state_t state)
{
    if (state == s_bridge.current_state) return;

    ESP_LOGI(TAG, "face: %s -> %s",
             ui_face_state_name(s_bridge.current_state),
             ui_face_state_name(state));

    s_bridge.current_state = state;
    ui_face_set_state(state);
}

/**
 * Process inference boxes from AI camera preview.
 *
 * The preview_info contains a tf_data_inference_info with
 * INFERENCE_TYPE_BOX data (array of sscma_client_box_t).
 * Each box has a `target` field (class id) and `score`.
 *
 * We scan boxes for the highest-priority detection:
 *   person (target 0) -> PRESENCE
 *   other targets      -> treat as pet/object -> HAPPY
 */
static void process_inference(const struct tf_data_inference_info *info)
{
    if (!info || !info->is_valid) return;
    if (info->type != INFERENCE_TYPE_BOX) return;

    const sscma_client_box_t *boxes = (const sscma_client_box_t *)info->p_data;
    uint32_t count = info->cnt;

    if (!boxes || count == 0) return;

    bool saw_person = false;
    bool saw_pet    = false;
    time_t now      = time(NULL);

    for (uint32_t i = 0; i < count; i++) {
        if (boxes[i].score < DEBI_BRIDGE_MIN_SCORE) continue;

        s_bridge.last_detection_time = now;

        /*
         * Target ID mapping depends on the loaded model.
         * For the default person detection model:
         *   target 0 = person
         * For other COCO-like models:
         *   target 0 = person,  target 14 = bird,
         *   target 15 = cat,    target 16 = dog
         *
         * We check class names if available, else use target id.
         */
        uint8_t tid = boxes[i].target;

        /* Check class name string if available */
        if (tid < CONFIG_MODEL_CLASSES_MAX_NUM &&
            info->classes[tid] != NULL) {
            const char *cls = info->classes[tid];
            if (strcmp(cls, "person") == 0 || strcmp(cls, "human") == 0) {
                saw_person = true;
            } else if (strcmp(cls, "cat") == 0 || strcmp(cls, "dog") == 0 ||
                       strcmp(cls, "bird") == 0 || strcmp(cls, "pet") == 0) {
                saw_pet = true;
            }
        } else {
            /* Fallback: target 0 is person on default model */
            if (tid == 0) {
                saw_person = true;
            } else {
                saw_pet = true;
            }
        }
    }

    /* Clear override on new inference */
    s_bridge.overridden = false;

    /* Priority: person > pet > (keep current) */
    if (saw_person) {
        if (!s_bridge.person_present) {
            s_bridge.person_first_seen = now;
            s_bridge.person_present    = true;
            ESP_LOGI(TAG, "person entered view");
        }
        bridge_set_face(FACE_STATE_PRESENCE);
    } else if (saw_pet) {
        s_bridge.person_present = false;
        bridge_set_face(FACE_STATE_HAPPY);
    }
    /* If no high-confidence boxes, let the timer handle idle transition */
}

/* ────────────────────────────────────────────────────
 *  Event handlers
 * ──────────────────────────────────────────────────── */

/**
 * AI camera preview event — carries inference results from the
 * Himax camera running an object detection model.
 */
static void on_ai_camera_preview(void *handler_arg, esp_event_base_t base,
                                  int32_t id, void *event_data)
{
    if (!s_bridge.active) return;

    const struct tf_module_ai_camera_preview_info *preview =
        (const struct tf_module_ai_camera_preview_info *)event_data;
    if (!preview) return;

    process_inference(&preview->inference);
}

/**
 * Local detection event — fired when the on-device model detects
 * a gesture (0), pet (1), or human (2) without cloud inference.
 */
static void on_local_detection(void *handler_arg, esp_event_base_t base,
                                int32_t id, void *event_data)
{
    if (!s_bridge.active) return;
    if (!event_data) return;

    uint32_t local_task_id = *(uint32_t *)event_data;
    time_t now = time(NULL);

    s_bridge.last_detection_time = now;
    s_bridge.overridden = false;

    ESP_LOGI(TAG, "local detection: %lu (%s)",
             (unsigned long)local_task_id,
             local_task_id == 0 ? "gesture" :
             local_task_id == 1 ? "pet" :
             local_task_id == 2 ? "human" : "unknown");

    switch (local_task_id) {
        case 2: /* HUMAN */
            if (!s_bridge.person_present) {
                s_bridge.person_first_seen = now;
                s_bridge.person_present    = true;
            }
            bridge_set_face(FACE_STATE_PRESENCE);
            break;

        case 1: /* PET */
            s_bridge.person_present = false;
            bridge_set_face(FACE_STATE_HAPPY);
            break;

        case 0: /* GESTURE */
            /* Gesture means someone is interacting — treat as presence */
            if (!s_bridge.person_present) {
                s_bridge.person_first_seen = now;
                s_bridge.person_present    = true;
            }
            bridge_set_face(FACE_STATE_PRESENCE);
            break;

        default:
            break;
    }
}

/**
 * Periodic timer callback — checks for idle and concerned states.
 *
 * Runs every 5 seconds.  If overridden, does nothing.
 */
static void timeout_check_cb(void *arg)
{
    if (!s_bridge.active || s_bridge.overridden) return;

    time_t now  = time(NULL);
    double idle_elapsed = difftime(now, s_bridge.last_detection_time);

    /* ── No detection timeout → IDLE ── */
    if (idle_elapsed >= DEBI_BRIDGE_IDLE_TIMEOUT_S) {
        s_bridge.person_present = false;
        bridge_set_face(FACE_STATE_IDLE);
        return;
    }

    /* ── Prolonged person presence → CONCERNED ── */
    if (s_bridge.person_present && s_bridge.person_first_seen > 0) {
        double person_elapsed = difftime(now, s_bridge.person_first_seen);
        if (person_elapsed >= DEBI_BRIDGE_CONCERNED_TIMEOUT_S) {
            ESP_LOGW(TAG, "person still for %.0f s — CONCERNED",
                     person_elapsed);
            bridge_set_face(FACE_STATE_CONCERNED);
        }
    }
}
