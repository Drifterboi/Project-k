# ===== BAUDRATE DEL PUERTO USB (VARIABLE) =====
DEFAULT_BAUDRATE = 4800  # Puede ser 4800, 9600, o 19200
BAUDRATES_SOPORTADOS = (4800, 9600, 19200)

# ===== BAUDRATE DE SOFTWARESERIAL (FIJO) =====
# Este baudrate NO debe cambiar nunca
SOFTWARESERIAL_BAUDRATE = 4800  # FIJO para Arduino↔Arduino
SERIAL_TIMEOUT = 0.5  # segundos

# ===== TAMAÑOS Y TIMEOUTS =====
DEFAULT_PAYLOAD_SIZE = 100  # bytes
DEFAULT_WINDOW_SIZE = 3
TIMEOUT_CHUNK_APP = 20000  # ms (20 segundos)
TIMEOUT_ACK = 2000  # ms
TIMEOUT_HANDSHAKE = 5000  # ms

# ===== DELAYS PARA CAMBIO DE BAUDRATE =====
# Estos delays se usan cuando se cambia baudrate desde Python
DELAY_BEFORE_SETBAUD = 0.5  # segundos: esperar antes de enviar SETBAUD
DELAY_AFTER_SETBAUD = 1.5   # segundos: esperar reinicio del Arduino
DELAY_AFTER_RECONNECT = 0.5 # segundos: esperar después de reconectar


def calculate_chunk_delay(baudrate: int, chunk_size: int) -> float:
    """
    Calcula el delay dinámico para enviar chunks según el baudrate.
    
    Considera:
    - Velocidad de transmisión (baudrate)
    - Tamaño del chunk
    - Overhead de protocolo (comando DATA, espacios, hex encoding)
    
    Args:
        baudrate: Velocidad en bps (4800, 9600, 19200)
        chunk_size: Tamaño del payload en bytes
        
    Returns:
        Delay en segundos (float)
    """
    # Bits por byte: 1 start + 8 data + 1 stop = 10 bits
    bits_per_byte = 10
    
    # El payload se envía como HEX, así que 2 caracteres por byte
    # Más overhead de comando: "DATA offset length hexstring\n"
    overhead_bits = 200  # ~50 caracteres de overhead
    
    total_bits = overhead_bits + (chunk_size * 2 * bits_per_byte)
    
    # Tiempo teórico en segundos con margen de seguridad
    transmission_time = total_bits / baudrate
    safety_margin = 1.5  # 50% extra
    
    delay = transmission_time * safety_margin
    
    # Límites razonables según baudrate
    if baudrate == 4800:
        return min(delay, 1.0)  # máximo 1 segundo
    elif baudrate == 9600:
        return min(delay, 0.5)  # máximo 0.5 segundos
    elif baudrate == 19200:
        return max(0.05, min(delay, 0.2))  # mínimo 0.05, máximo 0.2
    else:
        return delay
