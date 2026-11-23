#pragma once
#include "WiFiClientSecure.h"
#define HTTP_CODE_OK 200
class HTTPClient {
 public:
  bool begin(WiFiClient &, const String &) { return true; }
  void setReuse(bool) {}
  void useHTTP10(bool = true) {}
  void setTimeout(uint16_t) {}
  void addHeader(const String &, const String &, bool = false, bool = false) {}
  int POST(const String &) { return 200; }
  WiFiClient &getStream() { return c_; }
  void end() {}
 private:
  WiFiClient c_;
};
