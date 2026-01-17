/*
 * OneWire Handler
 * Manages DS2433/DS2432 EEPROM operations via 1-wire protocol
 */

#ifndef ONEWIRE_HANDLER_H
#define ONEWIRE_HANDLER_H

#include <Arduino.h>
#include <OneWire.h>

class OneWireHandler {
private:
  OneWire ow;
  uint8_t romAddress[8];
  bool deviceFound;

  // DS2433 Commands
  static const uint8_t CMD_READ_MEMORY = 0xF0;
  static const uint8_t CMD_WRITE_SCRATCHPAD = 0x0F;
  static const uint8_t CMD_READ_SCRATCHPAD = 0xAA;
  static const uint8_t CMD_COPY_SCRATCHPAD = 0x55;
  static const uint8_t CMD_MATCH_ROM = 0x55;

  // Write a block to scratchpad, verify, and copy to EEPROM
  bool writeBlock(uint16_t addr, const uint8_t* data, uint8_t len);

public:
  OneWireHandler(uint8_t pin);

  // Search for 1-wire device and store ROM address
  bool search();

  // Get the ROM address as hex string
  String getRomAddress();

  // Reset the 1-wire bus
  bool reset();

  // Get raw reset result for debugging (0=no presence, 1=presence, 2=short)
  uint8_t resetRaw() { return ow.reset(); }

  // Read data from EEPROM
  bool read(uint16_t addr, uint8_t* buffer, uint16_t len);

  // Write data to EEPROM (handles scratchpad operations)
  bool write(uint16_t addr, const uint8_t* data, uint16_t len);

  // Check if device is found
  bool isDeviceFound() { return deviceFound; }
};

#endif
