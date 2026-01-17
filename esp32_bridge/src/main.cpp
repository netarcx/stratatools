/*
 * ESP32 1-Wire Bridge for Stratasys Cartridge Programmer
 *
 * This firmware implements a simple serial protocol to interface
 * with DS2433/DS2432 EEPROM cartridges via 1-wire protocol.
 *
 * Compatible with: ESP32, ESP32-C3, ESP32-S2, ESP32-S3
 *
 * Hardware:
 * - GPIO4: 1-wire data line (with 4.7k pull-up to 3.3V)
 * - Serial: Command interface (115200 baud, USB or UART)
 */

#include <Arduino.h>
#include "onewire_handler.h"
#include "serial_protocol.h"

// Pin configuration
#define ONEWIRE_PIN 4

OneWireHandler owHandler(ONEWIRE_PIN);
SerialProtocol protocol;

void setup() {
  // Initialize Serial
  Serial.begin(115200);

  // Small delay for serial to initialize
  delay(500);

  Serial.println("ESP32-C3 1-Wire Bridge v1.0");
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
