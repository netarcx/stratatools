/*
 * Serial Protocol Implementation
 *
 * Commands:
 *   SEARCH       - Search for 1-wire device
 *   READ <size>  - Read EEPROM (up to 512 bytes)
 *   WRITE <size> <hex_data> - Write EEPROM
 *   RESET        - Reset 1-wire bus
 *   VERSION      - Get firmware version
 *
 * Responses:
 *   ROM:<address>  - Device ROM address
 *   DATA:<hex>     - Read data
 *   OK             - Success
 *   ERROR <msg>    - Error message
 */

#include "serial_protocol.h"

SerialProtocol::SerialProtocol() {
}

bool SerialProtocol::hexStringToBytes(String hex, uint8_t* buffer, uint16_t* len) {
  hex.trim();
  *len = hex.length() / 2;

  for (uint16_t i = 0; i < *len; i++) {
    String byteStr = hex.substring(i * 2, i * 2 + 2);
    buffer[i] = (uint8_t) strtol(byteStr.c_str(), NULL, 16);
  }

  return true;
}

String SerialProtocol::bytesToHexString(const uint8_t* data, uint16_t len) {
  String result = "";
  for (uint16_t i = 0; i < len; i++) {
    if (data[i] < 0x10) result += "0";
    result += String(data[i], HEX);
  }
  return result;
}

void SerialProtocol::processCommand(String command, OneWireHandler& owHandler, Stream& serial) {
  command.toUpperCase();

  if (command == "SEARCH") {
    // Search for 1-wire device
    if (owHandler.search()) {
      serial.print("ROM:");
      serial.println(owHandler.getRomAddress());
    } else {
      serial.println("ERROR No device found");
    }
  }
  else if (command.startsWith("READ")) {
    // READ <size>
    int spaceIdx = command.indexOf(' ');
    if (spaceIdx == -1) {
      serial.println("ERROR Invalid READ command");
      return;
    }

    uint16_t size = command.substring(spaceIdx + 1).toInt();
    if (size == 0 || size > 512) {
      serial.println("ERROR Invalid size");
      return;
    }

    if (!owHandler.isDeviceFound()) {
      serial.println("ERROR No device found, run SEARCH first");
      return;
    }

    uint8_t buffer[512];
    if (owHandler.read(0, buffer, size)) {
      serial.print("DATA:");
      serial.println(bytesToHexString(buffer, size));
    } else {
      serial.println("ERROR Read failed");
    }
  }
  else if (command.startsWith("WRITE")) {
    // WRITE <size> <hex_data>
    int firstSpace = command.indexOf(' ');
    if (firstSpace == -1) {
      serial.println("ERROR Invalid WRITE command");
      return;
    }

    int secondSpace = command.indexOf(' ', firstSpace + 1);
    if (secondSpace == -1) {
      serial.println("ERROR Invalid WRITE command");
      return;
    }

    uint16_t size = command.substring(firstSpace + 1, secondSpace).toInt();
    if (size == 0 || size > 512) {
      serial.println("ERROR Invalid size");
      return;
    }

    if (!owHandler.isDeviceFound()) {
      serial.println("ERROR No device found, run SEARCH first");
      return;
    }

    String hexData = command.substring(secondSpace + 1);
    uint8_t buffer[512];
    uint16_t actualLen;

    if (!hexStringToBytes(hexData, buffer, &actualLen)) {
      serial.println("ERROR Invalid hex data");
      return;
    }

    if (actualLen != size) {
      serial.println("ERROR Size mismatch");
      return;
    }

    if (owHandler.write(0, buffer, size)) {
      serial.println("OK");
    } else {
      serial.println("ERROR Write failed");
    }
  }
  else if (command == "RESET") {
    if (owHandler.reset()) {
      serial.println("OK");
    } else {
      serial.println("ERROR Reset failed");
    }
  }
  else if (command == "VERSION") {
    #ifndef BOARD_NAME
      #define BOARD_NAME "ESP32"
    #endif
    serial.print(BOARD_NAME);
    serial.println(" 1-Wire Bridge v1.0");
  }
  else if (command == "DEBUG") {
    // Debug command to check 1-wire bus
    #ifndef ONEWIRE_PIN
      #define ONEWIRE_PIN 4
    #endif

    serial.print("DEBUG: Testing 1-wire bus on GPIO");
    serial.print(ONEWIRE_PIN);
    serial.println("...");
    serial.println("  Required: 4.7k pullup to 3.3V + EEPROM data line");
    serial.println("");

    // Check pin state
    pinMode(ONEWIRE_PIN, INPUT);
    int pinState = digitalRead(ONEWIRE_PIN);
    serial.print("  GPIO");
    serial.print(ONEWIRE_PIN);
    serial.print(" state (idle): ");
    serial.println(pinState ? "HIGH (good - pullup present)" : "LOW (BAD - no pullup or short to ground!)");
    serial.println("");

    // Try reset multiple times
    for (int i = 0; i < 5; i++) {
      uint8_t result = owHandler.resetRaw();
      serial.print("  Reset #");
      serial.print(i + 1);
      serial.print(": raw=");
      serial.print(result);
      serial.print(" - ");

      if (result == 0) {
        serial.println("NO PRESENCE (no device responding)");
      } else if (result == 1) {
        serial.println("PRESENCE DETECTED (device found!)");
      } else if (result == 2) {
        serial.println("SHORT CIRCUIT (data line shorted)");
      } else {
        serial.println("UNKNOWN");
      }
      delay(100);
    }

    serial.println("");
    serial.println("DEBUG: If GPIO4=LOW, add 4.7k resistor from GPIO4 to 3.3V");
    serial.println("DEBUG: If GPIO4=HIGH but no presence, check EEPROM connection");
  }
  else {
    serial.println("ERROR Unknown command");
  }
}
