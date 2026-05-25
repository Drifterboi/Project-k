#ifndef ARDUINO_H
#define ARDUINO_H

#include <stdint.h>

typedef uint8_t byte;

inline uint8_t highByte(uint16_t x) {
    return (x >> 8) & 0xFF;
}

inline uint8_t lowByte(uint16_t x) {
    return x & 0xFF;
}

inline uint16_t word(uint8_t h, uint8_t l) {
    return ((uint16_t)h << 8) | l;
}

#endif