#ifndef PROTOCOLO_H
#define PROTOCOLO_H

#include <Arduino.h>

namespace Protocolo {

const uint8_t FLAG = 0x7E; // 01111110

const uint8_t NODO_NANO = 0x01;
const uint8_t NODO_UNO = 0x02;
const uint8_t NODO_BROADCAST = 0xFF;

const uint16_t MAX_CARGA_UTIL = 64;
const uint16_t MAX_CARGA_UTIL_PROYECTO = 1000;
const uint8_t TAM_LONGITUD = 2;
const uint8_t TAM_CRC = 2;
const uint8_t TAM_ENCABEZADO = 1 + 1 + 1 + 1 + 1 + TAM_LONGITUD;
const uint8_t TAM_CONTROL_FINAL = TAM_CRC + 1;
const uint16_t TAM_MAX_TRAMA = TAM_ENCABEZADO + MAX_CARGA_UTIL + TAM_CONTROL_FINAL;

enum TipoTrama : uint8_t {
  TRAMA_DATOS = 0x01,
  TRAMA_ACK = 0x02,
  TRAMA_NACK = 0x03,
  TRAMA_HANDSHAKE = 0x04,
  TRAMA_FIN = 0x05,
  TRAMA_ERROR = 0x06
};

struct Trama {
  uint8_t direccion;
  uint8_t control;
  uint8_t seq;
  uint8_t ack;
  uint16_t longitud;
  uint8_t cargaUtil[MAX_CARGA_UTIL];
  uint16_t crc;
};

bool esTipoValido(uint8_t control);
bool esConfirmacion(uint8_t control);
bool requiereCarga(uint8_t control);

} // namespace Protocolo

#endif
