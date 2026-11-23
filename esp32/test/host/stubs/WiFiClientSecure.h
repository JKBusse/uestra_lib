#pragma once
#include "Arduino.h"
class WiFiClient : public Stream {
 public:
  int available() override { return 0; }
  int read() override { return -1; }
  int peek() override { return -1; }
  size_t write(uint8_t) override { return 1; }
  void stop() {}
};
class WiFiClientSecure : public WiFiClient {
 public:
  void setCACert(const char *) {}
  void setInsecure() {}
};
