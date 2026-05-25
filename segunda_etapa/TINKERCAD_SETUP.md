# Setup del Receiver en Tinkercad

## Resumen
Tu firmware Arduino está listo para usarse en Tinkercad. Implementa:
- ✅ Máquina de estados completa (waiting → receiving → complete/error)
- ✅ Validación de frames con CRC
- ✅ Ventana deslizante de recepción
- ✅ Dummy frames para simular al Nano
- ✅ Comunicación serial bidireccional

## Archivos Generados
```
arduino/
├── src/receiver_uno/main.cpp          ← Firmware Uno (IMPLEMENTADO)
├── lib/
│   ├── dummy_frames/dummy_frames.h    ← Frames de simulación
│   ├── protocolo/protocolo.h          ← Definiciones protocolo
│   ├── protocolo/protocolo.cpp        ← Funciones protocolo
│   ├── enlace/enlace.h                ← Codificación/decodificación
│   ├── enlace/enlace.cpp              ← Implementación enlace
│   └── crc/crc.h                      ← Cálculo CRC
```

## Cómo Usar en Tinkercad

### 1. Copiar Código
En Tinkercad:
1. Ve a tu proyecto colaborativo
2. Abre el editor de código del Arduino Uno
3. Reemplaza todo el código con el contenido de `arduino/src/receiver_uno/main.cpp`

### 2. Simular Transmisión

El firmware espera **comandos por puerto serial**:

| Comando | Efecto |
|---------|--------|
| `s` | Inicia simulación (envía dummy frames) |
| `r` | Reinicia el receiver |
| `e` | Muestra estado actual |

### 3. Probar en Tinkercad

#### Opción A: Simulación Automática
1. Carga el código
2. Abre el Serial Monitor en Tinkercad
3. Escribe `s` y presiona Enter
4. Observa cómo procesa los frames

#### Opción B: Recibir Datos Reales
1. Conecta datos desde el Nano en Serial
2. El receiver automáticamente procesará frames

### 4. Entender la Salida

```
╔══════════════════════════════════════╗
║  RECEIVER (Arduino Uno) - Redes     ║
║  Estado: ESPERANDO                  ║
╚══════════════════════════════════════╝

Comandos:
  's' - Iniciar simulación (dummy frames)
  'r' - Reiniciar receiver
  'e' - Mostrar estado actual
```

Cuando recibe datos:
```
[DUMMY] Enviado HANDSHAKE
[TRAMA] Tipo: 0x04 Seq: 0

╔════════════════════════════════════╗
║       ESTADO DEL RECEIVER          ║
╚════════════════════════════════════╝
Estado: RECIBIENDO
Frames: 1/3
Bytes: 0/30
Progreso: 0%
Tasa Error: 0%
Tiempo: 2s
```

## Máquina de Estados

```
ESPERANDO
    ↓ [HANDSHAKE recibido]
RECIBIENDO
    ├→ [DATOS válido] → incrementar frames
    ├→ [DATOS duplicado] → enviar ACK
    └→ [FIN recibido] → COMPLETO
    ↓ [Timeout]
ERROR
```

## Secuencia de Dummy Frames

Los comandos `s` generan esta secuencia:

```
1. HANDSHAKE (seq=0, sin carga)
   ├─ Receiver envía: ACK(0)
   
2. DATOS Frame 1 (seq=0, 10 bytes)
   ├─ Payload: "Hola_desde"
   ├─ Receiver envía: ACK(1)
   
3. DATOS Frame 2 (seq=1, 10 bytes)
   ├─ Payload: "_Nano_en_T"
   ├─ Receiver envía: ACK(2)
   
4. DATOS Frame 3 (seq=2, 9 bytes)
   ├─ Payload: "inkercad"
   ├─ Receiver envía: ACK(3)
   
5. FIN (seq=3)
   ├─ Receiver envía: ACK(3)
   └─ Estado → COMPLETO
```

Total: 29 bytes de payload

## Próximos Pasos

### Fase 1 (Tinkercad) - En Progreso ✅
- [x] Máquina de estados
- [x] Parseo de frames
- [x] Validación CRC
- [x] Dummy frames
- [ ] Probar en Tinkercad real

### Fase 2 (Python UI)
- [ ] Cliente Python
- [ ] Leer puerto serial
- [ ] Mostrar estado en tiempo real
- [ ] Visualizar progreso

### Fase 3 (Hardware Real)
- [ ] Cambiar dummy frames por entrada Nano real
- [ ] Probar ventanas: 3, 4, 5
- [ ] Probar velocidades: 4800, 9600, 19200 bps
- [ ] Documentar resultados

## Debugging en Tinkercad

Si algo no funciona:

1. **Serial Monitor no muestra nada**
   - Verifica que Serial.begin(9600) esté en setup()
   - Comprueba que tienes el puerto serial conectado en el circuito

2. **No procesa frames**
   - Asegúrate de escribir `s` en el Serial Monitor
   - Los datos deben venir por el puerto serial

3. **Error de compilación**
   - Verifica que el path de #include sea correcto
   - Comprueba que las librerías existan

4. **ACK no se envía**
   - Revisa `enviarAck()` - debe escribir al puerto serial
   - Valida que `Enlace::codificar()` retorne CODIFICACION_OK

## Notas Importantes

- **Baud Rate**: 9600 (fijo por PlatformIO)
- **Timeout**: 2000ms sin recibir = ERROR
- **Tamaño Ventana**: 4 tramas (configurable)
- **CRC**: 2 bytes (obligatorio)
- **Hamming**: Se agregará cuando integremos física

---

**Última actualización**: 2026-05-13
**Estado**: Listo para Tinkercad ✅
