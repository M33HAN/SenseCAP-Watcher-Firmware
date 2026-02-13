# Debi Firmware & Hub Architecture
## The Debi Standard — v1.0

### Vision
Debi is a home care guardian. The SenseCAP Watcher is its eyes and face.
The Raspberry Pi 5 + Coral TPU is its brain. Together they detect falls,
monitor breathing (SIDS), track daily routines, and alert carers.

---

## HARDWARE INVENTORY

### Watcher (Edge Device)
- **SoC**: ESP32-S3 @ 240MHz, 8MB PSRAM
- **AI Chip**: Himax WiseEye2 (HX6538) — runs SSCMA models locally
- **Camera**: OV5647 via WiseEye2 (not directly on ESP32)
- **Display**: 466×466 round LCD, LVGL 8.4
- **Mic**: Built-in MEMS microphone
- **Speaker**: Built-in for alerts
- **Sensors**: SHT4x (temp/humidity), SCD4x (CO2)
- **Connectivity**: WiFi, BLE
- **Storage**: 32MB flash, SPIFFS

### Pi Hub (Brain)
- **Hardware**: Raspberry Pi 5 Model B Rev 1.1
- **AI Accelerator**: Google Coral USB TPU (Global Unichip 1a6e:089a)
- **Edge TPU Library**: libedgetpu1-std 16.0 (installed)
- **Zigbee**: 2x Silicon Labs CP210x dongles
- **Camera Support**: 16 video devices available
- **Connectivity**: WiFi, Ethernet, Zigbee2MQTT

---

## FIRMWARE ARCHITECTURE — What Changes

### FILES TO REMOVE (SenseCAP/Seeed bloat)
These are factory demo, SenseCraft cloud, and redundant UI code:

```
REMOVE — main/app/:
  app_sensecraft.c/h    — SenseCraft cloud service (58KB!) → gone
  app_taskflow.c/h      — Factory task flow manager → replaced by debi_taskflow
  app_ota.c/h           — SenseCraft OTA → replaced by debi_ota (hub-managed)
  app_cmd.c/h           — AT command handler → gone (use MQTT)
  at_cmd.c/h            — AT command parser → gone
  app_voice_interaction.c/h — SenseCraft voice → replaced by debi_voice
  app_ble.c/h           — BLE provisioning for SenseCraft → simplify
  app_png.c/h           — PNG decode for SenseCraft → may keep

REMOVE — main/task_flow_module/:
  tf_module_sensecraft_alarm.c/h — SenseCraft alarm → gone
  tf_module_http_alarm.c/h      — HTTP callback alarm → gone
  tf_module_img_analyzer.c/h    — Cloud image analysis → gone

REMOVE — main/view/:
  view_pages.c/h        — Factory menu/settings pages → gone
  view_alarm.c/h        — Factory alarm display → replaced by debi face
  view_image_preview.c/h — SenseCraft preview → repurpose for debug
```

### FILES TO KEEP (Hardware essentials)
```
KEEP — main/app/:
  app_wifi.c/h          — WiFi driver (needs it)
  app_audio_player.c/h  — Audio output (speaker driver)
  app_audio_recorder.c/h — Mic input (recording driver)
  app_rgb.c/h           — RGB LED control
  app_device_info.c/h   — Device info / MAC / serial
  app_time.c/h          — NTP time sync
  sound_push2talk.c     — Audio codec (keep for mic streaming)

KEEP — main/task_flow_module/:
  tf_module_ai_camera.c/h     — SSCMA/WiseEye2 camera control (CRITICAL)
  tf_module_alarm_trigger.c/h — Local alarm trigger logic
  tf_module_local_alarm.c/h   — On-device alarm (buzzer/LED)
  tf_module_timer.c/h         — Timer module
  tf_module_debug.c/h         — Debug output
  tf_module_uart_alarm.c/h    — UART alarm (may need for serial)
  common/                     — Shared task flow utilities

KEEP — main/task_flow_engine/
  Entire directory — drives SSCMA model execution

KEEP — main/sensor/
  sensor_i2c, sensor_scd4x, sensor_sht4x — environmental sensors

KEEP — main/view/:
  ui_face.c/h           — DEBI FACE (DO NOT TOUCH)
  ui_face_states.c/h    — DEBI FACE STATES (DO NOT TOUCH)
  view.c/h              — Core LVGL view manager (keep, simplify)

KEEP — main/util/
  All utility code

KEEP — External components:
  sscma_client          — WiseEye2 protocol
  sensecap-watcher      — Hardware BSP
  esp_io_expander       — I/O expander
  esp_jpeg_simd         — JPEG encode/decode
  esp-audio-player      — Audio playback
  esp-file-iterator     — File iteration
```

### DEBI FILES — Our code (keep + expand)
```
EXISTING:
  debi_os.c/h           — Core state machine, MQTT, modes
  debi_comms.c/h        — Message queuing, config, command dispatch
  debi_face_bridge.c/h  — Sensor→face state mapping
  debi_voice.c/h        — Audio alerts on state changes

NEW TO CREATE:
  debi_camera.c/h       — Camera frame pipeline (WiseEye2→JPEG→MQTT→Hub)
  debi_audio.c/h        — Mic audio streaming (ESP32→MQTT→Hub)
  debi_taskflow.c/h     — Simplified task flow (always-on person detection)
  debi_sensors.c/h      — Environmental sensor polling (temp/humidity/CO2)
  debi_ota.c/h          — Hub-managed OTA updates
```

---

## WATCHER DATA FLOW (New Architecture)

```

                    WATCHER (ESP32-S3)                │
                                                      │
  ┌──────────┐    SSCMA     ┌──────────────┐         │
  │ WiseEye2 │──────────────│ debi_camera  │         │
  │ (Camera) │  bboxes+     │ JPEG frames  │──┐      │
  │  AI Chip │  classes     │ + detections  │  │      │
  └──────────┘              └──────────────┘  │      │
                                               │ MQTT │
  ┌──────────┐              ┌──────────────┐  │      │
   Mic    │──────────────│ debi_audio   │──┤      │  
  │  MEMS    │   PCM/opus   │ audio chunks  │  │      │
  └──────────┘              └──────────────┘  │      │
                                               │      │
  ┌──────────┐              ┌──────────────┐  │      │
  │ SHT4x   │──────────────│debi_sensors  │──┤      │
  │ SCD4x   │  temp/hum/co2│ env data      │  │      │
  └──────────┘              └──────────────┘  │      │
      │                                               
  ┌──────────────┐  ┌───────────────────┐     │      │
  │  debi_os     │  │  debi_face_bridge │     │      │
  │  state/mode  │  │  detection→face   │     │      │
  └──────┬───────┘  └────────┬──────────┘     │      │
         │                    │                │      │
  ┌──────┴────────────────────┴──────────┐    │      │
  │         Debi Face Engine v2          │    │      │
  │    (ui_face.c — DO NOT MODIFY)       │    │      │
  │    466×466 round display, LVGL       │    │      │
  └──────────────────────────────────────┘    │      │
    lv_color_t col, opa lv_opa_t) {
                                               │
                    WiFi / MQTT                │
                                               │
    lv_color_t col, opa lv_opa_t) {
                 PI HUB (Brain)               │      │
                                               │      │
  ┌──────────────┐         ┌──────────────┐   │      │
  │ MQTT Broker  │◄────────┤  debi_hub    │◄──┘      │
  │ mosquitto    │         │  (server.py) │          │
  └──────┬───────┘         └──────┬───────┘          │
         │                         │                  │
      │  
  │              debi_ai.py                    │      │
  │  ┌─────────────┐  ┌────────────────────┐  │      │
  │  │ Debi Sense  │  │  Debi Guardian     │  │      │
  │  │ sensor      │  │  FALL DETECTION    │  │      │
  │  │ fusion      │  │  pose estimation   │  │      │
  │  via Coral TPU     │  │      │  │  
  │                    └────────────────────┘  │      │
  │  ┌─────────────┐  ┌────────────────────┐  │      │
  │  │ Breathing   │  │  Routine Engine    │  │      │
  │  │ Monitor     │  │  pattern learning  │  │      │
  │  │ SIDS/apnea  │  │  anomaly detect   │  │      │
  │      │  │  └─────────────┘  
  └───────────────────────────────────────────┘      │
                                                      │
  ┌──────────────┐  ┌──────────────┐                 │
  │ Coral USB    │  │ Zigbee2MQTT  │                 │
  │ Edge TPU     │  │ door/motion/ │                 │
  │ MoveNet/     │  │ radar sensors│                 │
  │ PoseNet      │  └──────────────┘                 │
  └──────────────┘                                    │

```

---

## MQTT TOPIC STRUCTURE (Debi Standard)

```
debi/watcher/status        — Watcher online/offline, mode, face state
debi/watcher/heartbeat     — Periodic health (seq, uptime, heap, detections)
debi/watcher/detection     — AI detection events (person/pet/gesture + bbox)
debi/watcher/camera/frame  — JPEG frames (320×320, ~10KB, 1-2 FPS)
debi/watcher/audio/chunk   — Audio data (16kHz PCM, 100ms chunks)
debi/watcher/sensors       — Temp, humidity, CO2 readings
debi/watcher/command       — Hub→Watcher commands (mode, face, config)

debi/hub/alerts            — Alert events (fall, SIDS, anomaly)
debi/hub/status            — Hub system status
debi/hub/ai/result         — AI inference results from Coral
debi/zigbee/#              — Zigbee sensor data (via Zigbee2MQTT)
```

---

## PI HUB AI STACK (To Install)

```bash
# In /opt/debi/venv:
pip install numpy opencv-python-headless pillow
pip install tflite-runtime        # TFLite for Coral
pip install pycoral               # Coral Edge TPU Python API

# Models to download:
# 1. MoveNet Lightning (pose estimation) — Coral-optimised
#    → detects 17 keypoints, identifies falls from pose
# 2. Person detection (SSD MobileNet) — Coral-optimised
#    → fast person bounding boxes
# 3. Audio classifier — breathing/crying/silence
```

---

## FALL DETECTION PIPELINE

```
Watcher Camera (WiseEye2 person detect)
    
    ├─ Local: bbox + confidence → face_bridge (presence/idle/concerned)
    │
    └─ JPEG frame via MQTT → Pi Hub
                                │
                        Coral TPU: MoveNet
                                │
                        17 keypoints extracted
                                │
                        debi_ai.py Guardian:
                        ├─ Pose analysis (standing/sitting/lying)
                        ├─ Fall detection (rapid vertical change)
                        ├─ Inactivity alert (lying + no movement)
                        └─ Severity scoring
                                │
                        If fall detected:
                        ├─ MQTT → Watcher: FACE_STATE_ALERT_FALL
                        ├─ Alert to carers (push/SMS/call)
                        └─ Record event + frame evidence
```

---

## SIDS / BREATHING MONITORING PIPELINE

```
Watcher Mic (MEMS)
    │
    └─ Audio chunks via MQTT → Pi Hub
                                │
                        breathing_monitor.py:
                        ├─ Audio energy analysis
                        ├─ Breathing pattern detection
                        ├─ Crying detection
                        ├─ Silence monitoring (danger threshold)
                        └─ Movement sound analysis
                                │
                        If breathing anomaly:
                        ├─ MQTT → Watcher: FACE_STATE_ALERT_SIDS
                        ├─ Escalating alert chain
                        └─ Audio recording saved
```

---

## IMPLEMENTATION PHASES

### Phase 1: Strip & Stabilise (Current Session)
- Remove SenseCraft cloud code
- Remove factory task flow manager
- Create debi_taskflow (always-on person detection)
- Ensure face engine untouched
- Build, flash, verify face still works

### Phase 2: Camera Streaming
- Create debi_camera module
- Stream JPEG frames from WiseEye2 via MQTT to hub
- Hub receives and processes frames
- Install Coral Python stack on Pi

### Phase 3: Fall Detection
- Install MoveNet model on Coral TPU
- Implement pose estimation pipeline in debi_ai.py
- Fall detection logic with severity scoring
- Wire alerts back to Watcher face

### Phase 4: Audio Streaming & SIDS
- Create debi_audio module (mic → MQTT)
- Connect to breathing_monitor.py on hub
- Implement breathing pattern analysis
- SIDS alert chain

### Phase 5: Full Integration
- Routine engine with all sensors
- Complete alert escalation (push, SMS, call)
- Web dashboard
- Setup wizard completion
- Testing with real scenarios

---

## BUILD NOTES
- ESP-IDF toolchain NOT installed on Pi — need to install or use Docker
- Flash command: `esptool.py --port /dev/ttyACM1 --baud 460800 --chip esp32s3 write_flash ...`
- Must stop zigbee2mqtt before flash: `sudo systemctl stop zigbee2mqtt`
- Must restart after: `sudo systemctl start zigbee2mqtt`
- Face files are SACRED — never modify ui_face.c/h or ui_face_states.c/h

