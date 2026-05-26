// TEST_RECEIVER_ECHO.ino
// Compila esto en Arduino Uno para diagnosticar comunicación con Arduino Nano

#include <SoftwareSerial.h>

// Configurar SoftwareSerial en pines 10 (RX) y 11 (TX)
// INVERTIDO respecto a Nano: Nano TX(10) -> Uno RX(11), Nano RX(11) -> Uno TX(10)
SoftwareSerial softSerial(11, 10);  // RX, TX

void setup() {
  Serial.begin(115200);  // Serial Monitor en 115200
  delay(1000);
  
  Serial.println("\n=== TEST RECEIVER ECHO ===");
  Serial.println("Este test escucha al Nano en SoftwareSerial");
  Serial.println("Uno: SoftwareSerial pins 10,11 @ 4800 bps");
  
  // Inicializar SoftwareSerial a 4800 bps (igual que el Nano)
  softSerial.begin(4800);
  delay(1000);
  
  Serial.println("\nEsperando mensajes del Nano...");
}

void loop() {
  // Ver si recibimos algo del Nano
  if (softSerial.available()) {
    String msg = softSerial.readStringUntil('\n');
    Serial.print("[UNO RX] Recibido: ");
    Serial.println(msg);
    
    // Echo de vuelta
    Serial.print("[UNO TX] Devolviendo: ECHO_");
    Serial.println(msg);
    softSerial.print("ECHO_");
    softSerial.println(msg);
    softSerial.flush();
  }
  
  // Debug cada 10 segundos
  static unsigned long lastDebug = 0;
  if (millis() - lastDebug > 10000) {
    lastDebug = millis();
    Serial.println("[DEBUG] Sigo aqui, esperando mensajes...");
  }
}
