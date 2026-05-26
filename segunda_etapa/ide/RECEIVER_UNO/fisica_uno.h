#ifndef FISICA_UNO_H
#define FISICA_UNO_H

#include <Arduino.h>

namespace FisicaUno {

const uint8_t PIN_RX = 11;  // ← INVERTIDO para Uno (escucha del Nano en pin 11)
const uint8_t PIN_TX = 10;  // ← INVERTIDO para Uno (envía a Nano en pin 10)

void iniciar(uint32_t baudrate);
void configurarHamming(bool activo);
bool hammingActivo();
bool escribirByte(uint8_t dato);
bool escribirBuffer(const uint8_t *datos, uint16_t longitud);
bool puedeLeerByte();
bool leerByte(uint8_t &dato, bool &errorIrrecuperable);

} // namespace FisicaUno

#endif
