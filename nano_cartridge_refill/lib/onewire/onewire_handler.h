/*
 * OneWire Handler -- DS2433 EEPROM operations via 1-Wire.
 *
 * Copied from esp32_cartridge_refill (audited brick-safe: only the reversible
 * write-scratchpad -> read-back-verify -> copy-scratchpad sequence). The one
 * change for AVR is that the ROM address is formatted into a caller-supplied
 * char buffer instead of an Arduino String, so the Nano never touches the heap.
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

  // DS2433 Commands (all reversible -- no protection/lock commands)
  static const uint8_t CMD_READ_MEMORY = 0xF0;
  static const uint8_t CMD_WRITE_SCRATCHPAD = 0x0F;
  static const uint8_t CMD_READ_SCRATCHPAD = 0xAA;
  static const uint8_t CMD_COPY_SCRATCHPAD = 0x55;
  static const uint8_t CMD_MATCH_ROM = 0x55;

  bool writeBlock(uint16_t addr, const uint8_t* data, uint8_t len);

  // A block write is scratchpad -> read-back-verify -> copy, and every step is
  // idempotent, so a transient bus glitch is worth retrying rather than
  // aborting on. Aborting mid-image is the expensive failure: it leaves the
  // cartridge half-written.
  static const uint8_t WRITE_RETRIES = 3;

public:
  OneWireHandler(uint8_t pin);

  bool search();
  void getRomAddress(char out[17]);                  // 16 hex chars + NUL
  const uint8_t* romBytes() { return romAddress; }   // raw 8-byte ROM / UID
  bool reset();
  uint8_t resetRaw() { return ow.reset(); }
  bool read(uint16_t addr, uint8_t* buffer, uint16_t len);
  bool write(uint16_t addr, const uint8_t* data, uint16_t len);
  bool isDeviceFound() { return deviceFound; }
};

#endif
