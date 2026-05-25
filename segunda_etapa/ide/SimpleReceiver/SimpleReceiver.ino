// SimpleReceiver.ino - Arduino Uno
// Recibe "HOLA" del Nano usando SoftwareSerial

#include <SoftwareSerial.h>

// RX en pin 11, TX en pin 10
SoftwareSerial serialSoft(11, 10);  // RX, TX

void setup() {
  Serial.begin(9600);  // Para debugging en Serial Monitor (USB)
  serialSoft.begin(4800);  // Comunicación con Nano
  delay(1000);
  Serial.println("[RECEIVER] Iniciado. Esperando mensaje...");
}

void loop() {
  if (serialSoft.available() > 0) {
    String mensaje = serialSoft.readStringUntil('\n');
    Serial.print("[RECEIVED] ");
    Serial.println(mensaje);
  }
}
