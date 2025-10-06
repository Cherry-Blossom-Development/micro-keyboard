# Thumb35 — PlatformIO Multi-Platform Keyboard Firmware

Matrix: 4 rows x 10 columns (35 keys used), diodes **COL -> ROW** (anode at column).

This firmware supports both RP2040 and ESP32-C3 microcontrollers with platform-specific optimizations.

## Supported Platforms

### RP2040 (Waveshare RP2040-Zero)
**Pin map (breadboard-only):**
- Rows (R0..R3): GP0, GP1, GP2, GP3
- Columns (C0..C9): GP4, GP5, GP6, GP7, GP8, GP14, GP15, GP26, GP27, GP28
- LED: GP16 (onboard WS2812). GP29 left spare.

### ESP32-C3 (ESP32-C3-DevKitM-1)
**Pin map:**
- Rows (R0..R3): GPIO0, GPIO1, GPIO2, GPIO3
- Columns (C0..C9): GPIO4, GPIO5, GPIO6, GPIO7, GPIO9, GPIO10, GPIO18, GPIO19, GPIO20, GPIO21
- LED: GPIO8 (WS2812 - adjust as needed for your board)

**Bottom row:** only columns C2..C6 populated (Space, Fn, Enter, Backspace, Alt).

## Build

### For RP2040:
1) Open this folder in PlatformIO (VS Code).
2) Put RP2040-Zero into UF2 bootloader (BOOT button while plugging in).
3) Build: `pio run -e rp2040zero`
4) The UF2 will be at `.pio/build/rp2040zero/firmware.uf2`. Drag it to the `RPI-RP2` drive.

### For ESP32-C3:
1) Open this folder in PlatformIO (VS Code).
2) Connect ESP32-C3 via USB.
3) Build and upload: `pio run -e esp32c3 --target upload`

If `upload` doesn't auto-copy for your OS, manual drag is the most reliable for RP2040.
# micro-keyboard
