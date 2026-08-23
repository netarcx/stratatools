/*
 * OneWire Handler Implementation
 */

#include "onewire_handler.h"
#include <string.h>

OneWireHandler::OneWireHandler(uint8_t pin) : ow(pin) {
  deviceFound = false;
  memset(romAddress, 0, 8);
}

bool OneWireHandler::search() {
  // MUST reset the search state first. OneWire::search() latches LastDeviceFlag
  // once it has enumerated the last device on the bus, and the whole search body
  // is skipped while that flag is set -- so without this, every second call
  // returns false and a perfectly good cartridge reports as "not found".
  ow.reset_search();

  if (!ow.search(romAddress)) {
    deviceFound = false;
    ow.reset_search();
    return false;
  }

  // Verify CRC8 of ROM address
  if (OneWire::crc8(romAddress, 7) != romAddress[7]) {
    deviceFound = false;
    ow.reset_search();
    return false;
  }

  deviceFound = true;
  return true;
}

// Format the 8-byte ROM as lowercase hex. Uses a caller-supplied buffer rather
// than an Arduino String: the ATmega328P has 2 KB of SRAM and no room to risk
// heap fragmentation on a cosmetic log line.
void OneWireHandler::getRomAddress(char out[17]) {
  static const char HEXDIGITS[] = "0123456789abcdef";
  for (uint8_t i = 0; i < 8; i++) {
    out[i * 2]     = HEXDIGITS[romAddress[i] >> 4];
    out[i * 2 + 1] = HEXDIGITS[romAddress[i] & 0x0F];
  }
  out[16] = '\0';
}

bool OneWireHandler::reset() {
  uint8_t result = ow.reset();
  // 0 = no presence, 1 = presence detected, 2 = short circuit
  return result == 1;
}

bool OneWireHandler::read(uint16_t addr, uint8_t* buffer, uint16_t len) {
  if (!deviceFound) return false;

  // Reset bus
  if (!reset()) return false;

  // Select device
  ow.write(CMD_MATCH_ROM);
  for (int i = 0; i < 8; i++) {
    ow.write(romAddress[i]);
  }

  // Read memory command
  ow.write(CMD_READ_MEMORY);
  ow.write(addr & 0xFF);        // TA1 (address low byte)
  ow.write((addr >> 8) & 0xFF); // TA2 (address high byte)

  // Read data
  for (uint16_t i = 0; i < len; i++) {
    buffer[i] = ow.read();
  }

  return true;
}

bool OneWireHandler::writeBlock(uint16_t addr, const uint8_t* data, uint8_t len) {
  if (len > 32) len = 32;  // DS2433 scratchpad is 32 bytes

  // Reset and select device
  if (!reset()) return false;

  ow.write(CMD_MATCH_ROM);
  for (int i = 0; i < 8; i++) {
    ow.write(romAddress[i]);
  }

  // Write scratchpad
  ow.write(CMD_WRITE_SCRATCHPAD);
  ow.write(addr & 0xFF);
  ow.write((addr >> 8) & 0xFF);

  for (uint8_t i = 0; i < len; i++) {
    ow.write(data[i]);
  }

  // Delay for scratchpad write
  delay(10);

  // Read scratchpad to verify
  if (!reset()) return false;

  ow.write(CMD_MATCH_ROM);
  for (int i = 0; i < 8; i++) {
    ow.write(romAddress[i]);
  }

  ow.write(CMD_READ_SCRATCHPAD);

  uint8_t ta1 = ow.read();
  uint8_t ta2 = ow.read();
  uint8_t es = ow.read();

  // Verify address
  if (ta1 != (addr & 0xFF) || ta2 != ((addr >> 8) & 0xFF)) {
    return false;
  }

  // Reject a partial scratchpad write before copying it to EEPROM. The E-S
  // byte's PF bit (0x20) is set when an incomplete byte reached the scratchpad
  // (bus glitch). copy-scratchpad would refuse or copy garbage, so bail out and
  // let the caller retry rather than commit an unverified row.
  if (es & 0x20) {
    return false;
  }

  // The AA bit (0x80) is cleared by write-scratchpad and set by a completed
  // copy. Seeing it set here means our write-scratchpad never landed and we are
  // looking at stale state from a previous copy -- do not copy that again.
  if (es & 0x80) {
    return false;
  }

  // copy-scratchpad transfers only up to the E-S ending offset, so a write that
  // was cut short mid-sequence would silently commit a partial row. PF does not
  // catch that case (it flags an incomplete *byte*), and the data compare below
  // cannot either: bytes past the ending offset read back as leftover scratchpad
  // content, which after a retry is the previous attempt's identical data.
  if ((es & 0x1F) != (uint8_t)((addr + len - 1) & 0x1F)) {
    return false;
  }

  // Verify data
  for (uint8_t i = 0; i < len; i++) {
    if (ow.read() != data[i]) {
      return false;
    }
  }

  // Copy scratchpad to EEPROM
  if (!reset()) return false;

  ow.write(CMD_MATCH_ROM);
  for (int i = 0; i < 8; i++) {
    ow.write(romAddress[i]);
  }

  ow.write(CMD_COPY_SCRATCHPAD);
  ow.write(ta1);
  ow.write(ta2);

  // The E-S byte goes out with power=1, which leaves the pin DRIVING the line
  // high instead of releasing it to the pull-up. That matters here because this
  // cartridge is wired with two conductors only -- data and ground -- so the
  // DS2433 has no VCC and must run parasitically off this line. Programming
  // draws far more than a 4.7k pull-up can supply, and a starved tPROG window
  // is how a row ends up half-written. The AVR pin sourcing directly is the
  // strong pull-up the datasheet asks for.
  ow.write(es, 1);

  // This does NOT replace the 4.7k pull-up on the data line, and does not fight
  // it: while the pin drives high both ends of that resistor sit at +5V, so no
  // current flows through it at all. depower() then returns the pin to
  // high-impedance and the 4.7k goes back to holding the line, which is what
  // every reset and time slot after this depends on. Releasing it before any
  // further bus activity is the part that matters -- a driven-high pin during
  // another device's low pulse would be a genuine conflict.
  delay(15);
  ow.depower();

  // Confirm this row actually reached EEPROM before moving on. Leaving it to
  // the whole-image check at the end would mean the remaining blocks get
  // committed on top of a failed one; catching it here lets the retry fix it.
  uint8_t check[32];
  if (!read(addr, check, len)) return false;
  if (memcmp(check, data, len) != 0) return false;

  return true;
}

bool OneWireHandler::write(uint16_t addr, const uint8_t* data, uint16_t len) {
  if (!deviceFound) return false;

  // Write in 32-byte blocks
  uint16_t offset = 0;
  while (offset < len) {
    uint8_t blockSize = (len - offset > 32) ? 32 : (len - offset);

    bool ok = false;
    for (uint8_t attempt = 0; attempt < WRITE_RETRIES && !ok; attempt++) {
      if (attempt) delay(10);   // let the bus settle before trying again
      ok = writeBlock(addr + offset, data + offset, blockSize);
    }
    if (!ok) return false;

    offset += blockSize;
  }

  return true;
}
