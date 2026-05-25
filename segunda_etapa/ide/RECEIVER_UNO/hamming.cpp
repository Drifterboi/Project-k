#include "hamming.h"

namespace Hamming {

uint8_t codificarNibble(uint8_t nibble) {
  nibble &= 0x0F;

  uint8_t d1 = (nibble >> 0) & 1;
  uint8_t d2 = (nibble >> 1) & 1;
  uint8_t d3 = (nibble >> 2) & 1;
  uint8_t d4 = (nibble >> 3) & 1;

  uint8_t p1 = d1 ^ d2 ^ d4;
  uint8_t p2 = d1 ^ d3 ^ d4;
  uint8_t p3 = d2 ^ d3 ^ d4;

  uint8_t codigo = 0;

  codigo |= (p1 << 0);
  codigo |= (p2 << 1);
  codigo |= (d1 << 2);
  codigo |= (p3 << 3);
  codigo |= (d2 << 4);
  codigo |= (d3 << 5);
  codigo |= (d4 << 6);

  return codigo & 0x7F;
}

uint8_t decodificarNibble(uint8_t codigo,
                          EstadoHamming &estado) {
  codigo &= 0x7F;

  bool banderaCorreccion = false;

  while (true) {
    uint8_t b1 = (codigo >> 0) & 1;
    uint8_t b2 = (codigo >> 1) & 1;
    uint8_t b3 = (codigo >> 2) & 1;
    uint8_t b4 = (codigo >> 3) & 1;
    uint8_t b5 = (codigo >> 4) & 1;
    uint8_t b6 = (codigo >> 5) & 1;
    uint8_t b7 = (codigo >> 6) & 1;

    uint8_t s1 = b1 ^ b3 ^ b5 ^ b7;
    uint8_t s2 = b2 ^ b3 ^ b6 ^ b7;
    uint8_t s3 = b4 ^ b5 ^ b6 ^ b7;

    uint8_t sindrome = s1 | (s2 << 1) | (s3 << 2);

    if (sindrome == 0) {
      estado = banderaCorreccion ? HAMMING_CORREGIDO : HAMMING_OK;
      break;
    }

    if (banderaCorreccion) {
      estado = HAMMING_NO_CORREGIBLE;
      break;
    }

    codigo ^= (1 << (sindrome - 1));
    banderaCorreccion = true;
  }

  uint8_t d1 = (codigo >> 2) & 1;
  uint8_t d2 = (codigo >> 4) & 1;
  uint8_t d3 = (codigo >> 5) & 1;
  uint8_t d4 = (codigo >> 6) & 1;

  return (d4 << 3) | (d3 << 2) | (d2 << 1) | d1;
}

bool codificarBuffer(const uint8_t *entrada,
                     uint16_t longitudEntrada,
                     uint8_t *salida,
                     uint16_t capacidadSalida,
                     uint16_t &longitudSalida) {
  longitudSalida = 0;

  if (entrada == nullptr || salida == nullptr) {
    return false;
  }

  if (capacidadSalida < longitudEntrada * 2) {
    return false;
  }

  for (uint16_t i = 0; i < longitudEntrada; i++) {
    uint8_t nibbleAlto = (entrada[i] >> 4) & 0x0F;
    uint8_t nibbleBajo = entrada[i] & 0x0F;

    salida[longitudSalida++] = codificarNibble(nibbleAlto);
    salida[longitudSalida++] = codificarNibble(nibbleBajo);
  }

  return true;
}

bool decodificarBuffer(const uint8_t *entrada,
                       uint16_t longitudEntrada,
                       uint8_t *salida,
                       uint16_t capacidadSalida,
                       uint16_t &longitudSalida,
                       uint16_t &correcciones,
                       bool &errorIrrecuperable) {
  longitudSalida = 0;
  correcciones = 0;
  errorIrrecuperable = false;

  if (entrada == nullptr || salida == nullptr) {
    return false;
  }

  if (longitudEntrada % 2 != 0) {
    errorIrrecuperable = true;
    return false;
  }

  if (capacidadSalida < longitudEntrada / 2) {
    return false;
  }

  for (uint16_t i = 0; i < longitudEntrada; i += 2) {
    EstadoHamming estadoAlto = HAMMING_OK;
    EstadoHamming estadoBajo = HAMMING_OK;

    uint8_t nibbleAlto = decodificarNibble(entrada[i], estadoAlto);
    uint8_t nibbleBajo = decodificarNibble(entrada[i + 1], estadoBajo);

    if (estadoAlto == HAMMING_NO_CORREGIBLE ||
        estadoBajo == HAMMING_NO_CORREGIBLE) {
      errorIrrecuperable = true;
      return false;
    }

    if (estadoAlto == HAMMING_CORREGIDO) {
      correcciones++;
    }

    if (estadoBajo == HAMMING_CORREGIDO) {
      correcciones++;
    }

    salida[longitudSalida++] = (nibbleAlto << 4) | nibbleBajo;
  }

  return true;
}

} // namespace Hamming