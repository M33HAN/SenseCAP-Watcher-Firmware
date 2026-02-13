# CLAUDE.md — Debi Watcher Firmware

**Last updated: 2026-02-13**
This file lives in the repo so any Claude instance working on the firmware has full context.

## What Is This Repo?

A fork of Seeed-Studio/SenseCAP-Watcher-Firmware — the ESP-IDF SDK for the SenseCAP AI Watcher device. We've added 4 custom modules that turn the Watcher into the Debi Guardian companion device.

The Watcher connects to a Raspberry Pi hub (debi-hub.local) via MQTT. The hub repo is at M33HAN/debi-guardian (private) — read its CLAUDE.md for the full system context.

**Quick Links**
- Firmware source: `examples/factory_firmware/main/`
- Debi modules: `examples/factory_firmware/main/app/debi_*.c/h`
- Face engine: `examples/factory_firmware/main/view/ui_face.c/h` and `ui_face_states.c/h`
- Build: `cd examples/factory_firmware && idf.py build`
- Flash: Must flash from the Pi (macOS USB-CDC is broken for this device) — `idf.py --port /dev/ttyACM1 flash`
- Hub MQTT: `mqtt://192.168.0.182:1883`

## The 4 Debi Modules (all in main/app/)

### debi_os.c/h — Core State Machine & Hub Connection
**Status: COMPLETE — fully implemented, all fixes applied**
- Operating modes: BOOT -> CONNECTING -> ACTIVE -> NIGHT -> ALERT -> SETUP -> ERROR
- Owns the MQTT client, connects to mqtt://192.168.0.182:1883
- MQTT deferred to WiFi — starts only after on_wifi_connected() fires
- Heartbeat every 30s with night mode auto-switch check
- Sensor reporting every 60s (SHT4x temp/humidity, SCD4x CO2)
- Detection relay (person/pet/gesture -> JSON to hub)

### debi_comms.c/h — Communications Layer
**Status: COMPLETE — fully implemented**
- Ring-buffer outbound queue (16 msgs, survives disconnects)
- Mutex-protected, thread-safe publish
- Full command dispatch: set_mode, mute, set_volume, play_sound, stop_sound, report_sensors, ping, get_health, reboot
- Config sync from hub: idle_timeout, concerned_timeout, volume, mute, night_auto, night hours
- Must be initialized BEFORE debi_os_init()

### debi_face_bridge.c/h — Sensor -> Face State
**Status: COMPLETE — fully implemented**
- Maps: person -> PRESENCE, pet -> HAPPY, no detection -> IDLE, prolonged person -> CONCERNED
- Configurable timeouts (idle=120s, concerned=1800s, min_score=50)

### debi_voice.c/h — Audio Alerts
**Status: COMPLETE — fully implemented**
- One-shot chimes for presence/happy, repeating alarms for concerned/alert
- Night mode suppresses non-critical audio

## Face Engine v2 — DO NOT MODIFY

**The face files (ui_face.c/h, ui_face_states.c/h) are DESIGNED, APPROVED AND FLASHED.**
**DO NOT change them without explicit user approval.**

Design specs:
- Squared rounded-rect eyes with white highlight squares and dark pupils
- Cyan Classic theme (#38BDF8 primary on #0B1623 background)
- 7 expressions: Idle, Happy, Love, Talk, Worried, Alert, Sleep
- Love: big heart eyes + 10 floating heart bubbles that rise and pop
- Alert: police-style red/blue alternating flash + "ALERT!" text + glow ring
- Auto-blink, gaze wander, smooth lerp transitions
- 15 face state presets covering all Debi operating states
- Uses header constants: FACE_EYE_W=80, FACE_EYE_H=86, FACE_EYE_SPACING=82, FACE_EYE_Y_OFF=-30

## Session Log — 2026-02-13

### Commits (chronological)
1. `379e1e5` — Add debi_voice: audio alerts on face state transitions
`a2cf2ed` 2. Add debi_comms: communications layer with queuing & command dispatch 
3. `62ef864` — Face engine v2: squared-eye bold style with police alerts & love hearts
4. `4434384` — Fix ui_face.c: remove duplicate functions, add missing draw_squared_eye/crescent/worried, fix gaze struct access
5. `7450ea9` — Fix face sizing: draw_rounded_rect x/y/w/h params, use header constants for all eye dimensions
6. `6b94e6f` — Apply CLAUDE.md fixes: init order, remove duplicate cmd handler, defer MQTT to WiFi, night mode auto-switch

### Fixes Applied (commit 6b94e6f)
- Fix 1: Init order in main.c — debi_comms_init() first, debi_os_init() last
- Fix 2A: Removed duplicate handle_hub_command() entirely
- Fix 2B: MQTT deferred — mqtt_connect() moved from debi_os_init() to on_wifi_connected()
- Fix 2C: Night mode auto-switch added to heartbeat_cb()
- Fix 3: Comment in debi_comms.h corrected
- All Priority 1-4 API references verified against actual headers — all compile clean

### Pi Hub Status
- 9 Debi systemd services + Zigbee2MQTT (10 total)
- Mosquitto MQTT on port 1883, Hub API on port 8422
- 8 devices online, 18 rooms, 7 Zigbee sensors (Aqara)
- Zigbee2MQTT uses /dev/ttyUSB0 (CP210x Ember), LD2410C radar on /dev/ttyAMA0
- WiFi radar/sense bug: orphaned `iw scan` processes accumulate — kill with `sudo killall iw`
- setup_complete: false — web UI setup wizard pending
- Demo sensors (debi-demo.service) generate simulated daily routines
- People: christopher (male, home)

## Build & Flash

```bash
# On Pi — build
cd ~/SenseCAP-Watcher-Firmware/examples/factory_firmware
idf.py build

# On Pi — flash (port is ACM1, not ACM0)
idf.py --port /dev/ttyACM1 flash

# If flash fails, stop zigbee2mqtt first
sudo systemctl stop zigbee2mqtt
idf.py --port /dev/ttyACM1 flash
sudo systemctl start zigbee2mqtt

# Serial monitor
idf.py --port /dev/ttyACM0 monitor  # Ctrl+] to exit

# Check MQTT connectivity
mosquitto_sub -h localhost -u debi -P debi_hub_2024 -t "debi/watcher/#" -v
```

## Hardware

- SenseCAP Watcher (white, W1-B): ESP32-S3 + Himax WiseEye2 HX6538
- 466x466 round display (SPD2010), LVGL 8.4
- 120 deg camera, mic, speaker, scroll wheel
- Two USB-C: bottom (data+power), back (power only)
- MAC: e8:06:90:9e:9e:84
- ESP-IDF v5.2.1 toolchain

## Important Warnings

- **macOS USB-CDC is BROKEN** — always flash from the Pi
- Flash port is **/dev/ttyACM1** (not ACM0)
- **DO NOT modify the face engine files** without user approval
- WiFi credentials may need re-provisioning after flash if NVS was wiped
- Test builds before flashing — this device monitors a real person's safety
