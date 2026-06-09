/*
 * OneWire Handler — DS2433 EEPROM operations via 1-Wire.
 *
 * Copied from esp32_bridge (audited brick-safe: only the reversible
 * write-scratchpad -> read-back-verify -> copy-scratchpad sequence) with a
 * raw-ROM accessor added for on-device key derivation.
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

  // DS2433 Commands (all reversible — no protection/lock commands)
  static const uint8_t CMD_READ_MEMORY = 0xF0;
  static const uint8_t CMD_WRITE_SCRATCHPAD = 0x0F;
  static const uint8_t CMD_READ_SCRATCHPAD = 0xAA;
  static const uint8_t CMD_COPY_SCRATCHPAD = 0x55;
  static const uint8_t CMD_MATCH_ROM = 0x55;

  bool writeBlock(uint16_t addr, const uint8_t* data, uint8_t len);

public:
  OneWireHandler(uint8_t pin);

  bool search();
  String getRomAddress();
  const uint8_t* romBytes() { return romAddress; }   // raw 8-byte ROM / UID
  bool reset();
  uint8_t resetRaw() { return ow.reset(); }
  bool read(uint16_t addr, uint8_t* buffer, uint16_t len);
  bool write(uint16_t addr, const uint8_t* data, uint16_t len);
  bool isDeviceFound() { return deviceFound; }
};

#endif
