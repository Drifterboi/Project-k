#ifndef FISICA_H
#define FISICA_H

#include <Arduino.h>

namespace Fisica {

const uint8_t PIN_RX = 11;
const uint8_t PIN_TX = 10;

void iniciar(uint32_t baudrate);
void configurarHamming(bool activo);
bool hammingActivo();
bool escribirByte(uint8_t dato);
bool escribirBuffer(const uint8_t *datos, uint16_t longitud);
bool puedeLeerByte();
bool leerByte(uint8_t &dato, bool &errorIrrecuperable);

} // namespace Fisica

#endif
