// Minimale Attrappen des Arduino-ESP32-Cores, NUR für den Syntax-Check des Clients auf dem PC.
// Sie sind aus dem Gedächtnis der echten API nachgebaut und ersetzen keinen Build mit dem Core.
#pragma once
#include <stddef.h>
#include <stdint.h>
#include <string.h>
#include <string>
#include <time.h>

#ifndef ARDUINO
#define ARDUINO 10819
#endif

class String {
 public:
  String() {}
  String(const char *s) : s_(s) {}
  size_t length() const { return s_.size(); }
  const char *c_str() const { return s_.c_str(); }
  unsigned char concat(const char *c, unsigned int n) { s_.append(c, n); return 1; }
  unsigned char concat(const char *c) { s_ += c; return 1; }
  unsigned char concat(char c) { s_ += c; return 1; }
  unsigned char reserve(unsigned int) { return 1; }
  String &operator+=(const char *c) { s_ += c; return *this; }
  void trim() {}
 private:
  std::string s_;
};

class Print {
 public:
  virtual ~Print() {}
  virtual size_t write(uint8_t) = 0;
  virtual size_t write(const uint8_t *b, size_t n) { size_t k = 0; while (n--) k += write(*b++); return k; }
};

class Stream : public Print {
 public:
  virtual int available() = 0;
  virtual int read() = 0;
  virtual int peek() = 0;
  virtual size_t readBytes(char *b, size_t n) { size_t k = 0; int c; while (k < n && (c = read()) >= 0) b[k++] = (char)c; return k; }
};

class Printable {
 public:
  virtual ~Printable() {}
  virtual size_t printTo(Print &p) const = 0;
};
