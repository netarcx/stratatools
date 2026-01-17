/*
 * Serial Protocol Handler
 * Parses commands and formats responses
 */

#ifndef SERIAL_PROTOCOL_H
#define SERIAL_PROTOCOL_H

#include <Arduino.h>
#include "onewire_handler.h"

class SerialProtocol {
private:
  // Helper to convert hex string to bytes
  bool hexStringToBytes(String hex, uint8_t* buffer, uint16_t* len);

  // Helper to convert bytes to hex string
  String bytesToHexString(const uint8_t* data, uint16_t len);

public:
  SerialProtocol();

  // Process a command and send response
  void processCommand(String command, OneWireHandler& owHandler, Stream& serial);
};

#endif
