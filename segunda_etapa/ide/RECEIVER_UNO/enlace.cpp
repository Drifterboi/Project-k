#include "enlace.h"
#include "crc.h"

namespace Enlace {

static bool secuenciaEnVentana(uint8_t valor, uint8_t base, uint8_t tamano) {
  return static_cast<uint8_t>(valor - base) < tamano;
}

ParserStreaming::ParserStreaming() {
  reiniciar();
}

void ParserStreaming::reiniciar() {
  estado_ = ESPERANDO_FLAG;
  prepararTrama();
}

void ParserStreaming::prepararTrama() {
  trama_.direccion = 0;
  trama_.control = 0;
  trama_.seq = 0;
  trama_.ack = 0;
  trama_.longitud = 0;
  trama_.crc = 0;
  trama_.payloadMuestra = payloadBuffer_;
  trama_.payloadMuestraLongitud = 0;
  crcCalculado_ = crc16Inicial();
  payloadLeido_ = 0;
  crcAlto_ = 0;
}

void ParserStreaming::acumularCrc(uint8_t byte) {
  crcCalculado_ = actualizarCrc16(crcCalculado_, byte);
}

void ParserStreaming::guardarPayload(uint8_t byte) {
  if (trama_.payloadMuestraLongitud < PAYLOAD_MUESTRA_MAX) {
    payloadBuffer_[trama_.payloadMuestraLongitud++] = byte;
  }
}

EstadoParserStreaming ParserStreaming::invalidar(EstadoDecodificacion error,
                                                 EstadoDecodificacion &salida) {
  salida = error;
  reiniciar();
  return PARSER_TRAMA_INVALIDA;
}

EstadoParserStreaming ParserStreaming::procesarByte(uint8_t byte,
                                                    TramaLigera &trama,
                                                    EstadoDecodificacion &error) {
  error = DECODIFICACION_OK;

  switch (estado_) {
    case ESPERANDO_FLAG:
      if (byte == Protocolo::FLAG) {
        prepararTrama();
        estado_ = DIRECCION;
      }
      return PARSER_EN_CURSO;

    case DIRECCION:
      trama_.direccion = byte;
      acumularCrc(byte);
      estado_ = CONTROL;
      return PARSER_EN_CURSO;

    case CONTROL:
      if (!Protocolo::esTipoValido(byte)) {
        return invalidar(DECODIFICACION_TIPO_INVALIDO, error);
      }
      trama_.control = byte;
      acumularCrc(byte);
      estado_ = SEQ;
      return PARSER_EN_CURSO;

    case SEQ:
      trama_.seq = byte;
      acumularCrc(byte);
      estado_ = ACK;
      return PARSER_EN_CURSO;

    case ACK:
      trama_.ack = byte;
      acumularCrc(byte);
      estado_ = LONGITUD_ALTA;
      return PARSER_EN_CURSO;

    case LONGITUD_ALTA:
      trama_.longitud = static_cast<uint16_t>(byte) << 8;
      acumularCrc(byte);
      estado_ = LONGITUD_BAJA;
      return PARSER_EN_CURSO;

    case LONGITUD_BAJA:
      trama_.longitud |= byte;
      acumularCrc(byte);
      if (trama_.longitud > Protocolo::MAX_CARGA_UTIL_PROYECTO) {
        return invalidar(DECODIFICACION_CARGA_MUY_GRANDE, error);
      }
      estado_ = trama_.longitud == 0 ? CRC_ALTO : PAYLOAD;
      return PARSER_EN_CURSO;

    case PAYLOAD:
      guardarPayload(byte);
      acumularCrc(byte);
      payloadLeido_++;
      if (payloadLeido_ >= trama_.longitud) {
        estado_ = CRC_ALTO;
      }
      return PARSER_EN_CURSO;

    case CRC_ALTO:
      crcAlto_ = byte;
      estado_ = CRC_BAJO;
      return PARSER_EN_CURSO;

    case CRC_BAJO:
      trama_.crc = word(crcAlto_, byte);
      estado_ = FLAG_FINAL;
      return PARSER_EN_CURSO;

    case FLAG_FINAL:
      if (byte != Protocolo::FLAG) {
        return invalidar(DECODIFICACION_BANDERA_INVALIDA, error);
      }
      if (trama_.crc != crcCalculado_) {
        return invalidar(DECODIFICACION_CRC_INVALIDO, error);
      }
      trama = trama_;
      reiniciar();
      return PARSER_TRAMA_COMPLETA;
  }

  return invalidar(DECODIFICACION_TRAMA_INCOMPLETA, error);
}

VentanaTransmision::VentanaTransmision(uint8_t tamanoVentana)
    : base_(0), siguiente_(0), tamano_(tamanoVentana) {}

bool VentanaTransmision::puedeEnviar() const {
  return secuenciaEnVentana(siguiente_, base_, tamano_);
}

uint8_t VentanaTransmision::siguienteSeq() const {
  return siguiente_;
}

void VentanaTransmision::registrarEnvio() {
  siguiente_++;
}

bool VentanaTransmision::registrarAck(uint8_t ack) {
  if (!secuenciaEnVentana(ack, base_, static_cast<uint8_t>(tamano_ + 1))) {
    return false;
  }

  base_ = ack;
  return true;
}

void VentanaTransmision::reiniciar() {
  base_ = 0;
  siguiente_ = 0;
}

VentanaRecepcion::VentanaRecepcion(uint8_t tamanoVentana)
    : esperado_(0), tamano_(tamanoVentana) {}

bool VentanaRecepcion::acepta(uint8_t seq) const {
  return secuenciaEnVentana(seq, esperado_, tamano_);
}

bool VentanaRecepcion::registrarRecibida(uint8_t seq) {
  if (seq != esperado_) {
    return false;
  }

  esperado_++;
  return true;
}

uint8_t VentanaRecepcion::ackEsperado() const {
  return esperado_;
}

void VentanaRecepcion::reiniciar() {
  esperado_ = 0;
}

GestorVentanaTransmision::GestorVentanaTransmision(uint8_t tamanoVentana)
    : tamano_(tamanoVentana), activas_(0) {
  reiniciar();
  configurar(tamanoVentana);
}

void GestorVentanaTransmision::configurar(uint8_t tamanoVentana) {
  if (tamanoVentana < 1) tamanoVentana = 1;
  if (tamanoVentana > MAX_VENTANA) tamanoVentana = MAX_VENTANA;
  tamano_ = tamanoVentana;
}

void GestorVentanaTransmision::reiniciar() {
  activas_ = 0;
  for (uint8_t i = 0; i < MAX_VENTANA; i++) {
    pendientes_[i].seq = 0;
    pendientes_[i].offset = 0;
    pendientes_[i].longitud = 0;
    pendientes_[i].reintentos = 0;
    pendientes_[i].ultimoEnvio = 0;
    pendientes_[i].activa = false;
  }
}

bool GestorVentanaTransmision::puedeEnviar() const {
  return activas_ < tamano_;
}

bool GestorVentanaTransmision::registrarEnvio(uint8_t seq, uint32_t offset, uint16_t longitud, unsigned long ahora) {
  if (!puedeEnviar()) {
    return false;
  }

  for (uint8_t i = 0; i < MAX_VENTANA; i++) {
    if (!pendientes_[i].activa) {
      pendientes_[i].seq = seq;
      pendientes_[i].offset = offset;
      pendientes_[i].longitud = longitud;
      pendientes_[i].reintentos = 0;
      pendientes_[i].ultimoEnvio = ahora;
      pendientes_[i].activa = true;
      activas_++;
      return true;
    }
  }

  return false;
}

bool GestorVentanaTransmision::confirmarHasta(uint8_t ack, uint16_t &bytesConfirmados, uint8_t &framesConfirmados) {
  bytesConfirmados = 0;
  framesConfirmados = 0;

  for (uint8_t i = 0; i < MAX_VENTANA; i++) {
    if (pendientes_[i].activa && static_cast<uint8_t>(pendientes_[i].seq + 1) <= ack) {
      bytesConfirmados += pendientes_[i].longitud;
      framesConfirmados++;
      pendientes_[i].activa = false;
      activas_--;
    }
  }

  return framesConfirmados > 0;
}

uint8_t GestorVentanaTransmision::pendientesActivas() const {
  return activas_;
}

TramaPendiente *GestorVentanaTransmision::pendiente(uint8_t indice) {
  if (indice >= MAX_VENTANA) {
    return nullptr;
  }

  return &pendientes_[indice];
}

Protocolo::Trama crearTrama(uint8_t direccion,
                            uint8_t control,
                            uint8_t seq,
                            uint8_t ack,
                            const uint8_t *cargaUtil,
                            uint16_t longitud) {
  Protocolo::Trama trama;
  trama.direccion = direccion;
  trama.control = control;
  trama.seq = seq;
  trama.ack = ack;
  trama.longitud = longitud;
  trama.crc = 0;

  const uint16_t bytesCopiados = longitud > Protocolo::MAX_CARGA_UTIL ? 0 : longitud;
  for (uint16_t i = 0; i < bytesCopiados; i++) {
    trama.cargaUtil[i] = cargaUtil == nullptr ? 0 : cargaUtil[i];
  }

  return trama;
}

Protocolo::Trama crearAck(uint8_t direccion, uint8_t ack) {
  return crearTrama(direccion, Protocolo::TRAMA_ACK, 0, ack, nullptr, 0);
}

Protocolo::Trama crearNack(uint8_t direccion, uint8_t ack) {
  return crearTrama(direccion, Protocolo::TRAMA_NACK, 0, ack, nullptr, 0);
}

Protocolo::Trama crearHandshake(uint8_t direccion) {
  return crearTrama(direccion, Protocolo::TRAMA_HANDSHAKE, 0, 0, nullptr, 0);
}

Protocolo::Trama crearTramaFin(uint8_t direccion) {
  return crearTrama(direccion, Protocolo::TRAMA_FIN, 0, 0, nullptr, 0);
}

EstadoCodificacion codificar(const Protocolo::Trama &trama,
                             uint8_t *salida,
                             uint16_t capacidadSalida,
                             uint16_t &longitudSalida) {
  longitudSalida = 0;

  if (trama.longitud > Protocolo::MAX_CARGA_UTIL) {
    return CODIFICACION_CARGA_MUY_GRANDE;
  }

  const uint16_t necesaria = Protocolo::TAM_ENCABEZADO + trama.longitud + Protocolo::TAM_CONTROL_FINAL;
  if (capacidadSalida < necesaria) {
    return CODIFICACION_BUFFER_INSUFICIENTE;
  }

  salida[longitudSalida++] = Protocolo::FLAG;
  salida[longitudSalida++] = trama.direccion;
  salida[longitudSalida++] = trama.control;
  salida[longitudSalida++] = trama.seq;
  salida[longitudSalida++] = trama.ack;
  salida[longitudSalida++] = highByte(trama.longitud);
  salida[longitudSalida++] = lowByte(trama.longitud);

  for (uint16_t i = 0; i < trama.longitud; i++) {
    salida[longitudSalida++] = trama.cargaUtil[i];
  }

  const uint16_t crc = calcularCrc16(&salida[1], static_cast<uint16_t>(longitudSalida - 1));
  salida[longitudSalida++] = highByte(crc);
  salida[longitudSalida++] = lowByte(crc);
  salida[longitudSalida++] = Protocolo::FLAG;

  return CODIFICACION_OK;
}

ResultadoDecodificacion decodificar(const uint8_t *entrada, uint16_t longitudEntrada) {
  ResultadoDecodificacion resultado;
  resultado.estado = DECODIFICACION_OK;
  resultado.trama = crearTrama(0, 0, 0, 0, nullptr, 0);

  if (longitudEntrada < Protocolo::TAM_ENCABEZADO + Protocolo::TAM_CONTROL_FINAL) {
    resultado.estado = DECODIFICACION_TRAMA_INCOMPLETA;
    return resultado;
  }

  if (entrada[0] != Protocolo::FLAG || entrada[longitudEntrada - 1] != Protocolo::FLAG) {
    resultado.estado = DECODIFICACION_BANDERA_INVALIDA;
    return resultado;
  }

  const uint8_t control = entrada[2];
  if (!Protocolo::esTipoValido(control)) {
    resultado.estado = DECODIFICACION_TIPO_INVALIDO;
    return resultado;
  }

  const uint16_t longitudCarga = word(entrada[5], entrada[6]);
  if (longitudCarga > Protocolo::MAX_CARGA_UTIL) {
    resultado.estado = DECODIFICACION_CARGA_MUY_GRANDE;
    return resultado;
  }

  const uint16_t esperada = Protocolo::TAM_ENCABEZADO + longitudCarga + Protocolo::TAM_CONTROL_FINAL;
  if (longitudEntrada != esperada) {
    resultado.estado = DECODIFICACION_TRAMA_INCOMPLETA;
    return resultado;
  }

  const uint16_t crcRecibido = word(entrada[longitudEntrada - 3], entrada[longitudEntrada - 2]);
  const uint16_t crcCalculado = calcularCrc16(&entrada[1], static_cast<uint16_t>(longitudEntrada - 4));
  if (crcRecibido != crcCalculado) {
    resultado.estado = DECODIFICACION_CRC_INVALIDO;
    return resultado;
  }

  resultado.trama.direccion = entrada[1];
  resultado.trama.control = control;
  resultado.trama.seq = entrada[3];
  resultado.trama.ack = entrada[4];
  resultado.trama.longitud = longitudCarga;
  resultado.trama.crc = crcRecibido;

  for (uint16_t i = 0; i < longitudCarga; i++) {
    resultado.trama.cargaUtil[i] = entrada[Protocolo::TAM_ENCABEZADO + i];
  }

  return resultado;
}

EstadoCodificacion emitirTrama(uint8_t direccion,
                                uint8_t control,
                                uint8_t seq,
                                uint8_t ack,
                                const uint8_t *cargaUtil,
                                uint16_t longitud,
                                EscritorByte escritor) {
  if (longitud > Protocolo::MAX_CARGA_UTIL_PROYECTO) {
    return CODIFICACION_CARGA_MUY_GRANDE;
  }

  if (escritor == nullptr || (cargaUtil == nullptr && longitud > 0)) {
    return CODIFICACION_BUFFER_INSUFICIENTE;
  }

  uint16_t crc = crc16Inicial();

  if (!escritor(Protocolo::FLAG)) return CODIFICACION_BUFFER_INSUFICIENTE;

  const uint8_t encabezado[] = {
    direccion,
    control,
    seq,
    ack,
    highByte(longitud),
    lowByte(longitud)
  };

  for (uint8_t i = 0; i < sizeof(encabezado); i++) {
    crc = actualizarCrc16(crc, encabezado[i]);
    if (!escritor(encabezado[i])) return CODIFICACION_BUFFER_INSUFICIENTE;
  }

  for (uint16_t i = 0; i < longitud; i++) {
    crc = actualizarCrc16(crc, cargaUtil[i]);
    if (!escritor(cargaUtil[i])) return CODIFICACION_BUFFER_INSUFICIENTE;
  }

  if (!escritor(highByte(crc))) return CODIFICACION_BUFFER_INSUFICIENTE;
  if (!escritor(lowByte(crc))) return CODIFICACION_BUFFER_INSUFICIENTE;
  if (!escritor(Protocolo::FLAG)) return CODIFICACION_BUFFER_INSUFICIENTE;

  return CODIFICACION_OK;
}

} // namespace Enlace
