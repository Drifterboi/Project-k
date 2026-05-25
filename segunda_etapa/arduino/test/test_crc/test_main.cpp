#include <stdio.h>
#include "../../lib/enlace/enlace.h"

int main() {

    printf("\n=== INICIO DE PRUEBAS ===\n\n");

    uint8_t datos[] = {10,20,30,40};

    printf("Creando trama...\n");

    auto trama = Enlace::crearTrama(
        Protocolo::NODO_UNO,
        Protocolo::TRAMA_DATOS,
        0,
        0,
        datos,
        4
    );

    printf("Trama creada correctamente\n");

    uint8_t buffer[Protocolo::TAM_MAX_TRAMA];
    uint16_t longitud = 0;

    printf("\nCodificando trama...\n");

    auto estadoCod = Enlace::codificar(
        trama,
        buffer,
        sizeof(buffer),
        longitud
    );

    if(estadoCod == Enlace::CODIFICACION_OK) {
        printf("Codificacion OK\n");
    } else {
        printf("Codificacion FALLO\n");
        return 1;
    }

    printf("\nBytes de trama:\n");

    for(uint16_t i = 0; i < longitud; i++) {
        printf("%02X ", buffer[i]);
    }

    printf("\n");

    printf("\nDecodificando trama...\n");

    auto resultado = Enlace::decodificar(buffer, longitud);

    if(resultado.estado == Enlace::DECODIFICACION_OK) {
        printf("CRC VALIDO\n");
    } else {
        printf("CRC INVALIDO\n");
        return 1;
    }

    printf("\nVerificando carga util...\n");

    for(uint16_t i = 0; i < resultado.trama.longitud; i++) {
        printf("Dato %d = %d\n", i, resultado.trama.cargaUtil[i]);
    }

    printf("\nCorrompiendo trama...\n");

    buffer[8] ^= 0xFF;

    auto corrupto = Enlace::decodificar(buffer, longitud);

    if(corrupto.estado == Enlace::DECODIFICACION_CRC_INVALIDO) {
        printf("ERROR CRC DETECTADO CORRECTAMENTE\n");
    } else {
        printf("FALLO EN DETECCION CRC\n");
        return 1;
    }

    printf("\nProbando ACK...\n");

    auto ack = Enlace::crearAck(
        Protocolo::NODO_NANO,
        1
    );

    uint8_t bufferAck[Protocolo::TAM_MAX_TRAMA];
    uint16_t longitudAck = 0;

    Enlace::codificar(
        ack,
        bufferAck,
        sizeof(bufferAck),
        longitudAck
    );

    auto resultadoAck =
        Enlace::decodificar(bufferAck, longitudAck);

    if(resultadoAck.estado == Enlace::DECODIFICACION_OK &&
       resultadoAck.trama.control == Protocolo::TRAMA_ACK) {

        printf("ACK VALIDO\n");
    }
    else {
        printf("ACK INVALIDO\n");
        return 1;
    }

    printf("\nProbando ventana transmision...\n");

    Enlace::VentanaTransmision ventanaTx;

    printf("Seq inicial: %d\n", ventanaTx.siguienteSeq());

    ventanaTx.registrarEnvio();

    printf("Seq despues envio: %d\n", ventanaTx.siguienteSeq());

    ventanaTx.registrarAck(1);

    printf("ACK registrado correctamente\n");

    printf("\n=== TODAS LAS PRUEBAS PASARON ===\n");

    return 0;
}