/*
 * Standalone Cartridge Auto-Refill Device
 *
 * This device automatically detects and refills Stratasys cartridges
 *
 * Features:
 * - Auto-detect cartridge insertion
 * - Read cartridge and check quantity
 * - Automatically refill when below threshold
 * - LED status indicators
 * - Optional button for manual refill
 * - Serial interface for monitoring/control
 *
 * Status LED:
 * - Slow blink: Waiting for cartridge
 * - Fast blink: Reading cartridge
 * - Solid: Cartridge OK (above threshold)
 * - Triple blink: Refilling cartridge
 * - Rapid blink: Error
 *
 * Note: This version requires the Python refill daemon running
 * For true standalone operation, use Raspberry Pi version
 */

#include <Arduino.h>
#include <OneWire.h>

// Pin definitions (set by platformio.ini)
#ifndef ONEWIRE_PIN
  #define ONEWIRE_PIN 4
#endif

#ifndef STATUS_LED
  #define STATUS_LED 2
#endif

#ifndef BUTTON_PIN
  #define BUTTON_PIN 0
#endif

#ifndef AUTO_REFILL_THRESHOLD
  #define AUTO_REFILL_THRESHOLD 10.0
#endif

// Timing
#define CHECK_INTERVAL 5000  // Check for cartridge every 5 seconds
#define DEBOUNCE_TIME 50

OneWire ow(ONEWIRE_PIN);
uint8_t romAddress[8];
bool devicePresent = false;
bool lastDevicePresent = false;
unsigned long lastCheck = 0;
unsigned long lastBlink = 0;
bool ledState = false;
int blinkPattern = 0; // 0=slow, 1=fast, 2=solid, 3=triple, 4=error
bool buttonPressed = false;
unsigned long lastButtonChange = 0;

// LED blink patterns
void updateLED() {
  unsigned long now = millis();

  switch (blinkPattern) {
    case 0: // Slow blink - waiting for cartridge
      if (now - lastBlink > 1000) {
        ledState = !ledState;
        digitalWrite(STATUS_LED, ledState);
        lastBlink = now;
      }
      break;

    case 1: // Fast blink - reading
      if (now - lastBlink > 200) {
        ledState = !ledState;
        digitalWrite(STATUS_LED, ledState);
        lastBlink = now;
      }
      break;

    case 2: // Solid - cartridge OK
      digitalWrite(STATUS_LED, HIGH);
      break;

    case 3: // Triple blink - refilling
      // Pattern: on-off-on-off-on-off-pause
      static int tripleCount = 0;
      if (now - lastBlink > 200) {
        if (tripleCount < 6) {
          ledState = !ledState;
          digitalWrite(STATUS_LED, ledState);
          tripleCount++;
        } else if (now - lastBlink > 1000) {
          tripleCount = 0;
        }
        lastBlink = now;
      }
      break;

    case 4: // Rapid blink - error
      if (now - lastBlink > 100) {
        ledState = !ledState;
        digitalWrite(STATUS_LED, ledState);
        lastBlink = now;
      }
      break;
  }
}

bool resetBus() {
  uint8_t result = ow.reset();
  return result == 1;
}

bool searchDevice() {
  if (!ow.search(romAddress)) {
    ow.reset_search();
    return false;
  }

  // Verify CRC8
  if (OneWire::crc8(romAddress, 7) != romAddress[7]) {
    return false;
  }

  return true;
}

String getRomHex() {
  String result = "";
  for (int i = 0; i < 8; i++) {
    if (romAddress[i] < 0x10) result += "0";
    result += String(romAddress[i], HEX);
  }
  return result;
}

void setup() {
  Serial.begin(115200);

  pinMode(STATUS_LED, OUTPUT);
  pinMode(BUTTON_PIN, INPUT_PULLUP);

  delay(500);

  Serial.println();
  Serial.println("=========================================");
  Serial.println("  Stratasys Auto-Refill Device v1.0");
  #ifdef BOARD_ESP32
    Serial.println("  Platform: ESP32");
  #else
    Serial.println("  Platform: ESP8266");
  #endif
  Serial.println("=========================================");
  Serial.println();
  Serial.print("1-Wire Pin: GPIO");
  Serial.println(ONEWIRE_PIN);
  Serial.print("Status LED: GPIO");
  Serial.println(STATUS_LED);
  Serial.print("Button Pin: GPIO");
  Serial.println(BUTTON_PIN);
  Serial.print("Auto-refill threshold: ");
  Serial.print(AUTO_REFILL_THRESHOLD);
  Serial.println(" cu.in");
  Serial.println();
  Serial.println("Waiting for cartridge...");
  Serial.println("Press button for manual refill");
  Serial.println();

  blinkPattern = 0; // Slow blink - waiting
}

void loop() {
  updateLED();

  unsigned long now = millis();

  // Check button (with debounce)
  bool buttonState = digitalRead(BUTTON_PIN) == LOW;
  if (buttonState && !buttonPressed && (now - lastButtonChange > DEBOUNCE_TIME)) {
    buttonPressed = true;
    lastButtonChange = now;

    if (devicePresent) {
      Serial.println();
      Serial.println("*** MANUAL REFILL TRIGGERED ***");
      Serial.print("ROM:");
      Serial.println(getRomHex());
      Serial.println();
      blinkPattern = 3; // Triple blink - refilling
    }
  } else if (!buttonState && buttonPressed && (now - lastButtonChange > DEBOUNCE_TIME)) {
    buttonPressed = false;
    lastButtonChange = now;
  }

  // Check for cartridge at regular intervals
  if (now - lastCheck > CHECK_INTERVAL) {
    lastCheck = now;

    lastDevicePresent = devicePresent;

    // Try to find device
    if (resetBus()) {
      devicePresent = searchDevice();
    } else {
      devicePresent = false;
    }

    // Cartridge insertion detected
    if (devicePresent && !lastDevicePresent) {
      Serial.println();
      Serial.println("*** CARTRIDGE DETECTED ***");
      Serial.print("ROM:");
      Serial.println(getRomHex());
      Serial.println();
      Serial.println("Waiting for refill daemon to process...");
      Serial.println("(Run: python3 autorefill_daemon.py)");
      Serial.println();

      blinkPattern = 1; // Fast blink - reading

      // Wait a moment
      delay(500);

      // Send notification to daemon if connected
      Serial.print("CARTRIDGE_INSERTED:");
      Serial.println(getRomHex());
    }

    // Cartridge removal detected
    if (!devicePresent && lastDevicePresent) {
      Serial.println();
      Serial.println("Cartridge removed");
      Serial.println("Waiting for next cartridge...");
      Serial.println();

      blinkPattern = 0; // Slow blink - waiting
    }

    // Cartridge still present - check if daemon responded
    if (devicePresent) {
      blinkPattern = 2; // Solid - cartridge present
    }
  }

  // Check for commands from daemon/serial
  if (Serial.available()) {
    String command = Serial.readStringUntil('\n');
    command.trim();

    if (command == "STATUS") {
      Serial.print("Device present: ");
      Serial.println(devicePresent ? "YES" : "NO");
      if (devicePresent) {
        Serial.print("ROM: ");
        Serial.println(getRomHex());
      }
    }
    else if (command.startsWith("REFILLING")) {
      blinkPattern = 3; // Triple blink - refilling
      Serial.println("Refill acknowledged");
    }
    else if (command.startsWith("REFILL_DONE")) {
      blinkPattern = 2; // Solid - complete
      Serial.println("Refill complete acknowledged");

      // Celebrate!
      for (int i = 0; i < 5; i++) {
        digitalWrite(STATUS_LED, HIGH);
        delay(100);
        digitalWrite(STATUS_LED, LOW);
        delay(100);
      }
      digitalWrite(STATUS_LED, HIGH);
    }
    else if (command.startsWith("ERROR")) {
      blinkPattern = 4; // Rapid blink - error
      Serial.println("Error acknowledged");
      delay(5000);
      blinkPattern = devicePresent ? 2 : 0;
    }
  }

  delay(10);
}
