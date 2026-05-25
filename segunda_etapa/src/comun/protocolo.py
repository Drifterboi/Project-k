"""Shared protocol definitions for Arduino/Python integration."""

from __future__ import annotations

from enum import IntEnum
from typing import Dict


class Nodo(IntEnum):
    NANO = 0x01
    UNO = 0x02
    BROADCAST = 0xFF


class TipoTrama(IntEnum):
    DATOS = 0x01
    ACK = 0x02
    NACK = 0x03
    HANDSHAKE = 0x04
    FIN = 0x05
    ERROR = 0x06


FLAG = 0x7E
MAX_CARGA_UTIL = 64
TAM_LONGITUD = 2
TAM_CRC = 2
TAM_ENCABEZADO = 1 + 1 + 1 + 1 + 1 + TAM_LONGITUD
TAM_CONTROL_FINAL = TAM_CRC + 1
TAM_MAX_TRAMA = TAM_ENCABEZADO + MAX_CARGA_UTIL + TAM_CONTROL_FINAL


TIPO_TRAMA_STR: Dict[int, str] = {
    TipoTrama.DATOS: 'TRAMA_DATOS',
    TipoTrama.ACK: 'TRAMA_ACK',
    TipoTrama.NACK: 'TRAMA_NACK',
    TipoTrama.HANDSHAKE: 'TRAMA_HANDSHAKE',
    TipoTrama.FIN: 'TRAMA_FIN',
    TipoTrama.ERROR: 'TRAMA_ERROR',
}


def tipo_trama_to_str(tipo: int) -> str:
    return TIPO_TRAMA_STR.get(tipo, f'UNKNOWN(0x{tipo:02X})')
