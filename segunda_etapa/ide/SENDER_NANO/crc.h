#ifndef CRC_H
#define CRC_H

#include <Arduino.h>

uint16_t calcularCrc16(const uint8_t *datos, uint16_t longitud);
uint16_t crc16Inicial();
uint16_t actualizarCrc16(uint16_t crc, uint8_t dato);

#endif
