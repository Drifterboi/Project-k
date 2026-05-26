#include "fisica_uno.h"
#include "hamming.h"
#include <SoftwareSerial.h>

namespace FisicaUno {

static bool hammingActivo_ = true;
static SoftwareSerial enlaceSerial(PIN_RX, PIN_TX);  // RX=11, TX=10 (cruzado para Uno)

// IMPORTANTE: SoftwareSerial SIEMPRE usa 4800 bps para comunicación entre Arduinos
// Esto es más confiable que baudrates mayores con SoftwareSerial
const uint32_t SOFTWARESERIAL_BAUDRATE = 4800;

void iniciar(uint32_t baudrate) {
  // Ignoramos el parámetro baudrate y siempre usamos 4800 para SoftwareSerial
  // Esto asegura que Arduino↔Arduino sea confiable
  enlaceSerial.begin(SOFTWARESERIAL_BAUDRATE);
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

  Serial.print(F("[UNO TX] Escribiendo "));
  Serial.print(longitud);
  Serial.println(F(" bytes"));

  for (uint16_t i = 0; i < longitud; i++) {
    if (!escribirByte(datos[i])) {
      Serial.print(F("[UNO TX ERROR] Fallo en byte "));
      Serial.println(i);
      return false;
    }
  }

  Serial.println(F("[UNO TX] Escritura completada"));
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

} // namespace FisicaUno
