// ============================================================
// RECEIVER ARDUINO UNO - VERSIÓN TINKERCAD AUTOCONTENIDA
// Proyecto: Transferencia de Archivos Redes
// Autor: Equipo Redes
// ============================================================

// ============ DEFINICIONES PROTOCOLO ============
namespace Protocolo {
  const uint8_t FLAG = 0x7E;
  const uint8_t NODO_NANO = 0x01;
  const uint8_t NODO_UNO = 0x02;
  const uint8_t NODO_BROADCAST = 0xFF;
  
  const uint16_t MAX_CARGA_UTIL = 64;
  const uint8_t TAM_LONGITUD = 2;
  const uint8_t TAM_CRC = 2;
  const uint8_t TAM_ENCABEZADO = 1 + 1 + 1 + 1 + 1 + TAM_LONGITUD;
  const uint8_t TAM_CONTROL_FINAL = TAM_CRC + 1;
  const uint16_t TAM_MAX_TRAMA = TAM_ENCABEZADO + MAX_CARGA_UTIL + TAM_CONTROL_FINAL;
  
  enum TipoTrama : uint8_t {
    TRAMA_DATOS = 0x01,
    TRAMA_ACK = 0x02,
    TRAMA_NACK = 0x03,
    TRAMA_HANDSHAKE = 0x04,
    TRAMA_FIN = 0x05,
    TRAMA_ERROR = 0x06
  };
  
  struct Trama {
    uint8_t direccion;
    uint8_t control;
    uint8_t seq;
    uint8_t ack;
    uint16_t longitud;
    uint8_t cargaUtil[MAX_CARGA_UTIL];
    uint16_t crc;
  };
}

// ============ CRC SIMPLE ============
uint16_t calcularCRC(const uint8_t *data, uint16_t length) {
  uint16_t crc = 0xFFFF;
  for (uint16_t i = 0; i < length; i++) {
    crc ^= data[i];
    for (uint8_t j = 0; j < 8; j++) {
      if (crc & 1) {
        crc = (crc >> 1) ^ 0xA001;
      } else {
        crc >>= 1;
      }
    }
  }
  return crc;
}

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

// ============ VENTANA RECEPCIÓN ============
class VentanaRecepcion {
private:
  uint8_t esperado_;
  uint8_t tamano_;
  
public:
  VentanaRecepcion(uint8_t tamano = 4) : esperado_(0), tamano_(tamano) {}
  
  bool acepta(uint8_t seq) const {
    return (uint8_t)(seq - esperado_) < tamano_;
  }
  
  bool registrarRecibida(uint8_t seq) {
    if (seq != esperado_) return false;
    esperado_++;
    return true;
  }
  
  uint8_t ackEsperado() const {
    return esperado_;
  }
  
  void reiniciar() {
    esperado_ = 0;
  }
};

// ============ VARIABLES GLOBALES ============
Progreso progreso = {ESTADO_ESPERANDO, 0, 0, 0, 0, 0, 0};
VentanaRecepcion ventanaRx(4);
uint8_t bufferRx[Protocolo::TAM_MAX_TRAMA];
uint16_t posicionBuffer = 0;
unsigned long ultimoFrameTime = 0;
const unsigned long TIMEOUT_FRAME = 2000;

// ============ PROTOTIPOS ============
void procesarFrameRecibido(const Protocolo::Trama &trama);
bool decodificar(const uint8_t *entrada, uint16_t longitud, Protocolo::Trama &trama);
void procesarBytes(const uint8_t *entrada, uint16_t longitud);
void enviarAck(uint8_t ack);
void enviarNack(uint8_t ack);
void imprimirEstado();
void simularTransmisionNano();
void enviarTramaSimulada(const Protocolo::Trama &trama);
void enviarTrama(const Protocolo::Trama &trama);
Protocolo::Trama crearHandshake();
Protocolo::Trama crearDatos(uint8_t seq, const uint8_t *datos, uint16_t longitud);
Protocolo::Trama crearFin(uint8_t seq);

// ============ SETUP ============
void setup() {
  Serial.begin(9600);
  delay(1000);
  
  Serial.println(F("\n╔══════════════════════════════════════╗"));
  Serial.println(F("║  RECEIVER (Arduino Uno) - Redes     ║"));
  Serial.println(F("║  Estado: ESPERANDO                  ║"));
  Serial.println(F("║  Versión Tinkercad                  ║"));
  Serial.println(F("╚══════════════════════════════════════╝"));
  Serial.println(F(""));
  Serial.println(F("Comandos:"));
  Serial.println(F("  's' - Iniciar simulación (dummy frames)"));
  Serial.println(F("  'r' - Reiniciar receiver"));
  Serial.println(F("  'e' - Mostrar estado actual"));
  Serial.println(F(""));
  
  progreso.tiempoInicio = millis();
  progreso.estado = ESTADO_ESPERANDO;
}

// ============ LOOP PRINCIPAL ============
void loop() {
  if (Serial.available()) {
    char cmd = Serial.read();
    
    if (cmd == 's' || cmd == 'S') {
      Serial.println(F("\n>>> Iniciando simulación de transmisión..."));
      simularTransmisionNano();
      return;
    } 
    else if (cmd == 'r' || cmd == 'R') {
      Serial.println(F("\n>>> Reiniciando receiver..."));
      progreso.estado = ESTADO_ESPERANDO;
      progreso.framesRecibidos = 0;
      progreso.bytesRecibidos = 0;
      progreso.framesTotales = 0;
      progreso.tasaError = 0;
      ventanaRx.reiniciar();
      posicionBuffer = 0;
      Serial.println(F("Estado: ESPERANDO"));
      return;
    } 
    else if (cmd == 'e' || cmd == 'E') {
      imprimirEstado();
      return;
    }
  }

  if (progreso.estado == ESTADO_RECIBIENDO && 
      (millis() - ultimoFrameTime) > TIMEOUT_FRAME) {
    Serial.println(F("[ERROR] Timeout - Transacción abortada"));
    progreso.estado = ESTADO_ERROR;
    imprimirEstado();
    ultimoFrameTime = millis();
  }

  static unsigned long ultimaPrintTime = 0;
  if (progreso.estado == ESTADO_RECIBIENDO && 
      (millis() - ultimaPrintTime) > 10000) {
    imprimirEstado();
    ultimaPrintTime = millis();
  }
}

// ============ PROCESAR FRAME RECIBIDO ============
void procesarFrameRecibido(const Protocolo::Trama &trama) {
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
        progreso.framesTotales = 1;
        Serial.println(F("[HANDSHAKE] Conexión iniciada - Esperando datos..."));
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
          Serial.print(progreso.framesRecibidos);
          Serial.print(F("/"));
          Serial.print(progreso.framesTotales);
          Serial.print(F(" - "));
          Serial.print(trama.longitud);
          Serial.println(F(" bytes"));
          
          enviarAck(ventanaRx.ackEsperado());
        } else {
          Serial.println(F("[RETRANSMISIÓN] Ack duplicado"));
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
        enviarAck(ventanaRx.ackEsperado());
      }
      break;

    case Protocolo::TRAMA_ERROR:
      progreso.estado = ESTADO_ERROR;
      progreso.tasaError++;
      Serial.println(F("[ERROR REMOTO] Nano reportó error"));
      break;

    default:
      Serial.println(F("[DESCONOCIDA] Tipo de trama no reconocida"));
  }
}

// ============ CREAR TRAMAS ============
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

Protocolo::Trama crearDatos(uint8_t seq, const uint8_t *datos, uint16_t longitud) {
  Protocolo::Trama trama;
  trama.direccion = Protocolo::NODO_NANO;
  trama.control = Protocolo::TRAMA_DATOS;
  trama.seq = seq;
  trama.ack = 0;
  trama.longitud = (longitud > Protocolo::MAX_CARGA_UTIL) ? 
                   Protocolo::MAX_CARGA_UTIL : longitud;
  
  for (uint16_t i = 0; i < trama.longitud; i++) {
    trama.cargaUtil[i] = datos[i];
  }
  
  trama.crc = 0;
  return trama;
}

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

bool decodificar(const uint8_t *entrada, uint16_t longitud, Protocolo::Trama &trama) {
  if (longitud < 10) return false;
  if (entrada[0] != Protocolo::FLAG) return false;
  if (entrada[longitud - 1] != Protocolo::FLAG) return false;

  trama.direccion = entrada[1];
  trama.control = entrada[2];
  trama.seq = entrada[3];
  trama.ack = entrada[4];
  trama.longitud = ((uint16_t)entrada[5] << 8) | entrada[6];

  uint16_t esperado = 1 + 1 + 1 + 1 + 1 + 2 + trama.longitud + 2 + 1;
  if (longitud != esperado) return false;
  if (trama.longitud > Protocolo::MAX_CARGA_UTIL) return false;

  for (uint16_t i = 0; i < trama.longitud; i++) {
    trama.cargaUtil[i] = entrada[7 + i];
  }

  trama.crc = ((uint16_t)entrada[7 + trama.longitud] << 8) |
              entrada[7 + trama.longitud + 1];
  
  // El CRC no se verifica en esta versión Tinkercad, pero se deja para futuras mejoras.
  return true;
}

void procesarBytes(const uint8_t *entrada, uint16_t longitud) {
  Protocolo::Trama trama;
  if (!decodificar(entrada, longitud, trama)) {
    Serial.println(F("[ERROR] No se pudo decodificar la trama"));
    return;
  }
  ultimoFrameTime = millis();
  procesarFrameRecibido(trama);
}

void enviarTrama(const Protocolo::Trama &trama) {
  uint8_t buffer[Protocolo::TAM_MAX_TRAMA];
  uint16_t pos = 0;
  
  buffer[pos++] = Protocolo::FLAG;
  buffer[pos++] = trama.direccion;
  buffer[pos++] = trama.control;
  buffer[pos++] = trama.seq;
  buffer[pos++] = trama.ack;
  buffer[pos++] = (trama.longitud >> 8) & 0xFF;
  buffer[pos++] = trama.longitud & 0xFF;
  
  for (uint16_t i = 0; i < trama.longitud; i++) {
    buffer[pos++] = trama.cargaUtil[i];
  }
  
  uint16_t crc = calcularCRC(buffer + 1, 4 + 2 + trama.longitud);
  buffer[pos++] = (crc >> 8) & 0xFF;
  buffer[pos++] = crc & 0xFF;
  
  buffer[pos++] = Protocolo::FLAG;
  
  Serial.println(F("[RAW OUT] Trama codificada (simulación)."));
}

void enviarTramaSimulada(const Protocolo::Trama &trama) {
  uint8_t buffer[Protocolo::TAM_MAX_TRAMA];
  uint16_t pos = 0;
  
  buffer[pos++] = Protocolo::FLAG;
  buffer[pos++] = trama.direccion;
  buffer[pos++] = trama.control;
  buffer[pos++] = trama.seq;
  buffer[pos++] = trama.ack;
  buffer[pos++] = (trama.longitud >> 8) & 0xFF;
  buffer[pos++] = trama.longitud & 0xFF;
  
  for (uint16_t i = 0; i < trama.longitud; i++) {
    buffer[pos++] = trama.cargaUtil[i];
  }
  
  uint16_t crc = calcularCRC(buffer + 1, 4 + 2 + trama.longitud);
  buffer[pos++] = (crc >> 8) & 0xFF;
  buffer[pos++] = crc & 0xFF;
  
  buffer[pos++] = Protocolo::FLAG;
  
  procesarBytes(buffer, pos);
}

void enviarAck(uint8_t ack) {
  Protocolo::Trama ackTrama = crearHandshake();
  ackTrama.control = Protocolo::TRAMA_ACK;
  ackTrama.ack = ack;
  Serial.print(F("[ACK] Enviado Ack: "));
  Serial.println(ack);
}

void enviarNack(uint8_t ack) {
  Protocolo::Trama nackTrama = crearHandshake();
  nackTrama.control = Protocolo::TRAMA_NACK;
  nackTrama.ack = ack;
  Serial.print(F("[NACK] Enviado Nack: "));
  Serial.println(ack);
}

// ============ SIMULAR TRANSMISIÓN NANO ============
void simularTransmisionNano() {
  Serial.println(F("[SIMULACIÓN] Iniciando dummy frames del Nano...\n"));
  
  const char *datoDummy = "Hola_desde_Nano_en_Tinkercad";
  uint16_t longitudTotal = strlen(datoDummy);
  progreso.framesTotales = 3;
  progreso.bytesTotales = longitudTotal;
  
  // 1. HANDSHAKE
  delay(500);
  Protocolo::Trama handshake = crearHandshake();
  enviarTramaSimulada(handshake);
  Serial.println(F("[DUMMY] Enviado HANDSHAKE\n"));
  
  delay(1000);
  
  // 2. DATOS (3 frames)
  for (uint8_t i = 0; i < 3; i++) {
    uint16_t inicio = i * 10;
    uint16_t fin = inicio + 10;
    if (fin > longitudTotal) fin = longitudTotal;
    
    uint16_t longitud = fin - inicio;
    Protocolo::Trama datos = crearDatos(i, (uint8_t *)(datoDummy + inicio), longitud);
    enviarTramaSimulada(datos);
    
    Serial.print(F("[DUMMY] Enviado DATOS frame "));
    Serial.println(i);
    
    delay(800);
  }
  
  // 3. FIN
  delay(500);
  Protocolo::Trama fin = crearFin(3);
  enviarTramaSimulada(fin);
  Serial.println(F("[DUMMY] Enviado FIN\n"));
  
  Serial.println(F("[SIMULACIÓN] Completada\n"));
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
  
  if (progreso.bytesTotales > 0) {
    uint8_t porcentaje = (progreso.bytesRecibidos * 100) / progreso.bytesTotales;
    Serial.print(F("Progreso: "));
    Serial.print(porcentaje);
    Serial.println(F("%"));
  }
  
  Serial.print(F("Tasa Error: "));
  Serial.print(progreso.tasaError);
  Serial.println(F("%"));
  
  Serial.print(F("Tiempo: "));
  Serial.print(tiempoTranscurrido);
  Serial.println(F("s\n"));
}
