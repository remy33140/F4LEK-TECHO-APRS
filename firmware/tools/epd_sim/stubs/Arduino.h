// Arduino API, just enough to compile the e-paper driver on a PC (screenshot simulator).
#pragma once
#include <cmath>
#include <cstdarg>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>

typedef uint8_t byte;
#define F(s) (s)
#define PROGMEM
#define HIGH 1
#define LOW 0
#define INPUT 0
#define OUTPUT 1
#define INPUT_PULLUP 2
#define INPUT_PULLDOWN 3

uint32_t millis();
uint32_t micros();
inline void delay(uint32_t) {}
inline void delayMicroseconds(uint32_t) {}
inline void yield() {}
inline void pinMode(int, int) {}
inline void digitalWrite(int, int) {}
inline int digitalRead(int) { return 0; }
int analogRead(int pin);   // sim_stubs.cpp: the battery voltage of the scenario, as the ADC sees it
#define AR_INTERNAL_3_0 0
inline void analogReference(int) {}
inline void analogReadResolution(int) {}
inline void analogSampleTime(int) {}
inline void tone(int, unsigned, unsigned long = 0) {}
inline void noTone(int) {}
template <class T> T constrain(T v, T a, T b) { return v < a ? a : (v > b ? b : v); }

class String {
 public:
  String() {}
  String(const char *s) : s_(s ? s : "") {}
  String(const std::string &s) : s_(s) {}
  String(char c) : s_(1, c) {}
  String(int v) : s_(std::to_string(v)) {}
  String(unsigned v) : s_(std::to_string(v)) {}
  String(long v) : s_(std::to_string(v)) {}
  String(unsigned long v) : s_(std::to_string(v)) {}
  String(float v, int d = 2) { char b[32]; snprintf(b, sizeof b, "%.*f", d, v); s_ = b; }
  String(double v, int d = 2) { char b[32]; snprintf(b, sizeof b, "%.*f", d, v); s_ = b; }
  const char *c_str() const { return s_.c_str(); }
  unsigned length() const { return (unsigned)s_.size(); }
  char operator[](unsigned i) const { return i < s_.size() ? s_[i] : 0; }
  char &operator[](unsigned i) { return s_[i]; }
  String substring(unsigned a, unsigned b = (unsigned)-1) const {
    if (a > s_.size()) return String();
    if (b > s_.size()) b = (unsigned)s_.size();
    return String(s_.substr(a, b > a ? b - a : 0));
  }
  int indexOf(char c, unsigned from = 0) const { auto p = s_.find(c, from); return p == std::string::npos ? -1 : (int)p; }
  int indexOf(const char *t, unsigned from = 0) const { auto p = s_.find(t, from); return p == std::string::npos ? -1 : (int)p; }
  int indexOf(const String &t, unsigned from = 0) const { return indexOf(t.c_str(), from); }
  int lastIndexOf(char c) const { auto p = s_.rfind(c); return p == std::string::npos ? -1 : (int)p; }
  bool startsWith(const char *t) const { return s_.rfind(t, 0) == 0; }
  bool startsWith(const String &t) const { return startsWith(t.c_str()); }
  bool endsWith(const char *t) const { size_t n = strlen(t); return s_.size() >= n && s_.compare(s_.size() - n, n, t) == 0; }
  void trim() { size_t a = s_.find_first_not_of(" \t\r\n"); size_t b = s_.find_last_not_of(" \t\r\n");
                s_ = (a == std::string::npos) ? "" : s_.substr(a, b - a + 1); }
  void toUpperCase() { for (auto &c : s_) c = (char)toupper((unsigned char)c); }
  void toLowerCase() { for (auto &c : s_) c = (char)tolower((unsigned char)c); }
  void replace(const char *a, const char *b) { size_t p = 0, n = strlen(a); if (!n) return;
    while ((p = s_.find(a, p)) != std::string::npos) { s_.replace(p, n, b); p += strlen(b); } }
  void reserve(unsigned) {}
  int toInt() const { return atoi(s_.c_str()); }
  float toFloat() const { return (float)atof(s_.c_str()); }
  bool equalsIgnoreCase(const String &o) const { return strcasecmp(s_.c_str(), o.c_str()) == 0; }
  String &operator+=(const String &o) { s_ += o.s_; return *this; }
  String &operator+=(const char *o) { s_ += o; return *this; }
  String &operator+=(char c) { s_ += c; return *this; }
  String &operator+=(int v) { s_ += std::to_string(v); return *this; }
  bool operator==(const String &o) const { return s_ == o.s_; }
  bool operator==(const char *o) const { return s_ == o; }
  bool operator!=(const String &o) const { return s_ != o.s_; }
  bool operator!=(const char *o) const { return s_ != o; }
  friend String operator+(String a, const String &b) { a += b; return a; }
  friend String operator+(String a, const char *b) { a += b; return a; }
  friend String operator+(const char *a, const String &b) { String r(a); r += b; return r; }
  friend String operator+(String a, char b) { a += b; return a; }
 private:
  std::string s_;
};

class Stream {
 public:
  virtual ~Stream() {}
  virtual int printf(const char *, ...) { return 0; }
  virtual size_t print(const char *) { return 0; }
  virtual size_t println(const char * = "") { return 0; }
  virtual size_t write(const uint8_t *, size_t n) { return n; }
  virtual size_t write(uint8_t) { return 1; }
  virtual int available() { return 0; }
  virtual int read() { return -1; }
  virtual void flush() {}
};

struct SimSerial {
  int printf(const char *, ...) { return 0; }
  size_t print(const char *) { return 0; }
  size_t print(const String &) { return 0; }
  size_t println(const char * = "") { return 0; }
  size_t println(const String &) { return 0; }
  size_t write(const uint8_t *, size_t n) { return n; }
  size_t write(uint8_t) { return 1; }
  void flush() {}
  int available() { return 0; }
  int read() { return -1; }
  explicit operator bool() const { return true; }
};
extern SimSerial Serial;
