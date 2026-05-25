#ifndef HAMMING_H
#define HAMMING_H

#include <Arduino.h>

namespace Hamming {

enum EstadoHamming : uint8_t {
  HAMMING_OK = 0,
  HAMMING_CORREGIDO = 1,
  HAMMING_NO_CORREGIBLE = 2
};

uint8_t codificarNibble(uint8_t nibble);

uint8_t decodificarNibble(uint8_t codigo,
                          EstadoHamming &estado);

bool codificarBuffer(const uint8_t *entrada,
                     uint16_t longitudEntrada,
                     uint8_t *salida,
                     uint16_t capacidadSalida,
                     uint16_t &longitudSalida);

bool decodificarBuffer(const uint8_t *entrada,
                       uint16_t longitudEntrada,
                       uint8_t *salida,
                       uint16_t capacidadSalida,
                       uint16_t &longitudSalida,
                       uint16_t &correcciones,
                       bool &errorIrrecuperable);

} // namespace Hamming

#endif