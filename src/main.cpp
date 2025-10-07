#include <Arduino.h>

#ifdef ESP32_C3
  #include <BleKeyboard.h>
#else
  #include <NeoPixelConnect.h>
  #include <Keyboard.h>
#endif

// ---------------- LED Configuration ----------------
#ifdef ESP32_C3
  #define LED_PIN 8  // Simple blue LED on ESP32-C3 (adjust as needed)
#else
  #define LED_PIN 16  // WS2812 RGB LED on RP2040 Zero
  #define NUM_PIXELS 1  // Single onboard LED
#endif
static uint32_t lastLedToggle = 0;
static bool ledState = false;
static bool anyKeyPressed = false;  // Track if any key is currently pressed

#ifdef ESP32_C3
  BleKeyboard bleKeyboard("Thumb35", "MicroKeyboard", 100);
#else
  NeoPixelConnect strip(LED_PIN, NUM_PIXELS, pio0, 0);
#endif

// ---------------- Matrix wiring ----------------
#ifdef ESP32_C3
  // ESP32-C3 pin mapping (adjust as needed for your specific board)
  // Rows (R0..R3): GPIO0, GPIO1, GPIO2, GPIO3
  // Columns (C0..C9): GPIO4, GPIO5, GPIO6, GPIO7, GPIO9, GPIO10, GPIO18, GPIO19, GPIO20, GPIO21
  static const uint8_t ROWS = 4;
  static const uint8_t COLS = 10;
  static const uint8_t rowPins[ROWS] = {0, 1, 2, 3};
  static const uint8_t colPins[COLS] = {4, 5, 6, 7, 9, 10, 18, 19, 20, 21};
#else
  // RP2040 pin mapping (breadboardable pads only)
  // Rows (R0..R3): GP0, GP1, GP2, GP3
  // Columns (C0..C9): GP4, GP5, GP6, GP7, GP8, GP14, GP15, GP26, GP27, GP28
  static const uint8_t ROWS = 4;
  static const uint8_t COLS = 10;
  static const uint8_t rowPins[ROWS] = {0, 1, 2, 3};
  static const uint8_t colPins[COLS] = {4, 5, 6, 7, 8, 14, 15, 26, 27, 28};
#endif

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
#ifdef ESP32_C3
  bleKeyboard.begin();
  // Give BLE stack more time to initialize and start advertising
  delay(1000);
#else
  Keyboard.begin();
  delay(100);
#endif
}

// -------------------------------------------------------------------------
// LED setup and control
// -------------------------------------------------------------------------
void setupLED() {
#ifdef ESP32_C3
  pinMode(LED_PIN, OUTPUT);
  digitalWrite(LED_PIN, LOW);  // Start with LED off
#else
  strip.neoPixelClear(true);  // Start with LED off
#endif
}

void updateLED() {
  uint32_t now = millis();
  
  // If any key is pressed, keep LED on solid
  if (anyKeyPressed) {
#ifdef ESP32_C3
    digitalWrite(LED_PIN, HIGH);
#else
    strip.neoPixelSetValue(0, 255, 0, 0, true);  // Red for key press
#endif
    return;
  }
  
#ifdef ESP32_C3
  // BLE connection status indication
  if (!bleKeyboard.isConnected()) {
    // Fast blink when not connected (waiting for pairing)
    if (now - lastLedToggle >= 250) {  // Toggle every 250ms (fast blink)
      ledState = !ledState;
      digitalWrite(LED_PIN, ledState ? HIGH : LOW);
      lastLedToggle = now;
    }
  } else {
    // Slow heartbeat when connected
    if (now - lastLedToggle >= 2000) {  // Toggle every 2000ms (slow blink)
      ledState = !ledState;
      digitalWrite(LED_PIN, ledState ? HIGH : LOW);
      lastLedToggle = now;
    }
  }
#else
  // Normal heartbeat blink for RP2040
  if (now - lastLedToggle >= 1000) {  // Toggle every 1000ms (1 second)
    ledState = !ledState;
    if (ledState) {
      // Turn on LED
      strip.neoPixelSetValue(0, 0, 0, 255, true);
    } else {
      // Turn off LED
      strip.neoPixelSetValue(0, 0, 0, 0, true);
    }
    lastLedToggle = now;
  }
#endif
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
  {KEY_LEFT_ARROW, KEY_DOWN_ARROW, KEY_UP_ARROW, KEY_RIGHT_ARROW, KEY_F1, KEY_PAGE_DOWN, KEY_PAGE_UP, KEY_HOME, KEY_END, KEY_INSERT},
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
#ifdef ESP32_C3
  // Check if BLE keyboard is connected
  if (!bleKeyboard.isConnected()) {
    return;
  }
#endif

  uint8_t layer = debounced[FN_ROW][FN_COL] ? 1 : 0;
  
  // Check if any key is currently pressed (for LED feedback)
  anyKeyPressed = false;
  for (uint8_t r = 0; r < ROWS; r++) {
    for (uint8_t c = 0; c < COLS; c++) {
      // Skip the Fn key itself
      if (r == FN_ROW && c == FN_COL) continue;
      
      if (debounced[r][c]) {
        uint8_t code = keymap[layer][r][c];
        Serial.print("Key pressed at R");
        Serial.print(r);
        Serial.print(" C");
        Serial.print(c);
        Serial.print(" Layer:");
        Serial.print(layer);
        Serial.print(" Code:0x");
        Serial.print(code, HEX);
        Serial.print(" (");
        Serial.print((char)code);
        Serial.println(")");
        
        if (code != KC_NO) {
          anyKeyPressed = true;
          break;
        }
      }
    }
    if (anyKeyPressed) break;
  }
  
  if (anyKeyPressed) {
    Serial.println("anyKeyPressed = TRUE");
  }

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
#ifdef ESP32_C3
              switch (code) {
                case 0xE0: bleKeyboard.press(KEY_LEFT_CTRL); break;
                case 0xE1: bleKeyboard.press(KEY_LEFT_SHIFT); break;
                case 0xE2: bleKeyboard.press(KEY_LEFT_ALT); break;
                case 0xE3: bleKeyboard.press(KEY_LEFT_GUI); break;
                case 0xE4: bleKeyboard.press(KEY_RIGHT_CTRL); break;
                case 0xE5: bleKeyboard.press(KEY_RIGHT_SHIFT); break;
                case 0xE6: bleKeyboard.press(KEY_RIGHT_ALT); break;
                case 0xE7: bleKeyboard.press(KEY_RIGHT_GUI); break;
              }
#else
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
#endif
            } else {
              // Regular key
#ifdef ESP32_C3
              bleKeyboard.press(code);
#else
              Keyboard.press(code);
#endif
            }
          } else {
            // Key released
            if (code >= 0xE0 && code <= 0xE7) {
              // Modifier key
#ifdef ESP32_C3
              switch (code) {
                case 0xE0: bleKeyboard.release(KEY_LEFT_CTRL); break;
                case 0xE1: bleKeyboard.release(KEY_LEFT_SHIFT); break;
                case 0xE2: bleKeyboard.release(KEY_LEFT_ALT); break;
                case 0xE3: bleKeyboard.release(KEY_LEFT_GUI); break;
                case 0xE4: bleKeyboard.release(KEY_RIGHT_CTRL); break;
                case 0xE5: bleKeyboard.release(KEY_RIGHT_SHIFT); break;
                case 0xE6: bleKeyboard.release(KEY_RIGHT_ALT); break;
                case 0xE7: bleKeyboard.release(KEY_RIGHT_GUI); break;
              }
#else
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
#endif
            } else {
              // Regular key
#ifdef ESP32_C3
              bleKeyboard.release(code);
#else
              Keyboard.release(code);
#endif
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
  Serial.begin(115200);
  delay(500);
  Serial.println("Starting keyboard...");
  
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
  
  Serial.println("Keyboard ready!");
}

void loop() {
  scanMatrixRaw();
  debounceMatrix();
  sendHIDReport();
  updateLED();
  
#ifdef ESP32_C3
  // Slightly longer delay for BLE to reduce stack pressure
  delay(5);
#else
  delay(1);
#endif
}
