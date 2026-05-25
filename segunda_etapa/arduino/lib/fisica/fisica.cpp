#include "fisica.h"
#include "../hamming/hamming.h"
#include <SoftwareSerial.h>

namespace Fisica {

static bool hammingActivo_ = true;
static SoftwareSerial enlaceSerial(PIN_RX, PIN_TX);

void iniciar(uint32_t baudrate) {
  enlaceSerial.begin(baudrate);
}

void configurarHamming(bool activo) {
  hammingActivo_ = activo;
}

bool hammingActivo() {
  return hammingActivo_;
}

bool escribirByte(uint8_t dato) {
  if (!hammingActivo_) {
    return enlaceSerial.write(dato) == 1;
  }

  const uint8_t alto = Hamming::codificarNibble((dato >> 4) & 0x0F);
  const uint8_t bajo = Hamming::codificarNibble(dato & 0x0F);
  return enlaceSerial.write(alto) == 1 && enlaceSerial.write(bajo) == 1;
}

bool escribirBuffer(const uint8_t *datos, uint16_t longitud) {
  if (datos == nullptr && longitud > 0) {
    return false;
  }

  for (uint16_t i = 0; i < longitud; i++) {
    if (!escribirByte(datos[i])) {
      return false;
    }
  }

  return true;
}

bool puedeLeerByte() {
  return hammingActivo_ ? enlaceSerial.available() >= 2 : enlaceSerial.available() >= 1;
}

bool leerByte(uint8_t &dato, bool &errorIrrecuperable) {
  errorIrrecuperable = false;

  if (!puedeLeerByte()) {
    return false;
  }

  if (!hammingActivo_) {
    dato = static_cast<uint8_t>(enlaceSerial.read());
    return true;
  }

  Hamming::EstadoHamming estadoAlto = Hamming::HAMMING_OK;
  Hamming::EstadoHamming estadoBajo = Hamming::HAMMING_OK;
  const uint8_t alto = Hamming::decodificarNibble(static_cast<uint8_t>(enlaceSerial.read()), estadoAlto);
  const uint8_t bajo = Hamming::decodificarNibble(static_cast<uint8_t>(enlaceSerial.read()), estadoBajo);

  if (estadoAlto == Hamming::HAMMING_NO_CORREGIBLE ||
      estadoBajo == Hamming::HAMMING_NO_CORREGIBLE) {
    errorIrrecuperable = true;
    return false;
  }

  dato = static_cast<uint8_t>((alto << 4) | bajo);
  return true;
}

} // namespace Fisica
