/*
 * OneWire Handler Implementation
 */

#include "onewire_handler.h"

OneWireHandler::OneWireHandler(uint8_t pin) : ow(pin) {
  deviceFound = false;
  memset(romAddress, 0, 8);
}

bool OneWireHandler::search() {
  if (!ow.search(romAddress)) {
    deviceFound = false;
    ow.reset_search();
    return false;
  }

  // Verify CRC8 of ROM address
  if (OneWire::crc8(romAddress, 7) != romAddress[7]) {
    deviceFound = false;
    return false;
  }

  deviceFound = true;
  return true;
}

String OneWireHandler::getRomAddress() {
  String result = "";
  for (int i = 0; i < 8; i++) {
    if (romAddress[i] < 0x10) result += "0";
    result += String(romAddress[i], HEX);
  }
  return result;
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
  ow.write(es);

  // Wait for copy to complete (typically 10ms)
  delay(15);

  return true;
}

bool OneWireHandler::write(uint16_t addr, const uint8_t* data, uint16_t len) {
  if (!deviceFound) return false;

  // Write in 32-byte blocks
  uint16_t offset = 0;
  while (offset < len) {
    uint8_t blockSize = (len - offset > 32) ? 32 : (len - offset);

    if (!writeBlock(addr + offset, data + offset, blockSize)) {
      return false;
    }

    offset += blockSize;
  }

  return true;
}
