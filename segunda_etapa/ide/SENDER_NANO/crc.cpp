#include "crc.h"

uint16_t crc16Inicial() {
  return 0xFFFF;
}

uint16_t actualizarCrc16(uint16_t crc, uint8_t dato) {
  crc ^= static_cast<uint16_t>(dato) << 8;

  for (uint8_t bit = 0; bit < 8; bit++) {
    if (crc & 0x8000) {
      crc = static_cast<uint16_t>((crc << 1) ^ 0x1021);
    } else {
      crc = static_cast<uint16_t>(crc << 1);
    }
  }

  return crc;
}

uint16_t calcularCrc16(const uint8_t *datos, uint16_t longitud) {
  uint16_t crc = crc16Inicial();

  for (uint16_t i = 0; i < longitud; i++) {
    crc = actualizarCrc16(crc, datos[i]);
  }

  return crc;
}
