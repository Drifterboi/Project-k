// TEST_COMUNICACION_DIRECTA.ino
// Compila esto en Arduino Nano para diagnosticar comunicación con Arduino Uno

#include <SoftwareSerial.h>

// Configurar SoftwareSerial en pines 10 (RX) y 11 (TX)
SoftwareSerial softSerial(11, 10);  // RX, TX

void setup() {
  Serial.begin(115200);  // Serial Monitor en 115200
  delay(1000);
  
  Serial.println("\n=== TEST COMUNICACION DIRECTA ===");
  Serial.println("Este test envía mensajes simples entre Arduinos");
  Serial.println("Nano: SoftwareSerial pins 10,11 @ 4800 bps");
  Serial.println("Uno:  SoftwareSerial pins 10,11 @ 4800 bps");
  
  // Inicializar SoftwareSerial a 4800 bps (igual que el Uno)
  softSerial.begin(4800);
  delay(1000);
  
  Serial.println("\nEnviando TEST_NANO al Uno...");
  softSerial.println("TEST_NANO");
  softSerial.flush();
}

void loop() {
  // Ver si recibimos algo del Uno
  if (softSerial.available()) {
    String msg = softSerial.readStringUntil('\n');
    Serial.print("[RX] Recibido: ");
    Serial.println(msg);
  }
  
  // Cada 5 segundos, envía un PING
  static unsigned long lastTime = 0;
  if (millis() - lastTime > 5000) {
    lastTime = millis();
    Serial.print("[TX] Enviando PING_");
    Serial.print(lastTime / 1000);
    Serial.println("...");
    softSerial.print("PING_");
    softSerial.println(lastTime / 1000);
    softSerial.flush();
  }
}
