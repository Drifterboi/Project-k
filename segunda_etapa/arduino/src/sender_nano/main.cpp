#include <Arduino.h>

#include "../../lib/enlace/enlace.h"
#include "../../lib/protocolo/protocolo.h"
#include "../../lib/crc/crc.h"
#include "../../lib/dummy_frames/dummy_frames.h"
#include "../../lib/hamming/hamming.h"
#include "../../lib/fisica/fisica.h"

#ifndef SERIAL_BAUD
#define SERIAL_BAUD 9600
#endif

const uint8_t TAM_METADATA_HANDSHAKE = 10;


// ==== Variables para parsing de instrucciones enviadas por capa aplicación ====
#define MAX_COMMAND_LEN   128
#define MAX_CHUNK_SIZE    1000      // Maximo payload local; el archivo completo queda en Python.
#define PROGRESO_ARCHIVO_STEP 100
#define TIMEOUT_CHUNK_APP 3000

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
const unsigned long TIMEOUT_ACK = 1000;

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

void setup() {
  Serial.begin(SERIAL_BAUD);
  Fisica::iniciar(SERIAL_BAUD);
  delay(1000);
  setDireccionDestino(Protocolo::NODO_UNO);
}


// ============ LOOP PRINCIPAL - Lógica del transmisor ============
void loop() {
  if (progreso.estado == EstadoSender::ESTADO_PREPARANDO &&
      !archivoListoParaTransmitir &&
      handshakePendiente) {
    procesarComandoApp();
  }

  // Comenzar a transmitir el archivo una vez que ha sido recibido por el Sender.
  if (archivoListoParaTransmitir && progreso.estado == EstadoSender::ESTADO_PREPARANDO) {
    prepararTransmisionArchivo(); 
    handshakePendiente = true;
    archivoListoParaTransmitir = false;

    Serial.println(F("[INFO] Archivo listo: Iniciando transmisión hacia Receiver (Arduino Uno)"));
  }


  switch(progreso.estado) {

    case EstadoSender::ESTADO_PREPARANDO:
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
      if (progreso.estado == EstadoSender::ESTADO_ESPERANDO_ACK &&
          gestorVentanaTx.pendientesActivas() == 0) {
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
      }
      break;

    case EstadoSender::ESTADO_ERROR:
      break;
  }
}

// Reemplazar ventana deslizante actual con nueva ventana deslizante con nuevo tamaño.
void setNuevaVentanaTx(uint8_t nuevoTam) {
  if (nuevoTam <= 1) nuevoTam = 1;
  if (nuevoTam >= 5) nuevoTam = 5;
  ventanaTx = Enlace::VentanaTransmision(nuevoTam);
  gestorVentanaTx.configurar(nuevoTam);
  Serial.print(F("[CONFIG] Ventana deslizante configurada a: "));
  Serial.println(nuevoTam);
}

// Configurar dirección del destino de la transmisión.
void setDireccionDestino(uint8_t pDireccionDestino) {
  direccionRx = pDireccionDestino;
}

void prepararDatosTransmision() {
  if (datosPreparados) {
    return;
  }

  progreso.framesEnviados = 0;


  // Calcular cantidad de frames totales con tamaño de chunk como tamaño de payload
  // progreso.framesTotales = (tamArchivoRecibido + tamChunkArchivo - 1) / tamChunkArchivo;

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

  Serial.print(F("[PREPARADO] Bytes a enviar: "));
  Serial.print(progreso.bytesTotales);
  Serial.print(F(" Frames: "));
  Serial.println(progreso.framesTotales);
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

  // Salidas en consola para prueba con archivo.
  Serial.print(F("[CHUNK] Preparado - Offset: "));
  Serial.print(offsetDatos);
  Serial.print(F(" | Tamaño: "));
  Serial.print(ultimaLongitudPayload);
  Serial.print(F(" | Tam. Chunk configurado: "));
  Serial.println(tamChunkArchivo);

  return true;
} 

void confirmarTramaActual(uint8_t ack) {
  const uint8_t ackEsperado = ultimaSeqEnviada + 1;
  if (ack != ackEsperado) {
    Serial.print(F("Ack recibido no corresponde a la trama actual. Esperado: "));
    Serial.print(ackEsperado);
    Serial.print(F(" Recibido: "));
    Serial.println(ack);
    return;
  }

  progreso.framesEnviados++;
  progreso.bytesEnviados += ultimaLongitudPayload;
  offsetDatos += ultimaLongitudPayload;
  siguienteSeq = ack;
  ackPendiente = false;
  nackRecibido = false;
  reintentosActuales = 0;

  Serial.print(F("ACK valido. Progreso bytes: "));
  Serial.print(progreso.bytesEnviados);
  Serial.print(F("/"));
  Serial.println(progreso.bytesTotales);

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

bool cargarChunkDesdeApp(uint32_t offset, uint16_t longitud) {
  if (longitud > MAX_CHUNK_SIZE) {
    return false;
  }

  Serial.print(F("[REQ] "));
  Serial.print(offset);
  Serial.print(F(" "));
  Serial.println(longitud);
  Serial.flush();

  uint16_t recibidos = 0;
  const unsigned long inicio = millis();

  while (recibidos < longitud) {
    if (Serial.available()) {
      bufferArchivo[recibidos++] = Serial.read();
      continue;
    }

    if (millis() - inicio > TIMEOUT_CHUNK_APP) {
      Serial.print(F("[ERROR] Timeout esperando chunk app offset="));
      Serial.print(offset);
      Serial.print(F(" len="));
      Serial.println(longitud);
      return false;
    }
  }

  return true;
}

void llenarVentanaDatos() {
  while (gestorVentanaTx.puedeEnviar() && offsetDatos < tamArchivoRecibido) {
    const uint16_t bytesRestantes = tamArchivoRecibido - offsetDatos;
    const uint16_t longitud = min(bytesRestantes, min(tamChunkArchivo, (uint16_t)Protocolo::MAX_CARGA_UTIL_PROYECTO));
    const uint8_t seq = siguienteSeq;

    if (!cargarChunkDesdeApp(offsetDatos, longitud)) {
      Serial.println(F("[ERROR] No se pudo cargar chunk desde la aplicacion."));
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
    Serial.println(F("ACK duplicado o fuera de ventana. Ignorando."));
    return;
  }

  progreso.framesEnviados += framesConfirmados;
  progreso.bytesEnviados += bytesConfirmados;
  ventanaTx.registrarAck(ack);

  Serial.print(F("ACK valido. Progreso bytes: "));
  Serial.print(progreso.bytesEnviados);
  Serial.print(F("/"));
  Serial.println(progreso.bytesTotales);

  progreso.estado = EstadoSender::ESTADO_ENVIANDO;
}

void revisarTimeoutsDatos() {
  for (uint8_t i = 0; i < Enlace::GestorVentanaTransmision::MAX_VENTANA; i++) {
    Enlace::TramaPendiente *pendiente = gestorVentanaTx.pendiente(i);
    if (pendiente == nullptr || !pendiente->activa) continue;
    if (millis() - pendiente->ultimoEnvio <= TIMEOUT_ACK) continue;

    if (pendiente->reintentos >= Enlace::MAX_REINTENTOS) {
      Serial.println(F("[ERROR] Maximo de reintentos alcanzado. Transmision abortada."));
      progreso.estado = EstadoSender::ESTADO_ERROR;
      return;
    }

    pendiente->reintentos++;
    pendiente->ultimoEnvio = millis();
    Serial.print(F("[TIMEOUT] Reenviando seq "));
    Serial.print(pendiente->seq);
    Serial.print(F(" intento "));
    Serial.println(pendiente->reintentos);

    if (!cargarChunkDesdeApp(pendiente->offset, pendiente->longitud)) {
      Serial.println(F("[ERROR] No se pudo recargar chunk para reintento."));
      progreso.estado = EstadoSender::ESTADO_ERROR;
      return;
    }

    transmitirTramaDatos(
      pendiente->seq,
      bufferArchivo,
      pendiente->longitud,
      false
    );
  }
}

// Procesar tramas de respuestas enviadas por el receptor (ACK/NACK).
void procesarFrameRespuesta(const Enlace::TramaLigera &trama) {
  if (trama.direccion != Protocolo::NODO_NANO && 
      trama.direccion != Protocolo::NODO_BROADCAST) {
    Serial.println(F("[DESCARTE] No dirigido a este nodo"));
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
      Serial.println(F("[DESCONOCIDA] Tipo de trama no reconocida"));
  }
}

void onAckRecibido(uint8_t ack) {
  ultimoAckRecibido = ack;

  if (handshakePendiente && ack == 0) {
    handshakePendiente = false;
    progreso.estado = EstadoSender::ESTADO_PREPARANDO;
    Serial.println(F("Handshake completado con el receptor. Preparando tramas para transmisión...")); 
    return;
  }

  if (false && progreso.estado == EstadoSender::ESTADO_ESPERANDO_ACK && ack == ventanaTx.siguienteSeq() - 1) {
    progreso.framesEnviados++;
    progreso.bytesEnviados += Protocolo::MAX_CARGA_UTIL;
    progreso.estado = EstadoSender::ESTADO_COMPLETO;
    Serial.println(F("Transmisión completa confirmada por el receptor."));
    return;
  }

  if (progreso.estado == EstadoSender::ESTADO_ESPERANDO_ACK) {
    confirmarVentanaDatos(ack);
    return;
  }

  if (false && progreso.estado == EstadoSender::ESTADO_ESPERANDO_ACK) {
    if (ack != ultimaSeqEnviada + 1) {
      Serial.print(F("Ack recibido no corresponde a la trama actual. Esperado: "));
      Serial.print(ultimaSeqEnviada + 1);
      Serial.print(F(" Recibido: "));
      Serial.println(ack);
      return;
    }

    if (ventanaTx.registrarAck(ack)) {
      confirmarTramaActual(ack);
      return;
      progreso.framesEnviados++;
      progreso.bytesEnviados += Protocolo::MAX_CARGA_UTIL;
      Serial.println(F("Ack válido recibido. Preparando siguiente trama..."));
      siguienteSeq = ack;
      progreso.estado = EstadoSender::ESTADO_ENVIANDO;
    } else {
      Serial.println(F("Ack recibido fuera de ventana o no esperado. Ignorando."));
    }
    return;
  }
}

void onNackRecibido(uint8_t nack) {
  Serial.println(F("[NACK] Recibido. Reintentando transmisión..."));
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

// Enviar trama de handshake para iniciar comunicación con el receptor (Arduino Uno).
void enviarHandshake() {
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
// Revisar búfer de recepción para procesar trama de respuesta del receptor y actualizar el estado del transmisor en consecuencia.
void recibirMensaje() {
  if (!Fisica::puedeLeerByte()) {
    if (progreso.estado == EstadoSender::ESTADO_ESPERANDO_ACK &&
        millis() - ultimoFrameTime > TIMEOUT_ACK) {
      Serial.println(F("[TIMEOUT] No se recibio ACK a tiempo. Preparando reintento..."));
      revisarTimeoutsDatos();
    }
    return;
  }

  while (Fisica::puedeLeerByte()) {
    uint8_t byteRecibido = 0;
    bool errorIrrecuperable = false;

    if (!Fisica::leerByte(byteRecibido, errorIrrecuperable)) {
      if (errorIrrecuperable) {
        Serial.println(F("[ERROR] Hamming: error no corregible en respuesta"));
      }
      return;
    }

    ultimoFrameTime = millis();

    Enlace::TramaLigera trama;
    Enlace::EstadoDecodificacion error = Enlace::DECODIFICACION_OK;
    Enlace::EstadoParserStreaming estadoParser =
      parserRespuesta.procesarByte(byteRecibido, trama, error);

    if (estadoParser == Enlace::PARSER_TRAMA_COMPLETA) {
      Serial.print(F("[TRAMA RECIBIDA] Tipo: 0x"));
      Serial.print(trama.control, HEX);
      Serial.print(F(" Seq: "));
      Serial.println(trama.seq);
      procesarFrameRespuesta(trama);
    } else if (estadoParser == Enlace::PARSER_TRAMA_INVALIDA) {
      Serial.print(F("[ERROR] Decodificacion respuesta: "));
      Serial.println(error);
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
    Serial.println(F("Error al codificar la trama de datos. No se envio."));
    Serial.print(F("Estado de codificacion: "));
    Serial.println(resultadoCodificacion);
    progreso.estado = EstadoSender::ESTADO_ERROR;
    return false;
  }

  if (registrarEnvio) {
    ventanaTx.registrarEnvio();
  }

  ultimoFrameTime = millis();
  ackPendiente = true;
  progreso.estado = EstadoSender::ESTADO_ESPERANDO_ACK;

  Serial.print(F("Trama de datos enviada. Seq: "));
  Serial.print(seq);
  Serial.print(F(" Longitud: "));
  Serial.println(longitud);

  return true;
}

// Crear trama de datos para enviar al receptor.
void enviarTramaDatos(uint8_t seq, const uint8_t *datos, uint16_t longitud) {
  transmitirTramaDatos(seq, datos, longitud, true);
}
void reintentarEnvioTramaDatos(uint8_t seq, const uint8_t *datos, uint16_t longitud) {
  reintentosActuales++;
  Serial.print(F("Reintentando envío de la trama con Seq: "));
  Serial.println(seq);
  Serial.print(F("Intento "));
  Serial.print(reintentosActuales);
  Serial.print(F("/"));
  Serial.println(Enlace::MAX_REINTENTOS);

  ackPendiente = false;
  nackRecibido = false;
  transmitirTramaDatos(seq, datos, longitud, false);
}

// Enviar trama de finalización (FIN) para indicar al receptor que se ha completado la transmisión de datos.
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
// Reiniciar transmisor a estado base.
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
}

// Procesar comando enviado por capa de aplicación para enviar un nuevo archivo.
void procesarComandoApp() {
  while (Serial.available()) {
    char c = Serial.read();

    if (c == '\n' || c == '\r') {
      if (cmdIndex == 0) return;  // Línea vacía

      bufferComandoApp[cmdIndex] = '\0';
      
      // === START Command ===
      if (strncmp(bufferComandoApp, "START", 5) == 0) {
        Serial.println(F("[App] START recibido de app."));
        
        // Parsear parámetros: "START nombrearchivo tam_archivo tam_chunk tam_ventana"
        char *token = strtok(bufferComandoApp, " ");
        int paramCount = 0;
        
        while (token != NULL && paramCount < 5) {
          paramCount++;
          if (paramCount == 3) tamArchivoEsperado = strtoul(token, NULL, 10);
          else if (paramCount == 4) tamChunkArchivo = (uint16_t)atoi(token);
          else if (paramCount == 5) tamVentanaCmd = (uint8_t)atoi(token);
          token = strtok(NULL, " ");
        }

        if (tamChunkArchivo > Protocolo::MAX_CARGA_UTIL_PROYECTO) {
          Serial.print(F("[ERROR] Carga util por trama no soportada por el firmware actual. Solicitada: "));
          Serial.print(tamChunkArchivo);
          Serial.print(F(" bytes | Maximo actual: "));
          Serial.print(Protocolo::MAX_CARGA_UTIL_PROYECTO);
          Serial.println(F(" bytes."));
          cmdIndex = 0;
          return;
        }

        tamArchivoRecibido = tamArchivoEsperado;
        ultimoProgresoArchivo = 0;
        recibiendoArchivo = false;
        archivoRecibido = true;
        archivoListoParaTransmitir = true;

        Serial.print(F("[App] Archivo registrado en Python: "));
        Serial.print(tamArchivoEsperado);
        Serial.print(F(" bytes | Tam. Chunk: "));
        Serial.print(tamChunkArchivo);
        Serial.print(F(" | Tam. Ventana Deslizante: "));
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
      
      cmdIndex = 0;  // Reset buffer
    } 
    else if (cmdIndex < MAX_COMMAND_LEN - 1) {
      bufferComandoApp[cmdIndex++] = c;
    }
  }
}

// Procesar datos del archivo enviado por la aplicación antes de generar frames.
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

// Preparar Sender para transmitir datos del archivo enviado desde capa de aplicación.
void prepararTransmisionArchivo() {
  // Configurar nueva ventana deslizante para el archivo a transmitir.
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

  Serial.print(F("[TRANSMISIÓN] Archivo listo para ser enviado - "));
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
