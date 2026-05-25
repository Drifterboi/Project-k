// RECEIVER_UNO.ino - Arduino Uno (Receptor)

#include <Arduino.h>
#include <EEPROM.h>
#include <avr/wdt.h>
#include "enlace.h"
#include "protocolo.h"
#include "crc.h"
#include "hamming.h"
#include "fisica_uno.h"

#define SERIAL_BAUD 4800  // Baudrate inicial fijo
#define EEPROM_BAUD_ADDR 0  // Dirección en EEPROM para guardar baudrate

const uint8_t TAM_METADATA_HANDSHAKE = 10;
#define LOG_RX_BYTES 0
#define LOG_TRAMA_DETALLE 0

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
unsigned long ultimoStatusTime = 0;
const unsigned long TIMEOUT_FRAME = 5000;
const unsigned long STATUS_PRINT_INTERVAL = 500;  // Imprimir estado cada 500ms
uint32_t tramasObservadas = 0;
uint32_t erroresDetectados = 0;

// ============ PROTOTIPOS ============
void procesarFrameRecibido(const Enlace::TramaLigera &trama);
void enviarAck(uint8_t ack);
void enviarNack(uint8_t ack);
void imprimirEstado();
void resetReceiver();
void procesarEntradaSerial();
void procesarMetadataHandshake(const Enlace::TramaLigera &trama);
uint32_t calcularProgresoCent();
uint32_t calcularTasaErrorCent();
void imprimirPorcentajeCent(uint32_t valorCent);

// ============ FUNCIONES PARA BAUDRATE DINÁMICO ============
uint16_t leerBaudRateEEPROM() {
  uint8_t byte1 = EEPROM.read(EEPROM_BAUD_ADDR);
  uint8_t byte2 = EEPROM.read(EEPROM_BAUD_ADDR + 1);
  uint16_t baudrate = ((uint16_t)byte1 << 8) | byte2;
  // Si EEPROM está vacío (0xFFFF), devuelve 4800
  return (baudrate == 0xFFFF) ? 4800 : baudrate;
}

void guardarBaudRateEEPROM(uint16_t baudrate) {
  EEPROM.write(EEPROM_BAUD_ADDR, (uint8_t)(baudrate >> 8));
  EEPROM.write(EEPROM_BAUD_ADDR + 1, (uint8_t)(baudrate & 0xFF));
}

void procesarComandoSETBAUD(const char* comando) {
  // Formato: "SETBAUD:9600" o "SETBAUD:4800"
  const char* ptr = strchr(comando, ':');
  if (!ptr) {
    Serial.println(F("[ERROR] Formato inválido. Use: SETBAUD:baudrate"));
    return;
  }
  
  uint16_t nuevoBaud = atoi(ptr + 1);
  if (nuevoBaud == 0) {
    Serial.println(F("[ERROR] Baudrate inválido"));
    return;
  }
  
  guardarBaudRateEEPROM(nuevoBaud);
  Serial.print(F("[OK] Baudrate cambiado a "));
  Serial.println(nuevoBaud);
  Serial.println(F("[INFO] Reiniciando..."));
  delay(100);
  
  // Reset por software usando WDT
  wdt_enable(WDTO_15MS);
  while(1);
}

// ============ SETUP ============
void setup() {
  uint16_t baud = leerBaudRateEEPROM();
  if (baud == 0 || baud == 0xFFFF) baud = SERIAL_BAUD;
  Serial.begin(baud);
  FisicaUno::iniciar(baud);
  delay(1000);

  progreso.tiempoInicio = millis();
  progreso.estado = ESTADO_ESPERANDO;
}

// ============ LOOP PRINCIPAL ============
void loop() {
  // Procesar comandos ASCII desde USB (ej. SETBAUD:xxxx)
  if (Serial.available()) {
    String cmd = Serial.readStringUntil('\n');
    cmd.trim();
    if (cmd.length() > 0) {
      if (cmd.startsWith("SETBAUD:")) {
        procesarComandoSETBAUD(cmd.c_str());
      }
    }
  }

  procesarEntradaSerial();

  // Imprimir estado periódicamente cuando está recibiendo
  if (progreso.estado == ESTADO_RECIBIENDO &&
      (millis() - ultimoStatusTime) > STATUS_PRINT_INTERVAL) {
    imprimirEstado();
    ultimoStatusTime = millis();
  }

  if (progreso.estado == ESTADO_RECIBIENDO &&
      (millis() - ultimoFrameTime) > TIMEOUT_FRAME) {
    erroresDetectados++;
    progreso.estado = ESTADO_ERROR;
    ultimoFrameTime = millis();
  }
}

// ============ PROCESAR ENTRADAS SERIAL ============
void procesarEntradaSerial() {
  while (FisicaUno::puedeLeerByte()) {
    uint8_t byteRecibido = 0;
    bool errorIrrecuperable = false;

    if (!FisicaUno::leerByte(byteRecibido, errorIrrecuperable)) {
      if (errorIrrecuperable) {
        Serial.println(F("[ERROR] Error irrecuperable Hamming"));
        erroresDetectados++;
        enviarNack(ventanaRx.ackEsperado());
      }
      return;
    }

    if (LOG_RX_BYTES) {
      Serial.print(F("[RX] Byte: 0x"));
      Serial.println(byteRecibido, HEX);
    }

    ultimoFrameTime = millis();

    Enlace::TramaLigera trama;
    Enlace::EstadoDecodificacion error = Enlace::DECODIFICACION_OK;
    Enlace::EstadoParserStreaming estadoParser =
      parserRx.procesarByte(byteRecibido, trama, error);

    if (estadoParser == Enlace::PARSER_TRAMA_COMPLETA) {
      tramasObservadas++;
      if (LOG_TRAMA_DETALLE) {
        Serial.print(F("[TRAMA] Dir: 0x"));
        Serial.print(trama.direccion, HEX);
        Serial.print(F(" Ctrl: 0x"));
        Serial.print(trama.control, HEX);
        Serial.print(F(" Seq: "));
        Serial.print(trama.seq);
        Serial.print(F(" Long: "));
        Serial.println(trama.longitud);
      }
      procesarFrameRecibido(trama);
    } else if (estadoParser == Enlace::PARSER_TRAMA_INVALIDA) {
      Serial.println(F("[ERROR] Trama inválida"));
      erroresDetectados++;
      enviarNack(ventanaRx.ackEsperado());
    }
  }
}

// ============ PROCESAR FRAME RECIBIDO ============
void procesarFrameRecibido(const Enlace::TramaLigera &trama) {
  if (LOG_TRAMA_DETALLE) {
    Serial.print(F("[VALIDAR] Dirección recibida: 0x"));
    Serial.print(trama.direccion, HEX);
    Serial.print(F(" (NODO_UNO=0x"));
    Serial.print(Protocolo::NODO_UNO, HEX);
    Serial.print(F(", BROADCAST=0x"));
    Serial.print(Protocolo::NODO_BROADCAST, HEX);
    Serial.println(F(")"));
  }

  if (trama.direccion != Protocolo::NODO_UNO &&
      trama.direccion != Protocolo::NODO_BROADCAST) {
    Serial.println(F("[RECHAZO] Dirección no coincide"));
    return;
  }

  switch (trama.control) {
    case Protocolo::TRAMA_HANDSHAKE:
      Serial.println(F("[HANDSHAKE] Recibido"));
      if (progreso.estado == ESTADO_COMPLETO) {
        resetReceiver(false);
      }
      if (progreso.estado == ESTADO_ESPERANDO) {
        Serial.println(F("[HANDSHAKE] Estado OK, procesando..."));
        progreso.estado = ESTADO_RECIBIENDO;
        progreso.framesRecibidos = 0;
        progreso.bytesRecibidos = 0;
        procesarMetadataHandshake(trama);
        tramasObservadas = 0;
        erroresDetectados = 0;
        enviarAck(0);
      } else {
        Serial.print(F("[HANDSHAKE] Rechazado - Estado: "));
        Serial.println(progreso.estado);
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
          Serial.print(F("[FILE:"));
          Serial.print(trama.seq);
          Serial.print(':');
          Serial.print(trama.payloadMuestraLongitud);
          Serial.print(F("] "));
          for (uint16_t _fi = 0; _fi < trama.payloadMuestraLongitud; _fi++) {
            if (_fi > 0) Serial.print(' ');
            if (trama.payloadMuestra[_fi] < 0x10) Serial.print('0');
            Serial.print(trama.payloadMuestra[_fi], HEX);
          }
          Serial.println();
          enviarAck(ventanaRx.ackEsperado());
        } else {
          enviarAck(ventanaRx.ackEsperado());
        }
      }
      break;

    case Protocolo::TRAMA_FIN:
      if (progreso.estado == ESTADO_RECIBIENDO) {
        progreso.estado = ESTADO_COMPLETO;
        Serial.println(F("[TRANSMISIÓN FINALIZADA]"));
        enviarAck(ventanaRx.ackEsperado());
      }
      break;

    case Protocolo::TRAMA_ERROR:
      progreso.estado = ESTADO_ERROR;
      erroresDetectados++;
      break;

    default:
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
    if (!FisicaUno::escribirBuffer(bufferTx, longitudSalida)) {
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
    if (!FisicaUno::escribirBuffer(bufferTx, longitudSalida)) {
      Serial.println(F("[ERROR] No se pudo enviar NACK por capa fisica."));
      return;
    }

    Serial.print(F("[NACK] Enviado nack="));
    Serial.println(ack);
  } else {
    Serial.println(F("[ERROR] No se pudo codificar NACK."));
  }
}

// ============ PROCESAR METADATA HANDSHAKE ============
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

// ============ CALCULAR PROGRESO ============
uint32_t calcularProgresoCent() {
  if (progreso.bytesTotales == 0) {
    return 0;
  }

  uint32_t valor = (progreso.bytesRecibidos * 10000UL) / progreso.bytesTotales;
  return valor > 10000UL ? 10000UL : valor;
}

// ============ CALCULAR TASA DE ERROR ============
uint32_t calcularTasaErrorCent() {
  uint32_t total = tramasObservadas + erroresDetectados;
  if (total == 0) {
    return 0;
  }

  return (erroresDetectados * 10000UL) / total;
}

// ============ IMPRIMIR PORCENTAJE ============
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
  Serial.print(F("Tasa Error: "));
  imprimirPorcentajeCent(calcularTasaErrorCent());
  Serial.println(F("%"));
  Serial.print(F("Tiempo: "));
  Serial.print(tiempoTranscurrido);
  Serial.println(F("s"));
}

// ============ RESET RECEIVER ============
// Usar parametro binario para indicar si el parser debe ser reiniciado
void resetReceiver(bool resetParser) {
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
  if (resetParser) {
    parserRx.reiniciar();
  }
}
