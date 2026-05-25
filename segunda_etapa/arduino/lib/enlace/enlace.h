#ifndef ENLACE_H
#define ENLACE_H

#include <Arduino.h>
#include "../protocolo/protocolo.h"

namespace Enlace {

const uint8_t VENTANA_PREDETERMINADA = 4;
const uint16_t TIMEOUT_RETRANSMISION_MS = 1000;
const uint8_t MAX_REINTENTOS = 5;

enum EstadoCodificacion : uint8_t {
  CODIFICACION_OK,
  CODIFICACION_CARGA_MUY_GRANDE,
  CODIFICACION_BUFFER_INSUFICIENTE
};

typedef bool (*EscritorByte)(uint8_t byte);

enum EstadoDecodificacion : uint8_t {
  DECODIFICACION_OK,
  DECODIFICACION_TRAMA_INCOMPLETA,
  DECODIFICACION_BANDERA_INVALIDA,
  DECODIFICACION_TIPO_INVALIDO,
  DECODIFICACION_CARGA_MUY_GRANDE,
  DECODIFICACION_CRC_INVALIDO
};

struct ResultadoDecodificacion {
  EstadoDecodificacion estado;
  Protocolo::Trama trama;
};

enum EstadoParserStreaming : uint8_t {
  PARSER_EN_CURSO,
  PARSER_TRAMA_COMPLETA,
  PARSER_TRAMA_INVALIDA
};

const uint8_t PAYLOAD_MUESTRA_MAX = 12;

struct TramaLigera {
  uint8_t direccion;
  uint8_t control;
  uint8_t seq;
  uint8_t ack;
  uint16_t longitud;
  uint16_t crc;
  uint8_t payloadMuestra[PAYLOAD_MUESTRA_MAX];
  uint8_t payloadMuestraLongitud;
};

class ParserStreaming {
public:
  ParserStreaming();

  void reiniciar();
  EstadoParserStreaming procesarByte(uint8_t byte,
                                     TramaLigera &trama,
                                     EstadoDecodificacion &error);

private:
  enum EstadoInterno : uint8_t {
    ESPERANDO_FLAG,
    DIRECCION,
    CONTROL,
    SEQ,
    ACK,
    LONGITUD_ALTA,
    LONGITUD_BAJA,
    PAYLOAD,
    CRC_ALTO,
    CRC_BAJO,
    FLAG_FINAL
  };

  void prepararTrama();
  void acumularCrc(uint8_t byte);
  void guardarPayload(uint8_t byte);
  EstadoParserStreaming invalidar(EstadoDecodificacion error,
                                  EstadoDecodificacion &salida);

  EstadoInterno estado_;
  TramaLigera trama_;
  uint16_t crcCalculado_;
  uint16_t payloadLeido_;
  uint8_t crcAlto_;
};

class VentanaTransmision {
public:
  explicit VentanaTransmision(uint8_t tamanoVentana = VENTANA_PREDETERMINADA);

  bool puedeEnviar() const;
  uint8_t siguienteSeq() const;
  void registrarEnvio();
  bool registrarAck(uint8_t ack);
  void reiniciar();

private:
  uint8_t base_;
  uint8_t siguiente_;
  uint8_t tamano_;
};

class VentanaRecepcion {
public:
  explicit VentanaRecepcion(uint8_t tamanoVentana = VENTANA_PREDETERMINADA);

  bool acepta(uint8_t seq) const;
  bool registrarRecibida(uint8_t seq);
  uint8_t ackEsperado() const;
  void reiniciar();

private:
  uint8_t esperado_;
  uint8_t tamano_;
};

struct TramaPendiente {
  uint8_t seq;
  uint32_t offset;
  uint16_t longitud;
  uint8_t reintentos;
  unsigned long ultimoEnvio;
  bool activa;
};

class GestorVentanaTransmision {
public:
  static const uint8_t MAX_VENTANA = 5;

  explicit GestorVentanaTransmision(uint8_t tamanoVentana = VENTANA_PREDETERMINADA);

  void configurar(uint8_t tamanoVentana);
  void reiniciar();
  bool puedeEnviar() const;
  bool registrarEnvio(uint8_t seq, uint32_t offset, uint16_t longitud, unsigned long ahora);
  bool confirmarHasta(uint8_t ack, uint16_t &bytesConfirmados, uint8_t &framesConfirmados);
  uint8_t pendientesActivas() const;
  TramaPendiente *pendiente(uint8_t indice);

private:
  TramaPendiente pendientes_[MAX_VENTANA];
  uint8_t tamano_;
  uint8_t activas_;
};

Protocolo::Trama crearTrama(uint8_t direccion,
                            uint8_t control,
                            uint8_t seq,
                            uint8_t ack,
                            const uint8_t *cargaUtil,
                            uint16_t longitud);

Protocolo::Trama crearAck(uint8_t direccion, uint8_t ack);
Protocolo::Trama crearNack(uint8_t direccion, uint8_t ack);
Protocolo::Trama crearHandshake(uint8_t direccion);
Protocolo::Trama crearTramaFin(uint8_t direccion);

EstadoCodificacion codificar(const Protocolo::Trama &trama,
                             uint8_t *salida,
                             uint16_t capacidadSalida,
                             uint16_t &longitudSalida);

ResultadoDecodificacion decodificar(const uint8_t *entrada, uint16_t longitudEntrada);

EstadoCodificacion emitirTrama(uint8_t direccion,
                                uint8_t control,
                                uint8_t seq,
                                uint8_t ack,
                                const uint8_t *cargaUtil,
                                uint16_t longitud,
                                EscritorByte escritor);

} // namespace Enlace

#endif
