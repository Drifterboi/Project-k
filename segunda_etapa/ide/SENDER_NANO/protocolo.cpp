#include "protocolo.h"

namespace Protocolo {

bool esTipoValido(uint8_t control) {
  return control == TRAMA_DATOS ||
         control == TRAMA_ACK ||
         control == TRAMA_NACK ||
         control == TRAMA_HANDSHAKE ||
         control == TRAMA_FIN ||
         control == TRAMA_ERROR;
}

bool esConfirmacion(uint8_t control) {
  return control == TRAMA_ACK || control == TRAMA_NACK;
}

bool requiereCarga(uint8_t control) {
  return control == TRAMA_DATOS || control == TRAMA_HANDSHAKE || control == TRAMA_ERROR;
}

} // namespace Protocolo
