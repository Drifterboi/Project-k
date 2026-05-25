#ifndef FISICA_UNO_H
#define FISICA_UNO_H

#include <Arduino.h>

namespace FisicaUno {

const uint8_t PIN_RX = 10;  // Recibe del NANO TX (pin 10 del NANO)
const uint8_t PIN_TX = 11;  // Envía al NANO RX (pin 11 del NANO)

void iniciar(uint32_t baudrate);
void configurarHamming(bool activo);
bool hammingActivo();
bool escribirByte(uint8_t dato);
bool escribirBuffer(const uint8_t *datos, uint16_t longitud);
bool puedeLeerByte();
bool leerByte(uint8_t &dato, bool &errorIrrecuperable);

} // namespace FisicaUno

#endif
