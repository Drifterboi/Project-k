// SENDER_NANO.ino - Arduino Nano (Transmisor)

#include <Arduino.h>
#include <EEPROM.h>
#include <avr/wdt.h>
#include "enlace.h"
#include "protocolo.h"
#include "crc.h"
#include "dummy_frames.h"
#include "hamming.h"
#include "fisica.h"

#define SERIAL_BAUD 4800  // Baudrate inicial fijo
#define EEPROM_BAUD_ADDR 0  // Dirección en EEPROM para guardar baudrate

const uint8_t TAM_METADATA_HANDSHAKE = 10;

// ==== Variables para parsing de instrucciones enviadas por capa aplicación ====
#define MAX_COMMAND_LEN   128
#define MAX_CHUNK_SIZE    1000      // Maximo payload local; el archivo completo queda en Python.
#define PROGRESO_ARCHIVO_STEP 100
#define TIMEOUT_CHUNK_APP 20000

// Búfer para el comando enviado pór la capa de aplicación (programa Python).
char bufferComandoApp[MAX_COMMAND_LEN] = {0};
uint8_t cmdIndex = 0;

// Búfer y métricas de archivo enviado por la capa de aplicación (programa Python)..
uint8_t bufferArchivo[MAX_CHUNK_SIZE];
uint32_t tamArchivoRecibido = 0;
uint32_t tamArchivoEsperado = 0;
uint32_t ultimoProgresoArchivo = 0;

// Tamaños de chunk y ventana deslizante enviados por la capa de aplicación.
// Defaults: 400 (bytes) por chunk; ventana de 3 
uint16_t tamChunkArchivo = 400;           
uint8_t tamVentanaCmd = 3;            

// Banderas para control de procesamiento del archivo enviado por la capa de aplicación.
bool recibiendoArchivo = false;
bool archivoRecibido = false;
bool archivoListoParaTransmitir = false;
bool transferenciaActiva = false;

// ===============================================================================

// ============ ESTADOS DEL SENDER ============
enum EstadoSender : uint8_t {
  ESTADO_PREPARANDO = 0,
  ESTADO_ENVIANDO = 1,
  ESTADO_ESPERANDO_ACK = 2,
  ESTADO_REINTENTANDO = 3,
  ESTADO_COMPLETO = 4,
  ESTADO_ERROR = 5
};

// ============ ESTRUCTURA DE PROGRESO ============
struct Progreso {
  EstadoSender estado;
  uint16_t framesEnviados;
  uint16_t framesTotales;
  uint32_t bytesEnviados;
  uint32_t bytesTotales;
  uint8_t tasaError;
  uint32_t tiempoInicio;
};

// Registro de estado de transmisor y dirección de dispositivo receptor.
Progreso progreso = {EstadoSender::ESTADO_PREPARANDO, 0, 0, 0, 0, 0, 0};
uint8_t direccionRx = 0;
uint8_t siguienteSeq = 0;

// Buffer para transmisión y variables para manejar la ventana de transmisión y tiempos de espera para ACK/NACK.
Enlace::VentanaTransmision ventanaTx(3);
Enlace::GestorVentanaTransmision gestorVentanaTx(3);
Enlace::ParserStreaming parserRespuesta;

// Contadores de tiempo para manejar retransmisiones y detectar timeouts.
unsigned long ultimoFrameTime = 0;
unsigned long TIMEOUT_ACK = 2000;
uint16_t currentBaudRate = SERIAL_BAUD;

// Buffer para recepción y variables para almacenar el último ACK/NACK recibido
uint16_t bytesDisponibles = 0;
uint8_t ultimoAckRecibido = 0;
bool ackPendiente = false;
bool nackRecibido = false;
bool handshakePendiente = true;
bool transmisionCompleta = false;

uint32_t offsetDatos = 0;
uint8_t seqTramaActual = 0;
uint8_t reintentosActuales = 0;
uint8_t ultimoPayload[Protocolo::MAX_CARGA_UTIL];
uint16_t ultimaLongitudPayload = 0;
uint8_t ultimaSeqEnviada = 0;
bool datosPreparados = false;
bool finEnviado = false;

// ============ PROTOTIPOS ============
void setNuevaVentanaTx(uint8_t nuevoTam);
void setDireccionDestino(uint8_t pDireccionDestino);
void enviarHandshake();
void recibirMensaje();
void procesarFrameRespuesta(const Enlace::TramaLigera &trama);
void onAckRecibido(uint8_t ack);
void onNackRecibido(uint8_t nack);
void enviarTramaDatos(uint8_t seq, const uint8_t* datos, uint16_t longitud);
void reintentarEnvioTramaDatos(uint8_t seq, const uint8_t *datos, uint16_t longitud);
void enviarTramaFin();
void resetSender();
void prepararDatosTransmision();
bool prepararSiguienteTramaDatos();
bool transmitirTramaDatos(uint8_t seq, const uint8_t *datos, uint16_t longitud, bool registrarEnvio);
void confirmarTramaActual(uint8_t ack);
void llenarVentanaDatos();
void confirmarVentanaDatos(uint8_t ack);
void revisarTimeoutsDatos();
void procesarComandoApp();
void recibirDatosArchivoEnviar();
void prepararTransmisionArchivo();
void crearMetadataHandshake(uint8_t *payload);
uint16_t calcularFramesTotales(uint32_t bytesTotales, uint16_t tamPayload);
bool escribirByteFisico(uint8_t byte);
bool cargarChunkDesdeApp(uint32_t offset, uint16_t longitud);
bool leerTokenApp(char *token, uint8_t maxLen, const unsigned long inicio);
bool leerPayloadHexApp(uint16_t longitud, const unsigned long inicio);
int8_t valorHex(char c);

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
  currentBaudRate = nuevoBaud;
  updateTimeoutACK(nuevoBaud);
  Serial.print(F("[OK] Baudrate cambiado a "));
  Serial.println(nuevoBaud);
  Serial.println(F("[INFO] Reiniciando..."));
  delay(100);
  
  // Reset por software usando WDT
  wdt_enable(WDTO_15MS);
  while(1);
}

void setup() {
  uint16_t baud = leerBaudRateEEPROM();
  if (baud == 0 || baud == 0xFFFF) baud = SERIAL_BAUD;
  currentBaudRate = baud;
  Serial.begin(baud);
  Fisica::iniciar(baud);
  updateTimeoutACK(baud);
  delay(1000);
  setDireccionDestino(Protocolo::NODO_UNO);
}

void updateTimeoutACK(uint16_t baud) {
  unsigned long bytesPerSecond = (baud / 10);
  unsigned long tramaBytes = 50 + (Protocolo::MAX_CARGA_UTIL * 2);
  unsigned long msPerTrama = (tramaBytes * 1000) / (bytesPerSecond > 0 ? bytesPerSecond : 1);
  TIMEOUT_ACK = max(3000UL, (msPerTrama + 1000));
}

// ============ LOOP PRINCIPAL - Lógica del transmisor ============
void loop() {
  // CRITICAL: Always try to process application commands (START, DATA)
  // Don't restrict to ESTADO_PREPARANDO only - we need to receive DATA during transmission
  if (Serial.available()) {
    procesarComandoApp();
  }

  // Comenzar a transmitir el archivo una vez que ha sido recibido por el Sender.
  if (archivoListoParaTransmitir &&
      transferenciaActiva &&
      progreso.estado == EstadoSender::ESTADO_PREPARANDO) {
    prepararTransmisionArchivo(); 
    handshakePendiente = true;
    archivoListoParaTransmitir = false;
    Serial.println(F("[INFO] Archivo listo: Iniciando transmisión hacia Receiver (Arduino Uno)"));
  }

  switch(progreso.estado) {
    case EstadoSender::ESTADO_PREPARANDO:
      if (!transferenciaActiva) {
        break;
      }
      if (handshakePendiente) {
        enviarHandshake();
        ultimoFrameTime = millis();
      } else {
        prepararDatosTransmision();
        progreso.estado = EstadoSender::ESTADO_ENVIANDO;
      }
      break;

    case EstadoSender::ESTADO_ENVIANDO:
      llenarVentanaDatos();
      if (gestorVentanaTx.pendientesActivas() > 0) {
        progreso.estado = EstadoSender::ESTADO_ESPERANDO_ACK;
      } else if (offsetDatos >= tamArchivoRecibido) {
        progreso.estado = EstadoSender::ESTADO_COMPLETO;
      } else {
        progreso.estado = EstadoSender::ESTADO_ERROR;
      }
      break;

    case EstadoSender::ESTADO_ESPERANDO_ACK:
      recibirMensaje();
      revisarTimeoutsDatos();
      // No avanzar hacia ENVIANDO/COMPLETO si el handshake sigue pendiente.
      if (progreso.estado == EstadoSender::ESTADO_ESPERANDO_ACK &&
          gestorVentanaTx.pendientesActivas() == 0 &&
          !handshakePendiente) {
        progreso.estado = offsetDatos >= tamArchivoRecibido ?
          EstadoSender::ESTADO_COMPLETO : EstadoSender::ESTADO_ENVIANDO;
      }
      break;
    
    case EstadoSender::ESTADO_REINTENTANDO:
      if (handshakePendiente) {
        enviarHandshake();
      } else {
        revisarTimeoutsDatos();
      }
      break;

    case EstadoSender::ESTADO_COMPLETO:
      if (!finEnviado) {
        enviarTramaFin();
        finEnviado = true;
        transferenciaActiva = false;
        handshakePendiente = false;
        datosPreparados = false;
        archivoRecibido = false;
        recibiendoArchivo = false;
        tamArchivoRecibido = 0;
        tamArchivoEsperado = 0;
        offsetDatos = 0;
        progreso.estado = EstadoSender::ESTADO_PREPARANDO;
        resetSender();
        Serial.println(F("[INFO] Transferencia finalizada. Sender listo para nuevo archivo."));
      }
      break;

    case EstadoSender::ESTADO_ERROR:
      if (transferenciaActiva) {
        transferenciaActiva = false;
        handshakePendiente = false;
        datosPreparados = false;
        archivoListoParaTransmitir = false;
        archivoRecibido = false;
        recibiendoArchivo = false;
        tamArchivoRecibido = 0;
        tamArchivoEsperado = 0;
        offsetDatos = 0;
        resetSender();
        Serial.println(F("[ERROR] Transferencia abortada. Sender en espera de nuevo START."));
      }
      progreso.estado = EstadoSender::ESTADO_PREPARANDO;
      break;
  }
}

void setNuevaVentanaTx(uint8_t nuevoTam) {
  if (nuevoTam <= 1) nuevoTam = 1;
  if (nuevoTam >= 5) nuevoTam = 5;
  ventanaTx = Enlace::VentanaTransmision(nuevoTam);
  gestorVentanaTx.configurar(nuevoTam);
}

void setDireccionDestino(uint8_t pDireccionDestino) {
  direccionRx = pDireccionDestino;
}

void prepararDatosTransmision() {
  if (datosPreparados) {
    return;
  }
  progreso.framesEnviados = 0;
  progreso.framesTotales = calcularFramesTotales(tamArchivoRecibido, tamChunkArchivo);
  progreso.bytesEnviados = 0;
  progreso.bytesTotales = tamArchivoRecibido;
  progreso.tasaError = 0;
  progreso.tiempoInicio = millis();

  offsetDatos = 0;
  siguienteSeq = 0;
  seqTramaActual = 0;
  reintentosActuales = 0;
  ultimaLongitudPayload = 0;
  ultimaSeqEnviada = 0;
  datosPreparados = true;
  transmisionCompleta = false;
  finEnviado = false;
}

bool prepararSiguienteTramaDatos() {
  if (offsetDatos >= tamArchivoRecibido) {
    transmisionCompleta = true;
    return false;
  }

  const uint16_t bytesRestantes = tamArchivoRecibido - offsetDatos;
  ultimaLongitudPayload = min(bytesRestantes, 
        min(tamChunkArchivo, (uint16_t)Protocolo::MAX_CARGA_UTIL_PROYECTO));

  if (!cargarChunkDesdeApp(offsetDatos, ultimaLongitudPayload)) {
    return false;
  }

  const uint16_t bytesRespaldo = min(ultimaLongitudPayload, (uint16_t)sizeof(ultimoPayload));
  for (uint16_t i = 0; i < bytesRespaldo; i++) {
    ultimoPayload[i] = bufferArchivo[i];
  }

  seqTramaActual = siguienteSeq;
  ultimaSeqEnviada = seqTramaActual;
  reintentosActuales = 0;

  return true;
}

void confirmarTramaActual(uint8_t ack) {
  const uint8_t ackEsperado = ultimaSeqEnviada + 1;
  if (ack != ackEsperado) {
    return;
  }

  progreso.framesEnviados++;
  progreso.bytesEnviados += ultimaLongitudPayload;
  offsetDatos += ultimaLongitudPayload;
  siguienteSeq = ack;
  ackPendiente = false;
  nackRecibido = false;
  reintentosActuales = 0;

  if (offsetDatos >= tamArchivoRecibido) {
    transmisionCompleta = true;
    progreso.estado = EstadoSender::ESTADO_COMPLETO;
  } else {
    progreso.estado = EstadoSender::ESTADO_ENVIANDO;
  }
}

bool escribirByteFisico(uint8_t byte) {
  return Fisica::escribirByte(byte);
}

bool leerTokenApp(char *token, uint8_t maxLen, const unsigned long inicio) {
  if (maxLen == 0) {
    return false;
  }

  char c = '\0';
  do {
    while (!Serial.available()) {
      if (millis() - inicio > TIMEOUT_CHUNK_APP) {
        token[0] = '\0';
        return false;
      }
      delay(1);
    }
    c = (char)Serial.read();
  } while (c == ' ' || c == '\n' || c == '\r' || c == '\t');

  uint8_t index = 0;
  while (c != ' ' && c != '\n' && c != '\r' && c != '\t') {
    if (index < maxLen - 1) {
      token[index++] = c;
    }

    while (!Serial.available()) {
      if (millis() - inicio > TIMEOUT_CHUNK_APP) {
        token[index] = '\0';
        return index > 0;
      }
      delay(1);
    }
    c = (char)Serial.read();
  }

  token[index] = '\0';
  return index > 0;
}

int8_t valorHex(char c) {
  if (c >= '0' && c <= '9') return c - '0';
  if (c >= 'A' && c <= 'F') return c - 'A' + 10;
  if (c >= 'a' && c <= 'f') return c - 'a' + 10;
  return -1;
}

bool leerPayloadHexApp(uint16_t longitud, const unsigned long inicio) {
  uint16_t recibidos = 0;
  int8_t nibbleAlto = -1;

  while (recibidos < longitud) {
    while (!Serial.available()) {
      if (millis() - inicio > TIMEOUT_CHUNK_APP) {
        Serial.print(F("[ERROR] Timeout DATA hex. Recibidos: "));
        Serial.print(recibidos);
        Serial.print(F("/"));
        Serial.println(longitud);
        return false;
      }
      delay(1);
    }

    char c = (char)Serial.read();
    if (c == ' ' || c == '\n' || c == '\r' || c == '\t') {
      continue;
    }

    int8_t valor = valorHex(c);
    if (valor < 0) {
      Serial.println(F("[ERROR] DATA contiene hex invalido."));
      return false;
    }

    if (nibbleAlto < 0) {
      nibbleAlto = valor;
    } else {
      bufferArchivo[recibidos++] = (uint8_t)((nibbleAlto << 4) | valor);
      nibbleAlto = -1;
    }
  }

  return true;
}

bool cargarChunkDesdeApp(uint32_t offset, uint16_t longitud) {
  if (longitud > MAX_CHUNK_SIZE) {
    return false;
  }

  Serial.print(F("[REQ] "));
  Serial.print(offset);
  Serial.print(F(" "));
  Serial.println(longitud);
  Serial.flush();

  const unsigned long inicio = millis();
  char token[16];

  if (!leerTokenApp(token, sizeof(token), inicio) || strcmp(token, "DATA") != 0) {
    Serial.println(F("[ERROR] Respuesta DATA esperada."));
    return false;
  }

  if (!leerTokenApp(token, sizeof(token), inicio)) {
    Serial.println(F("[ERROR] DATA sin offset."));
    return false;
  }
  uint32_t offsetRecibido = strtoul(token, NULL, 10);

  if (!leerTokenApp(token, sizeof(token), inicio)) {
    Serial.println(F("[ERROR] DATA sin longitud."));
    return false;
  }
  uint16_t longitudRecibida = (uint16_t)atoi(token);

  if (offsetRecibido != offset || longitudRecibida != longitud) {
    Serial.println(F("[ERROR] DATA no coincide con REQ."));
    return false;
  }

  return leerPayloadHexApp(longitud, inicio);
}

void llenarVentanaDatos() {
  while (gestorVentanaTx.puedeEnviar() && offsetDatos < tamArchivoRecibido) {
    const uint16_t bytesRestantes = tamArchivoRecibido - offsetDatos;
    const uint16_t longitud = min(bytesRestantes, min(tamChunkArchivo, (uint16_t)Protocolo::MAX_CARGA_UTIL_PROYECTO));
    const uint8_t seq = siguienteSeq;

    if (!cargarChunkDesdeApp(offsetDatos, longitud)) {
      progreso.estado = EstadoSender::ESTADO_ERROR;
      return;
    }

    if (!gestorVentanaTx.registrarEnvio(seq, offsetDatos, longitud, millis())) {
      return;
    }

    transmitirTramaDatos(seq, bufferArchivo, longitud, true);
    offsetDatos += longitud;
    siguienteSeq++;
  }
}

void confirmarVentanaDatos(uint8_t ack) {
  uint16_t bytesConfirmados = 0;
  uint8_t framesConfirmados = 0;

  if (!gestorVentanaTx.confirmarHasta(ack, bytesConfirmados, framesConfirmados)) {
    return;
  }

  progreso.framesEnviados += framesConfirmados;
  progreso.bytesEnviados += bytesConfirmados;
  ventanaTx.registrarAck(ack);

  progreso.estado = EstadoSender::ESTADO_ENVIANDO;
}

void revisarTimeoutsDatos() {
  for (uint8_t i = 0; i < Enlace::GestorVentanaTransmision::MAX_VENTANA; i++) {
    Enlace::TramaPendiente *pendiente = gestorVentanaTx.pendiente(i);
    if (pendiente == nullptr || !pendiente->activa) continue;
    if (millis() - pendiente->ultimoEnvio <= TIMEOUT_ACK) continue;

    if (pendiente->reintentos >= Enlace::MAX_REINTENTOS) {
      progreso.estado = EstadoSender::ESTADO_ERROR;
      return;
    }

    pendiente->reintentos++;
    pendiente->ultimoEnvio = millis();

    if (!cargarChunkDesdeApp(pendiente->offset, pendiente->longitud)) {
      progreso.estado = EstadoSender::ESTADO_ERROR;
      return;
    }

    transmitirTramaDatos(pendiente->seq, bufferArchivo, pendiente->longitud, false);
  }
}

void procesarFrameRespuesta(const Enlace::TramaLigera &trama) {
  if (trama.direccion != Protocolo::NODO_NANO && 
      trama.direccion != Protocolo::NODO_BROADCAST) {
    return;
  }

  switch (trama.control) {
    case Protocolo::TRAMA_ACK:
      onAckRecibido(trama.ack);
      break;
    case Protocolo::TRAMA_NACK:
      onNackRecibido(trama.ack);
      break;
    default:
      break;
  }
}

void onAckRecibido(uint8_t ack) {
  Serial.print(F("[NANO ACK] Recibido ack="));
  Serial.println(ack);
  
  ultimoAckRecibido = ack;

  if (handshakePendiente && ack == 0) {
    Serial.println(F("[NANO] Handshake completado!"));
    handshakePendiente = false;
    progreso.estado = EstadoSender::ESTADO_PREPARANDO;
    Serial.println(F("Handshake completado con el receptor. Preparando tramas para transmisión...")); 
    return;
  }

  if (progreso.estado == EstadoSender::ESTADO_ESPERANDO_ACK) {
    Serial.println(F("[NANO] Procesando ACK de datos"));
    confirmarVentanaDatos(ack);
    return;
  }
}

void onNackRecibido(uint8_t nack) {
  nackRecibido = true;
  revisarTimeoutsDatos();
  progreso.estado = EstadoSender::ESTADO_ESPERANDO_ACK;
}

void crearMetadataHandshake(uint8_t *payload) {
  payload[0] = (uint8_t)((tamArchivoRecibido >> 24) & 0xFF);
  payload[1] = (uint8_t)((tamArchivoRecibido >> 16) & 0xFF);
  payload[2] = (uint8_t)((tamArchivoRecibido >> 8) & 0xFF);
  payload[3] = (uint8_t)(tamArchivoRecibido & 0xFF);
  payload[4] = (uint8_t)((progreso.framesTotales >> 8) & 0xFF);
  payload[5] = (uint8_t)(progreso.framesTotales & 0xFF);
  payload[6] = (uint8_t)((tamChunkArchivo >> 8) & 0xFF);
  payload[7] = (uint8_t)(tamChunkArchivo & 0xFF);
  payload[8] = tamVentanaCmd;
  payload[9] = 0;
}

void enviarHandshake() {
  Serial.print(F("[HANDSHAKE] Tiempo envio: "));
  Serial.println(millis());
  uint8_t metadata[TAM_METADATA_HANDSHAKE];
  crearMetadataHandshake(metadata);

  Enlace::EstadoCodificacion estadoCodificacion = Enlace::emitirTrama(
    direccionRx,
    Protocolo::TRAMA_HANDSHAKE,
    0,
    0,
    metadata,
    TAM_METADATA_HANDSHAKE,
    escribirByteFisico
  );

  if (estadoCodificacion == Enlace::CODIFICACION_OK) {
    progreso.estado = EstadoSender::ESTADO_ESPERANDO_ACK;
    ultimoFrameTime = millis();
    Serial.println(F("Handshake enviado al receptor. Esperando ACK de confirmacion..."));
  } else {
    Serial.println(F("Error al codificar el handshake. No se envio."));
  }
}

void recibirMensaje() {
  Serial.print(F("[NANO RECV] Tiempo: "));
  Serial.print(millis());
  Serial.print(F(" puedeLeerByte="));
  Serial.print(Fisica::puedeLeerByte());
  Serial.print(F(" available="));
  Serial.println(Fisica::puedeLeerByte() ? "2+" : "0-1");
  
  if (!Fisica::puedeLeerByte()) {
    if (progreso.estado == EstadoSender::ESTADO_ESPERANDO_ACK &&
        millis() - ultimoFrameTime > TIMEOUT_ACK) {
      Serial.println(F("[TIMEOUT] No se recibio ACK"));
      revisarTimeoutsDatos();
    }
    return;
  }

  Serial.println(F("[NANO] Hay bytes disponibles para leer"));

  while (Fisica::puedeLeerByte()) {
    uint8_t byteRecibido = 0;
    bool errorIrrecuperable = false;

    if (!Fisica::leerByte(byteRecibido, errorIrrecuperable)) {
      if (errorIrrecuperable) {
        Serial.println(F("[NANO ERROR] Error Hamming no corregible"));
      }
      return;
    }

    Serial.print(F("[NANO RX] Byte: 0x"));
    Serial.println(byteRecibido, HEX);

    ultimoFrameTime = millis();

    Enlace::TramaLigera trama;
    Enlace::EstadoDecodificacion error = Enlace::DECODIFICACION_OK;
    Enlace::EstadoParserStreaming estadoParser =
      parserRespuesta.procesarByte(byteRecibido, trama, error);

    if (estadoParser == Enlace::PARSER_TRAMA_COMPLETA) {
      Serial.print(F("[NANO TRAMA] Dir: 0x"));
      Serial.print(trama.direccion, HEX);
      Serial.print(F(" Ctrl: 0x"));
      Serial.print(trama.control, HEX);
      Serial.print(F(" Ack: "));
      Serial.println(trama.ack);
      procesarFrameRespuesta(trama);
    }
  }
}

bool transmitirTramaDatos(uint8_t seq, const uint8_t *datos, uint16_t longitud, bool registrarEnvio) {
  if (registrarEnvio && !ventanaTx.puedeEnviar()) {
    Serial.println(F("Ventana de transmision llena. No se puede enviar la trama en este momento."));
    return false;
  }

  Enlace::EstadoCodificacion resultadoCodificacion =
    Enlace::emitirTrama(
    direccionRx,
    Protocolo::TRAMA_DATOS,
    seq,
    0,
    datos,
    longitud,
    escribirByteFisico
  );

  if (resultadoCodificacion == Enlace::CODIFICACION_CARGA_MUY_GRANDE) {
    Serial.println(F("Error: La carga util es demasiado grande para la trama."));
    progreso.estado = EstadoSender::ESTADO_ERROR;
    return false;
  }

  if (resultadoCodificacion != Enlace::CODIFICACION_OK) {
    progreso.estado = EstadoSender::ESTADO_ERROR;
    return false;
  }

  if (registrarEnvio) {
    ventanaTx.registrarEnvio();
  }

  ultimoFrameTime = millis();
  ackPendiente = true;
  progreso.estado = EstadoSender::ESTADO_ESPERANDO_ACK;

  return true;
}

void enviarTramaDatos(uint8_t seq, const uint8_t *datos, uint16_t longitud) {
  transmitirTramaDatos(seq, datos, longitud, true);
}

void reintentarEnvioTramaDatos(uint8_t seq, const uint8_t *datos, uint16_t longitud) {
  reintentosActuales++;
  ackPendiente = false;
  nackRecibido = false;
  transmitirTramaDatos(seq, datos, longitud, false);
}

void enviarTramaFin() {
  Enlace::EstadoCodificacion estadoCodificacion = Enlace::emitirTrama(
    direccionRx,
    Protocolo::TRAMA_FIN,
    0,
    0,
    nullptr,
    0,
    escribirByteFisico
  );

  if (estadoCodificacion == Enlace::CODIFICACION_OK) {
    Serial.println(F("Trama FIN enviada al receptor."));
  } else {
    Serial.println(F("Error al codificar la trama FIN. No se envio."));
    Serial.print(F("Estado de codificacion: "));
    Serial.println(estadoCodificacion);
  }
}

void resetSender() {
  progreso = {EstadoSender::ESTADO_PREPARANDO, 0, 0, 0, 0, 0, 0};
  siguienteSeq = 0;
  ultimoFrameTime = 0;
  bytesDisponibles = 0;
  ultimoAckRecibido = 0;
  ackPendiente = false;
  nackRecibido = false;
  handshakePendiente = true;
  transmisionCompleta = false;
  datosPreparados = false;
  offsetDatos = 0;
  seqTramaActual = 0;
  reintentosActuales = 0;
  ultimaLongitudPayload = 0;
  ultimaSeqEnviada = 0;
  finEnviado = false;
  ventanaTx.reiniciar();
  gestorVentanaTx.reiniciar();
  parserRespuesta.reiniciar();
  transferenciaActiva = false;
}

void procesarComandoApp() {
  while (Serial.available()) {
    char c = Serial.read();

    if (c == '\n' || c == '\r') {
      if (cmdIndex == 0) return;

      bufferComandoApp[cmdIndex] = '\0';
      
      // === SETBAUD command ===
      if (strncmp(bufferComandoApp, "SETBAUD", 7) == 0) {
        procesarComandoSETBAUD(bufferComandoApp);
      }
      // === START Command ===
      else if (strncmp(bufferComandoApp, "START", 5) == 0) {
        Serial.println(F("[App] START recibido de app."));
        
        char *token = strtok(bufferComandoApp, " ");
        int paramCount = 0;
        
        while (token != NULL && paramCount < 5) {
          paramCount++;
          if (paramCount == 3) tamArchivoEsperado = strtoul(token, NULL, 10);
          else if (paramCount == 4) tamChunkArchivo = (uint16_t)atoi(token);
          else if (paramCount == 5) tamVentanaCmd = (uint8_t)atoi(token);
          token = strtok(NULL, " ");
        }

        if (paramCount < 5 || tamArchivoEsperado == 0 || tamChunkArchivo == 0) {
          Serial.println(F("[ERROR] START invalido. Uso: START <archivo> <tam> <chunk> <ventana>"));
          cmdIndex = 0;
          return;
        }

        if (tamVentanaCmd < 1) tamVentanaCmd = 1;
        if (tamVentanaCmd > 5) tamVentanaCmd = 5;

        if (tamChunkArchivo > Protocolo::MAX_CARGA_UTIL_PROYECTO) {
          Serial.print(F("[ERROR] Carga util por trama no soportada. Solicitada: "));
          Serial.print(tamChunkArchivo);
          Serial.print(F(" | Maximo: "));
          Serial.println(Protocolo::MAX_CARGA_UTIL_PROYECTO);
          cmdIndex = 0;
          return;
        }

        tamArchivoRecibido = tamArchivoEsperado;
        ultimoProgresoArchivo = 0;
        recibiendoArchivo = false;
        archivoRecibido = true;
        archivoListoParaTransmitir = true;
        transferenciaActiva = true;
        handshakePendiente = true;
        finEnviado = false;

        Serial.print(F("[App] Archivo registrado: "));
        Serial.print(tamArchivoEsperado);
        Serial.print(F(" bytes | Chunk: "));
        Serial.print(tamChunkArchivo);
        Serial.print(F(" | Ventana: "));
        Serial.println(tamVentanaCmd);
      } 
      
      // === END Command ===
      else if (strncmp(bufferComandoApp, "[END]", 5) == 0) {
        Serial.println(F("[App] END recibido - Archivo completo."));
        recibiendoArchivo = false;
        archivoRecibido = true;
        
        if (tamArchivoRecibido == tamArchivoEsperado) {
          Serial.println(F("[App] Tamaño correcto. Listo para transmitir."));
          archivoListoParaTransmitir = true;
        } else {
          Serial.print(F("[ERROR] Tamaño recibido no coincide. Esperado: "));
          Serial.print(tamArchivoEsperado);
          Serial.print(F(" Recibido: "));
          Serial.println(tamArchivoRecibido);
        }
      }
      
      cmdIndex = 0;
    } 
    else if (cmdIndex < MAX_COMMAND_LEN - 1) {
      bufferComandoApp[cmdIndex++] = c;
    }
  }
}

void recibirDatosArchivoEnviar() {
  if (!recibiendoArchivo) return;

  while (Serial.available() &&
         tamArchivoRecibido < MAX_CHUNK_SIZE &&
         tamArchivoRecibido < tamArchivoEsperado) {
    bufferArchivo[tamArchivoRecibido++] = Serial.read();
    
    if ((tamArchivoRecibido - ultimoProgresoArchivo) >= PROGRESO_ARCHIVO_STEP) {
      Serial.print(F("[PROGRESO] Recibidos: "));
      Serial.print(tamArchivoRecibido);
      Serial.println(F(" bytes"));
      ultimoProgresoArchivo = tamArchivoRecibido;
    }
  }

  if (tamArchivoEsperado > 0 && tamArchivoRecibido >= tamArchivoEsperado) {
    recibiendoArchivo = false;
  }
}

void prepararTransmisionArchivo() {
  setNuevaVentanaTx(tamVentanaCmd);

  progreso.framesEnviados = 0;
  progreso.bytesEnviados = 0;
  progreso.bytesTotales = tamArchivoRecibido;
  progreso.framesTotales = calcularFramesTotales(tamArchivoRecibido, tamChunkArchivo);
  progreso.tasaError = 0;
  progreso.tiempoInicio = millis();

  offsetDatos = 0;
  siguienteSeq = 0;
  seqTramaActual = 0;
  reintentosActuales = 0;
  datosPreparados = true;
  transmisionCompleta = false;
  finEnviado = false;

  Serial.print(F("[TRANSMISIÓN] Archivo listo - "));
  gestorVentanaTx.reiniciar();
  Serial.print(tamArchivoRecibido);
  Serial.print(F(" bytes | "));
  Serial.print(progreso.framesTotales);
  Serial.println(F(" frames"));
}

uint16_t calcularFramesTotales(uint32_t bytesTotales, uint16_t tamPayload) {
  if (tamPayload == 0) {
    return 0;
  }
  return (uint16_t)((bytesTotales + tamPayload - 1) / tamPayload);
}
