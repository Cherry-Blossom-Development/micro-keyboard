#include <Arduino.h>
#include <Keyboard.h>
#include <NeoPixelConnect.h>

// ---------------- LED Configuration ----------------
#define LED_PIN 16  // WS2812 RGB LED on RP2040 Zero
#define NUM_PIXELS 1  // Single onboard LED
static uint32_t lastLedToggle = 0;
static bool ledState = false;

NeoPixelConnect strip(LED_PIN, NUM_PIXELS, pio0, 0);

// ---------------- Matrix wiring (breadboardable pads only) ----------------
// Rows (R0..R3): GP0, GP1, GP2, GP3
// Columns (C0..C9): GP4, GP5, GP6, GP7, GP8, GP14, GP15, GP26, GP27, GP28
static const uint8_t ROWS = 4;
static const uint8_t COLS = 10;
static const uint8_t rowPins[ROWS] = {0, 1, 2, 3};
static const uint8_t colPins[COLS] = {4, 5, 6, 7, 8, 14, 15, 26, 27, 28};

// Fn key coordinate (hold = Layer 1)
static const uint8_t FN_ROW = 3;
static const uint8_t FN_COL = 3;

// Debounce params
static const uint8_t DEBOUNCE_MS = 5;

// Key state buffers
static bool rawState[ROWS][COLS];
static bool debounced[ROWS][COLS];
static uint8_t stableCount[ROWS][COLS];

// HID report (boot keyboard): up to 6 keys + modifiers
#define MAX_KEYS 6

// -------------------------------------------------------------------------
// Keyboard HID setup
// -------------------------------------------------------------------------
void setupHID() {
  Keyboard.begin();
  delay(100);
}

// -------------------------------------------------------------------------
// LED setup and control
// -------------------------------------------------------------------------
void setupLED() {
  strip.neoPixelClear(true);  // Start with LED off
}

void updateLED() {
  uint32_t now = millis();
  if (now - lastLedToggle >= 1000) {  // Toggle every 1000ms (1 second)
    ledState = !ledState;
    if (ledState) {
      // Turn on with a nice blue color
      strip.neoPixelSetValue(0, 0, 0, 255, true);
    } else {
      // Turn off
      strip.neoPixelSetValue(0, 0, 0, 0, true);
    }
    lastLedToggle = now;
  }
}

// -------------------------------------------------------------------------
// Matrix keymap: 2 layers (0: base, 1: Fn)
// Use HID_KEY_* for normal keys; use 0 for KC_NO (empty).
// For modifiers like LALT we use special code 0xE2 (Left Alt) in modifier byte.
// -------------------------------------------------------------------------

#define KC_NO 0

// We'll encode Left Alt as a "modifier sentinel" using Arduino Keyboard library constant
#define KC_LALT KEY_LEFT_ALT

// Base layer (letters/punct) and Fn layer (numbers/arrows/symbols)
static const uint8_t keymap[2][ROWS][COLS] = {
// Layer 0
{
  // R0: Q W E R T   Y U I O P
  {'q', 'w', 'e', 'r', 't', 'y', 'u', 'i', 'o', 'p'},
  // R1: A S D F G   H J K L ;
  {'a', 's', 'd', 'f', 'g', 'h', 'j', 'k', 'l', ';'},
  // R2: Z X C V B   N M , . /
  {'z', 'x', 'c', 'v', 'b', 'n', 'm', ',', '.', '/'},
  // R3: __ __ Space Fn Enter Bspc Alt __ __ __   (only C2..C6 used)
  {KC_NO, KC_NO, ' ', KC_NO /*Fn handled separately*/, KEY_RETURN, KEY_BACKSPACE, KC_LALT, KC_NO, KC_NO, KC_NO}
},
// Layer 1 (Fn)
{
  // Numbers row
  {'1', '2', '3', '4', '5', '6', '7', '8', '9', '0'},
  // Symbols / editing
  {KEY_TAB, KEY_ESC, '-', '=', '`', '[', ']', '\\', '\'', KEY_DELETE},
  // Arrows and nav
  {KEY_LEFT_ARROW, KEY_DOWN_ARROW, KEY_UP_ARROW, KEY_RIGHT_ARROW, KEY_MENU, KEY_PAGE_DOWN, KEY_PAGE_UP, KEY_HOME, KEY_END, KEY_INSERT},
  // Bottom row pass-through (kept transparent/none)
  {KC_NO, KC_NO, KC_NO, KC_NO, KC_NO, KC_NO, KC_NO, KC_NO, KC_NO, KC_NO}
}
};

// -------------------------------------------------------------------------
// Matrix scanning
// With diodes COL -> ROW, we drive one column LOW at a time and read rows.
// Non-scanned columns are left as INPUT (Hi-Z). Rows use INPUT_PULLUP.
// Pressed = row reads LOW during the active column.
// -------------------------------------------------------------------------

void setupMatrixPins() {
  for (uint8_t r = 0; r < ROWS; r++) {
    pinMode(rowPins[r], INPUT_PULLUP);
  }
  for (uint8_t c = 0; c < COLS; c++) {
    pinMode(colPins[c], INPUT); // Hi-Z by default
  }
}

void scanMatrixRaw() {
  for (uint8_t c = 0; c < COLS; c++) {
    // Activate this column: drive LOW
    pinMode(colPins[c], OUTPUT);
    digitalWrite(colPins[c], LOW);
    delayMicroseconds(30);

    for (uint8_t r = 0; r < ROWS; r++) {
      bool pressed = (digitalRead(rowPins[r]) == LOW);
      rawState[r][c] = pressed;
    }

    // Deactivate: go back to Hi-Z
    pinMode(colPins[c], INPUT);
    // small settle
    delayMicroseconds(5);
  }
}

void debounceMatrix() {
  static uint32_t last_ms = 0;
  uint32_t now = millis();
  bool time_elapsed = (now - last_ms) >= 1; // run every ~1ms
  if (!time_elapsed) return;
  last_ms = now;

  for (uint8_t r = 0; r < ROWS; r++) {
    for (uint8_t c = 0; c < COLS; c++) {
      bool v = rawState[r][c];
      if (v == debounced[r][c]) {
        stableCount[r][c] = 0;
      } else {
        if (stableCount[r][c] < DEBOUNCE_MS) {
          stableCount[r][c]++;
          if (stableCount[r][c] >= DEBOUNCE_MS) {
            debounced[r][c] = v;
            stableCount[r][c] = 0;
          }
        }
      }
    }
  }
}

// -------------------------------------------------------------------------
// Build and send HID report
// Fn is held at (FN_ROW, FN_COL) to select layer 1.
// Modifiers (only LALT here) are sent via modifier byte.
// -------------------------------------------------------------------------

// Track previous key states to detect press/release events
static bool prevDebounced[ROWS][COLS];

void sendHIDReport() {
  uint8_t layer = debounced[FN_ROW][FN_COL] ? 1 : 0;

  for (uint8_t r = 0; r < ROWS; r++) {
    for (uint8_t c = 0; c < COLS; c++) {
      if (r == FN_ROW && c == FN_COL) continue; // Fn not a HID key

      bool currentState = debounced[r][c];
      bool previousState = prevDebounced[r][c];

      // Key state changed
      if (currentState != previousState) {
        uint8_t code = keymap[layer][r][c];
        
        if (code != KC_NO) {
          if (currentState) {
            // Key pressed
            if (code >= 0xE0 && code <= 0xE7) {
              // Modifier key
              switch (code) {
                case 0xE0: Keyboard.press(KEY_LEFT_CTRL); break;
                case 0xE1: Keyboard.press(KEY_LEFT_SHIFT); break;
                case 0xE2: Keyboard.press(KEY_LEFT_ALT); break;
                case 0xE3: Keyboard.press(KEY_LEFT_GUI); break;
                case 0xE4: Keyboard.press(KEY_RIGHT_CTRL); break;
                case 0xE5: Keyboard.press(KEY_RIGHT_SHIFT); break;
                case 0xE6: Keyboard.press(KEY_RIGHT_ALT); break;
                case 0xE7: Keyboard.press(KEY_RIGHT_GUI); break;
              }
            } else {
              // Regular key
              Keyboard.press(code);
            }
          } else {
            // Key released
            if (code >= 0xE0 && code <= 0xE7) {
              // Modifier key
              switch (code) {
                case 0xE0: Keyboard.release(KEY_LEFT_CTRL); break;
                case 0xE1: Keyboard.release(KEY_LEFT_SHIFT); break;
                case 0xE2: Keyboard.release(KEY_LEFT_ALT); break;
                case 0xE3: Keyboard.release(KEY_LEFT_GUI); break;
                case 0xE4: Keyboard.release(KEY_RIGHT_CTRL); break;
                case 0xE5: Keyboard.release(KEY_RIGHT_SHIFT); break;
                case 0xE6: Keyboard.release(KEY_RIGHT_ALT); break;
                case 0xE7: Keyboard.release(KEY_RIGHT_GUI); break;
              }
            } else {
              // Regular key
              Keyboard.release(code);
            }
          }
        }
        
        prevDebounced[r][c] = currentState;
      }
    }
  }
}

// -------------------------------------------------------------------------
// Setup and loop
// -------------------------------------------------------------------------

void setup() {
  setupHID();
  setupMatrixPins();
  setupLED();

  // Clear states
  for (uint8_t r = 0; r < ROWS; r++) {
    for (uint8_t c = 0; c < COLS; c++) {
      rawState[r][c] = false;
      debounced[r][c] = false;
      prevDebounced[r][c] = false;
      stableCount[r][c] = 0;
    }
  }
}

void loop() {
  scanMatrixRaw();
  debounceMatrix();
  sendHIDReport();
  updateLED();
  delay(1);
}
