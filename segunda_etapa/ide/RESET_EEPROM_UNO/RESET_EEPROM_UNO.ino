// ================================================
// SKETCH PARA RESETEAR EEPROM A 4800 bps
// ================================================
// Sube esto a RECEIVER_UNO (COM3)
// Abre Serial Monitor a 115200 bps
// Cuando diga [OK], ya está listo
// Luego sube RECEIVER_UNO.ino normalmente
// ================================================

#include <EEPROM.h>
#include <avr/wdt.h>

#define EEPROM_BAUD_ADDR 0

void setup() {
  Serial.begin(115200);
  delay(2000);
  
  Serial.println(F("\n\n╔════════════════════════════════════════╗"));
  Serial.println(F("║  RESETEAR EEPROM A 4800 bps            ║"));
  Serial.println(F("╚════════════════════════════════════════╝\n"));
  
  // Leer valor actual
  uint8_t byte1 = EEPROM.read(EEPROM_BAUD_ADDR);
  uint8_t byte2 = EEPROM.read(EEPROM_BAUD_ADDR + 1);
  uint16_t baudrateActual = ((uint16_t)byte1 << 8) | byte2;
  
  Serial.print(F("📍 Baudrate actual en EEPROM: "));
  if (baudrateActual == 0xFFFF || baudrateActual == 0) {
    Serial.println(F("VACÍO"));
  } else {
    Serial.println(baudrateActual);
  }
  
  // Guardar 4800 en EEPROM (2 bytes)
  Serial.println(F("\n⏳ Guardando 4800 bps en EEPROM..."));
  EEPROM.write(EEPROM_BAUD_ADDR, (uint8_t)(4800 >> 8));      // byte alto
  EEPROM.write(EEPROM_BAUD_ADDR + 1, (uint8_t)(4800 & 0xFF)); // byte bajo
  
  delay(200);
  
  // Verificar que se grabó correctamente
  byte1 = EEPROM.read(EEPROM_BAUD_ADDR);
  byte2 = EEPROM.read(EEPROM_BAUD_ADDR + 1);
  uint16_t nuevoBaudrate = ((uint16_t)byte1 << 8) | byte2;
  
  Serial.print(F("📍 Nuevo baudrate en EEPROM: "));
  Serial.println(nuevoBaudrate);
  
  if (nuevoBaudrate == 4800) {
    Serial.println(F("\n✅ [OK] EEPROM RESETEADA CORRECTAMENTE"));
    Serial.println(F("\n📌 PRÓXIMOS PASOS:"));
    Serial.println(F("   1. Cierra este sketch"));
    Serial.println(F("   2. Sube RECEIVER_UNO.ino al Uno"));
    Serial.println(F("   3. Abre Serial Monitor a 4800 bps"));
    Serial.println(F("   4. Ejecuta: python src/cliente/main.py\n"));
  } else {
    Serial.println(F("\n❌ ERROR: EEPROM no se grabó correctamente"));
  }
}

void loop() {
  delay(1000);
}
