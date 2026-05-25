#include <Arduino.h>
#include "../../lib/enlace/enlace.h"
#include "../../lib/protocolo/protocolo.h"
#include "../../lib/crc/crc.h"
#include "../../lib/hamming/hamming.h"
#include "../../lib/fisica/fisica.h"

#ifndef SERIAL_BAUD
#define SERIAL_BAUD 9600
#endif

const uint8_t TAM_METADATA_HANDSHAKE = 10;

// ============ ESTADOS DEL RECEIVER ============
enum EstadoReceiver : uint8_t {
  ESTADO_ESPERANDO = 0,
  ESTADO_RECIBIENDO = 1,
  ESTADO_COMPLETO = 2,
  ESTADO_ERROR = 3
};

// ============ ESTRUCTURA DE PROGRESO ============
struct Progreso {
  EstadoReceiver estado;
  uint16_t framesRecibidos;
  uint16_t framesTotales;
  uint32_t bytesRecibidos;
  uint32_t bytesTotales;
  uint8_t tasaError;
  uint32_t tiempoInicio;
};

// ============ VARIABLES GLOBALES ============
Progreso progreso = {ESTADO_ESPERANDO, 0, 0, 0, 0, 0, 0};
Enlace::VentanaRecepcion ventanaRx(4);
Enlace::ParserStreaming parserRx;
unsigned long ultimoFrameTime = 0;
const unsigned long TIMEOUT_FRAME = 2000; // ms
uint32_t tramasObservadas = 0;
uint32_t erroresDetectados = 0;

// ============ PROTOTIPOS ============
void procesarFrameRecibido(const Enlace::TramaLigera &trama);
void enviarAck(uint8_t ack);
void enviarNack(uint8_t ack);
void imprimirEstado();
void resetReceiver();
void procesarEntradaSerial();
void procesarCadenaTrama();
void simularTransmision();
void procesarMetadataHandshake(const Enlace::TramaLigera &trama);
uint32_t calcularProgresoCent();
uint32_t calcularTasaErrorCent();
void imprimirPorcentajeCent(uint32_t valorCent);

// ============ SETUP ============
void setup() {
  Serial.begin(SERIAL_BAUD);
  Fisica::iniciar(SERIAL_BAUD);
  delay(1000);

  Serial.println(F("\n╔══════════════════════════════════════╗"));
  Serial.println(F("║  RECEIVER (Arduino Uno) - Redes     ║"));
  Serial.println(F("║  Estado: ESPERANDO                  ║"));
  Serial.println(F("╚══════════════════════════════════════╝"));
  Serial.println(F(""));
  Serial.println(F("Comandos de prueba (opcional):"));
  Serial.println(F("  's' - Simular trama de datos"));
  Serial.println(F("  'r' - Reiniciar receiver"));
  Serial.println(F("  'e' - Mostrar estado"));
  Serial.println(F(""));

  progreso.tiempoInicio = millis();
  progreso.estado = ESTADO_ESPERANDO;
}

// ============ LOOP PRINCIPAL ============
void loop() {
  procesarEntradaSerial();

  if (progreso.estado == ESTADO_RECIBIENDO &&
      (millis() - ultimoFrameTime) > TIMEOUT_FRAME) {
    Serial.println(F("[ERROR] Timeout - Transacción abortada"));
    erroresDetectados++;
    progreso.estado = ESTADO_ERROR;
    imprimirEstado();
    ultimoFrameTime = millis();
  }
}

// ============ PROCESAR ENTRADAS SERIAL ============
void procesarEntradaSerial() {
  while (Fisica::puedeLeerByte()) {
    uint8_t byteRecibido = 0;
    bool errorIrrecuperable = false;

    if (!Fisica::leerByte(byteRecibido, errorIrrecuperable)) {
      if (errorIrrecuperable) {
        Serial.println(F("[ERROR] Hamming: error no corregible"));
        erroresDetectados++;
        enviarNack(ventanaRx.ackEsperado());
      }
      return;
    }

    ultimoFrameTime = millis();

    Enlace::TramaLigera trama;
    Enlace::EstadoDecodificacion error = Enlace::DECODIFICACION_OK;
    Enlace::EstadoParserStreaming estadoParser =
      parserRx.procesarByte(byteRecibido, trama, error);

    if (estadoParser == Enlace::PARSER_TRAMA_COMPLETA) {
      tramasObservadas++;
      Serial.print(F("[TRAMA RECIBIDA] Tipo: 0x"));
      Serial.print(trama.control, HEX);
      Serial.print(F(" Seq: "));
      Serial.println(trama.seq);
      procesarFrameRecibido(trama);
    } else if (estadoParser == Enlace::PARSER_TRAMA_INVALIDA) {
      erroresDetectados++;
      Serial.print(F("[ERROR] Decodificacion: "));
      Serial.println(error);
      enviarNack(ventanaRx.ackEsperado());
    }
  }
}
// ============ PROCESAR FRAME RECIBIDO ============
void procesarFrameRecibido(const Enlace::TramaLigera &trama) {
  if (trama.direccion != Protocolo::NODO_UNO &&
      trama.direccion != Protocolo::NODO_BROADCAST) {
    Serial.println(F("[DESCARTE] No dirigido a este nodo"));
    return;
  }

  switch (trama.control) {
    case Protocolo::TRAMA_HANDSHAKE:
      if (progreso.estado == ESTADO_ESPERANDO) {
        progreso.estado = ESTADO_RECIBIENDO;
        progreso.framesRecibidos = 0;
        progreso.bytesRecibidos = 0;
        procesarMetadataHandshake(trama);
        tramasObservadas = 0;
        erroresDetectados = 0;
        Serial.println(F("[HANDSHAKE] Conexión iniciada. Esperando datos..."));
        imprimirEstado();
        enviarAck(0);
      }
      break;

    case Protocolo::TRAMA_DATOS:
      if (progreso.estado == ESTADO_RECIBIENDO) {
        if (ventanaRx.acepta(trama.seq)) {
          progreso.framesRecibidos++;
          progreso.bytesRecibidos += trama.longitud;
          ventanaRx.registrarRecibida(trama.seq);
          Serial.print(F("[DATOS] Frame "));
          Serial.print(trama.seq);
          Serial.print(F(" recibido, bytes="));
          Serial.println(trama.longitud);
          imprimirEstado();
          enviarAck(ventanaRx.ackEsperado());
        } else {
          Serial.println(F("[RETRANSMISIÓN] Secuencia fuera de ventana o duplicada."));
          enviarAck(ventanaRx.ackEsperado());
        }
      }
      break;

    case Protocolo::TRAMA_FIN:
      if (progreso.estado == ESTADO_RECIBIENDO) {
        progreso.estado = ESTADO_COMPLETO;
        Serial.println(F("[TRANSMISIÓN FINALIZADA]"));
        Serial.print(F("Total frames: "));
        Serial.println(progreso.framesRecibidos);
        Serial.print(F("Total bytes: "));
        Serial.println(progreso.bytesRecibidos);
        imprimirEstado();
        enviarAck(ventanaRx.ackEsperado());
      }
      break;

    case Protocolo::TRAMA_ERROR:
      progreso.estado = ESTADO_ERROR;
      erroresDetectados++;
      Serial.println(F("[ERROR REMOTO] El transmisor indicó error."));
      imprimirEstado();
      break;

    default:
      Serial.println(F("[DESCONOCIDA] Tipo de trama no reconocida"));
      break;
  }
}

// ============ ENVIAR ACK ============
void enviarAck(uint8_t ack) {
  Protocolo::Trama ackTrama = Enlace::crearAck(Protocolo::NODO_NANO, ack);
  uint8_t bufferTx[Protocolo::TAM_MAX_TRAMA];
  uint16_t longitudSalida;
  Enlace::EstadoCodificacion estado = Enlace::codificar(
      ackTrama, bufferTx, Protocolo::TAM_MAX_TRAMA, longitudSalida);

  if (estado == Enlace::CODIFICACION_OK) {
    if (!Fisica::escribirBuffer(bufferTx, longitudSalida)) {
      Serial.println(F("[ERROR] No se pudo enviar ACK por capa fisica."));
      return;
    }

    Serial.print(F("[ACK] Enviado ack="));
    Serial.println(ack);
  } else {
    Serial.println(F("[ERROR] No se pudo codificar ACK."));
  }
}

// ============ ENVIAR NACK ============
void enviarNack(uint8_t ack) {
  Protocolo::Trama nackTrama = Enlace::crearNack(Protocolo::NODO_NANO, ack);
  uint8_t bufferTx[Protocolo::TAM_MAX_TRAMA];
  uint16_t longitudSalida;
  Enlace::EstadoCodificacion estado = Enlace::codificar(
      nackTrama, bufferTx, Protocolo::TAM_MAX_TRAMA, longitudSalida);

  if (estado == Enlace::CODIFICACION_OK) {
    if (!Fisica::escribirBuffer(bufferTx, longitudSalida)) {
      Serial.println(F("[ERROR] No se pudo enviar NACK por capa fisica."));
      return;
    }

    Serial.print(F("[NACK] Enviado nack="));
    Serial.println(ack);
  } else {
    Serial.println(F("[ERROR] No se pudo codificar NACK."));
  }
}

void procesarMetadataHandshake(const Enlace::TramaLigera &trama) {
  if (trama.longitud >= TAM_METADATA_HANDSHAKE) {
    progreso.bytesTotales = ((uint32_t)trama.payloadMuestra[0] << 24) |
                             ((uint32_t)trama.payloadMuestra[1] << 16) |
                             ((uint32_t)trama.payloadMuestra[2] << 8) |
                             (uint32_t)trama.payloadMuestra[3];

    progreso.framesTotales = ((uint16_t)trama.payloadMuestra[4] << 8) |
                              (uint16_t)trama.payloadMuestra[5];

    uint8_t tamVentana = trama.payloadMuestra[8];
    if (tamVentana < 1) tamVentana = 1;
    if (tamVentana > 5) tamVentana = 5;
    ventanaRx = Enlace::VentanaRecepcion(tamVentana);
  } else {
    progreso.framesTotales = 0;
    progreso.bytesTotales = 0;
  }

  ventanaRx.reiniciar();
  progreso.tasaError = 0;
  progreso.tiempoInicio = millis();
}

uint32_t calcularProgresoCent() {
  if (progreso.bytesTotales == 0) {
    return 0;
  }

  uint32_t valor = (progreso.bytesRecibidos * 10000UL) / progreso.bytesTotales;
  return valor > 10000UL ? 10000UL : valor;
}

uint32_t calcularTasaErrorCent() {
  uint32_t total = tramasObservadas + erroresDetectados;
  if (total == 0) {
    return 0;
  }

  return (erroresDetectados * 10000UL) / total;
}

void imprimirPorcentajeCent(uint32_t valorCent) {
  Serial.print(valorCent / 100);
  Serial.print(F("."));
  uint8_t decimales = valorCent % 100;
  if (decimales < 10) {
    Serial.print(F("0"));
  }
  Serial.print(decimales);
}

// ============ IMPRIMIR ESTADO ============
void imprimirEstado() {
  const char *estadoStr[] = {"ESPERANDO", "RECIBIENDO", "COMPLETO", "ERROR"};
  uint32_t tiempoTranscurrido = (millis() - progreso.tiempoInicio) / 1000;

  Serial.println(F("\n╔════════════════════════════════════╗"));
  Serial.println(F("║       ESTADO DEL RECEIVER          ║"));
  Serial.println(F("╚════════════════════════════════════╝"));
  Serial.print(F("Estado: "));
  Serial.println(estadoStr[progreso.estado]);
  Serial.print(F("Frames: "));
  Serial.print(progreso.framesRecibidos);
  Serial.print(F("/"));
  Serial.println(progreso.framesTotales);
  Serial.print(F("Bytes: "));
  Serial.print(progreso.bytesRecibidos);
  Serial.print(F("/"));
  Serial.println(progreso.bytesTotales);
  Serial.print(F("Progreso: "));
  imprimirPorcentajeCent(calcularProgresoCent());
  Serial.println(F("%"));
  Serial.print(F("Tasa Error: "));
  imprimirPorcentajeCent(calcularTasaErrorCent());
  Serial.println(F("%"));
  Serial.print(F("Tiempo: "));
  Serial.print(tiempoTranscurrido);
  Serial.println(F("s\n"));
}

// ============ RESET RECEIVER ============
void resetReceiver() {
  progreso.estado = ESTADO_ESPERANDO;
  progreso.framesRecibidos = 0;
  progreso.framesTotales = 0;
  progreso.bytesRecibidos = 0;
  progreso.bytesTotales = 0;
  progreso.tasaError = 0;
  progreso.tiempoInicio = millis();
  tramasObservadas = 0;
  erroresDetectados = 0;
  ventanaRx.reiniciar();
  parserRx.reiniciar();
}

// ============ SIMULAR TRANSMISIÓN INTERNA ============
void simularTransmision() {
  Enlace::TramaLigera handshake = {};
  handshake.direccion = Protocolo::NODO_UNO;
  handshake.control = Protocolo::TRAMA_HANDSHAKE;
  handshake.longitud = TAM_METADATA_HANDSHAKE;
  handshake.payloadMuestraLongitud = TAM_METADATA_HANDSHAKE;
  handshake.payloadMuestra[3] = 30;
  handshake.payloadMuestra[5] = 3;
  handshake.payloadMuestra[7] = 10;
  handshake.payloadMuestra[8] = 3;
  procesarFrameRecibido(handshake);

  uint8_t seq = 0;
  for (uint8_t i = 0; i < 3; i++) {
    Enlace::TramaLigera datos = {};
    datos.direccion = Protocolo::NODO_UNO;
    datos.control = Protocolo::TRAMA_DATOS;
    datos.seq = seq++;
    datos.longitud = 10;
    procesarFrameRecibido(datos);
  }

  Enlace::TramaLigera fin = {};
  fin.direccion = Protocolo::NODO_UNO;
  fin.control = Protocolo::TRAMA_FIN;
  fin.seq = seq;
  procesarFrameRecibido(fin);
}
