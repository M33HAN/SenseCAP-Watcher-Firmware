/**
 * @file debi_camera.h
 * @brief Debi Camera — Frame streaming to hub via MQTT
 *
 * Forwards JPEG frames from WiseEye2 AI camera to the Pi hub
 * for Coral TPU pose estimation and fall detection.
 *
 * Copyright (c) 2026 Debi Guardian
 */
#pragma once

#include "esp_err.h"
#include "tf_module_ai_camera.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Forward a camera preview frame to the hub via MQTT.
 * Called from debi_face_bridge when a preview event arrives.
 *
 * @param preview  The AI camera preview info (image + inference)
 */
void debi_camera_forward_frame(const struct tf_module_ai_camera_preview_info *preview);

#ifdef __cplusplus
}
#endif
