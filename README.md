# Thumb35 — PlatformIO (Arduino + TinyUSB) for Waveshare RP2040-Zero

Matrix: 4 rows x 10 columns (35 keys used), diodes **COL -> ROW** (anode at column).

**Pin map (breadboard-only):**
- Rows (R0..R3): GP0, GP1, GP2, GP3
- Columns (C0..C9): GP4, GP5, GP6, GP7, GP8, GP14, GP15, GP26, GP27, GP28
- Skip GP16 (onboard WS2812). GP29 left spare.

**Bottom row:** only columns C2..C6 populated (Space, Fn, Enter, Backspace, Alt).

## Build

1) Open this folder in PlatformIO (VS Code).
2) Put RP2040-Zero into UF2 bootloader (BOOT button while plugging in).
3) Build: `pio run`
4) The UF2 will be at `.pio/build/rpipico/firmware.uf2`. Drag it to the `RPI-RP2` drive.

If `upload` doesn't auto-copy for your OS, manual drag is the most reliable.
# micro-keyboard
