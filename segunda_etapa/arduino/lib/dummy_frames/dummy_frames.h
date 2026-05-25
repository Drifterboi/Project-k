#ifndef DUMMY_FRAMES_H
#define DUMMY_FRAMES_H

#include <Arduino.h>
#include "../protocolo/protocolo.h"
#include "../enlace/enlace.h"

namespace DummyFrames {

/**
 * Genera un frame HANDSHAKE del Nano
 * Retorna la trama lista para enviar
 */
Protocolo::Trama crearHandshake() {
  Protocolo::Trama trama;
  trama.direccion = Protocolo::NODO_NANO;
  trama.control = Protocolo::TRAMA_HANDSHAKE;
  trama.seq = 0;
  trama.ack = 0;
  trama.longitud = 0;
  trama.crc = 0;
  return trama;
}

/**
 * Genera un frame de DATOS del Nano
 * @param seq Número de secuencia
 * @param datos Puntero a los datos
 * @param longitud Cantidad de bytes
 */
Protocolo::Trama crearDatos(uint8_t seq, const uint8_t *datos, uint16_t longitud) {
  Protocolo::Trama trama;
  trama.direccion = Protocolo::NODO_NANO;
  trama.control = Protocolo::TRAMA_DATOS;
  trama.seq = seq;
  trama.ack = 0;
  trama.longitud = (longitud > Protocolo::MAX_CARGA_UTIL) ? 
                   Protocolo::MAX_CARGA_UTIL : longitud;
  
  // Copiar datos
  for (uint16_t i = 0; i < trama.longitud; i++) {
    trama.cargaUtil[i] = datos[i];
  }
  
  trama.crc = 0; // Se calcularía en la codificación real
  return trama;
}

/**
 * Genera un frame FIN del Nano
 * @param seq Número de secuencia final
 */
Protocolo::Trama crearFin(uint8_t seq) {
  Protocolo::Trama trama;
  trama.direccion = Protocolo::NODO_NANO;
  trama.control = Protocolo::TRAMA_FIN;
  trama.seq = seq;
  trama.ack = 0;
  trama.longitud = 0;
  trama.crc = 0;
  return trama;
}

/**
 * Simula una secuencia de transmisión del Nano
 * HANDSHAKE -> DATOS (3x) -> FIN
 */
void simularTransmisionNano() {
  Serial.println(F("\n[SIMULACIÓN] Iniciando dummy frames del Nano..."));
  
  // 1. HANDSHAKE
  delay(500);
  Protocolo::Trama handshake = crearHandshake();
  uint8_t bufferTx[Protocolo::TAM_MAX_TRAMA];
  uint16_t longitudSalida;
  
  Enlace::codificar(handshake, bufferTx, Protocolo::TAM_MAX_TRAMA, longitudSalida);
  Serial.write(bufferTx, longitudSalida);
  Serial.println(F("[DUMMY] Enviado HANDSHAKE"));
  
  delay(1000);
  
  // 2. DATOS (3 frames)
  const char *datoDummy = "Hola_desde_Nano_en_Tinkercad";
  for (uint8_t i = 0; i < 3; i++) {
    uint16_t inicio = i * 10;
    uint16_t fin = inicio + 10;
    if (fin > strlen(datoDummy)) fin = strlen(datoDummy);
    
    Protocolo::Trama datos = crearDatos(i, (uint8_t *)(datoDummy + inicio), fin - inicio);
    Enlace::codificar(datos, bufferTx, Protocolo::TAM_MAX_TRAMA, longitudSalida);
    Serial.write(bufferTx, longitudSalida);
    
    Serial.print(F("[DUMMY] Enviado DATOS frame "));
    Serial.println(i);
    
    delay(800);
  }
  
  // 3. FIN
  Protocolo::Trama fin = crearFin(3);
  Enlace::codificar(fin, bufferTx, Protocolo::TAM_MAX_TRAMA, longitudSalida);
  Serial.write(bufferTx, longitudSalida);
  Serial.println(F("[DUMMY] Enviado FIN"));
}

} // namespace DummyFrames

#endif
