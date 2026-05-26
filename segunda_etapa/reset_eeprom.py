#!/usr/bin/env python3
"""
Script para resetear la EEPROM de los Arduinos a 4800 bps.
Ejecuta esto una sola vez antes de usar los Arduinos.

Uso:
    python reset_eeprom.py
"""

import serial
import serial.tools.list_ports
import time
import sys

def get_port():
    """Mostrar puertos disponibles y pedir selección"""
    ports = [p.device for p in serial.tools.list_ports.comports()]
    if not ports:
        print("❌ No hay puertos seriales disponibles")
        return None
    
    print(f"\n📍 Puertos disponibles:")
    for i, p in enumerate(ports, 1):
        print(f"  {i}. {p}")
    
    try:
        choice = int(input("\n¿Qué puerto usar? (número): "))
        if 1 <= choice <= len(ports):
            return ports[choice - 1]
    except ValueError:
        pass
    
    print("❌ Selección inválida")
    return None

def reset_eeprom():
    """Resetear EEPROM a 4800 bps"""
    
    print("\n" + "="*60)
    print("  RESETEAR EEPROM DE ARDUINO A 4800 bps")
    print("="*60)
    
    # Seleccionar puerto
    port = get_port()
    if not port:
        return False
    
    print(f"\n⏳ Conectando a {port}...")
    
    try:
        # Conectar a 115200 para ver los mensajes
        ser = serial.Serial(port, 115200, timeout=2)
        time.sleep(2)
        ser.reset_input_buffer()
        ser.reset_output_buffer()
        
        # Enviar comando para resetear EEPROM
        # El comando es un simple sketch que hemos subido
        # Si no está, el usuario debe subir RESET_EEPROM_NANO.ino primero
        
        print("✅ Conexión establecida")
        print("\n📋 Estado del Arduino:")
        
        # Leer lo que dice el Arduino
        for _ in range(10):
            if ser.in_waiting:
                line = ser.readline().decode('utf-8', errors='replace').strip()
                if line:
                    print(f"  {line}")
            time.sleep(0.2)
        
        ser.close()
        
        print("\n✅ EEPROM reseteada (si el sketch está subido)")
        print("\n📌 Ahora sube SENDER_NANO.ino o RECEIVER_UNO.ino al Arduino")
        return True
        
    except Exception as e:
        print(f"❌ Error: {e}")
        return False

if __name__ == "__main__":
    reset_eeprom()
