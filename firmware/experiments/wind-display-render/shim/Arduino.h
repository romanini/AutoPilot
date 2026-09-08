#pragma once
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <cmath>
#include <cstdio>
#include <algorithm>
#include "Print.h"
#define PROGMEM
#define pgm_read_byte(a) (*(const unsigned char *)(a))
#define pgm_read_word(a) (*(const unsigned short *)(a))
#define pgm_read_dword(a) (*(const unsigned long *)(a))
#define pgm_read_pointer(a) ((void *)(*(const uintptr_t *)(a)))
typedef bool boolean;
class __FlashStringHelper;
#include <string>
class String : public std::string {
public:
  String(const char *s = "") : std::string(s) {}
  const char *c_str() const { return std::string::c_str(); }
};
inline double radians(double d) { return d * 3.14159265358979323846 / 180.0; }

typedef uint8_t byte;
#ifndef PI
#define PI 3.1415926535897932384626433832795
#endif
#ifndef min
#define min(a,b) ((a)<(b)?(a):(b))
#endif
#ifndef max
#define max(a,b) ((a)>(b)?(a):(b))
#endif
#define _swap_int16_t(a, b) { int16_t t = a; a = b; b = t; }
inline void delay(unsigned long) {}
inline void delayMicroseconds(unsigned long) {}
inline unsigned long millis() { return 0; }
inline void pinMode(int, int) {}
inline void digitalWrite(int, int) {}
inline int digitalRead(int) { return 1; }
#define OUTPUT 1
#define INPUT 0
#define INPUT_PULLUP 2
#define HIGH 1
#define LOW 0
