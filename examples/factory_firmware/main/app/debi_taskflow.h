/**
 * @file debi_taskflow.h
 * @brief Debi Task Flow — Always-on person detection
 *
 * Replaces the complex factory task flow manager with a simple
 * always-on person detection pipeline using the WiseEye2 AI chip.
 *
 * Copyright (c) 2026 Debi Guardian
 */
#pragma once

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Initialise the Debi task flow.
 * Starts the WiseEye2 person detection model immediately.
 */
esp_err_t debi_taskflow_init(void);

/**
 * Stop the task flow (for shutdown/sleep).
 */
void debi_taskflow_deinit(void);

#ifdef __cplusplus
}
#endif
