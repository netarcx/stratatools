/*
 * Multi-Platform 1-Wire Bridge for Stratasys Cartridge Programmer
 *
 * This firmware implements a simple serial protocol to interface
 * with DS2433/DS2432 EEPROM cartridges via 1-wire protocol.
 *
 * Compatible with: ESP32, ESP32-C3, ESP8266
 *
 * Hardware:
 * - ESP32/ESP32-C3: GPIO4 - 1-wire data line (with 4.7k pull-up to 3.3V)
 * - ESP8266: GPIO4 (D2) - 1-wire data line (with 4.7k pull-up to 3.3V)
 * - Serial: Command interface (115200 baud, USB or UART)
 */

#include <Arduino.h>
#include "onewire_handler.h"
#include "serial_protocol.h"

// Pin configuration - set by build flags in platformio.ini
#ifndef ONEWIRE_PIN
  #define ONEWIRE_PIN 4  // Default to GPIO4
#endif

#ifndef BOARD_NAME
  #define BOARD_NAME "ESP32"  // Default board name
#endif

OneWireHandler owHandler(ONEWIRE_PIN);
SerialProtocol protocol;

void setup() {
  // Initialize Serial
  Serial.begin(115200);

  // Small delay for serial to initialize
  delay(500);

  Serial.print(BOARD_NAME);
  Serial.println(" 1-Wire Bridge v1.0");
  Serial.println("Ready");
}

void loop() {
  // Check for incoming commands
  if (Serial.available()) {
    String command = Serial.readStringUntil('\n');
    command.trim();

    if (command.length() == 0) {
      return;
    }

    // Process command
    protocol.processCommand(command, owHandler, Serial);
  }
}
