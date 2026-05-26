// SimpleSender.ino - Arduino Nano
// Envía "HOLA" cada segundo al Uno usando SoftwareSerial

#include <SoftwareSerial.h>

// RX en pin 11, TX en pin 10
SoftwareSerial serialSoft(11, 10);  // RX, TX

void setup() {
  Serial.begin(9600);  // Para debugging en Serial Monitor (USB)
  serialSoft.begin(4800);  // Comunicación con Uno
  delay(1000);
  Serial.println("[SENDER] Iniciado. Enviando HOLA cada segundo...");
}

void loop() {
  // Si llega algo por USB (desde Python), leer y reenviar por SoftwareSerial
  if (Serial.available() > 0) {
    String msg = Serial.readStringUntil('\n');
    msg.trim();
    if (msg.length() > 0) {
      serialSoft.println(msg);
      Serial.print("[SENDER] Forwarded: ");
      Serial.println(msg);
    }
  }

  // Heartbeat ocasional para diagnóstico (cada 10s)
  static unsigned long lastHeartbeat = 0;
  if (millis() - lastHeartbeat > 10000) {
    serialSoft.println("HOLA");
    Serial.println("[SENDER] Enviado: HOLA");
    lastHeartbeat = millis();
  }
}
