"""Python UI for monitoring the Arduino Uno receiver over serial.

Usage:
    1. Conecta el Arduino Uno al puerto USB.
    2. Abre este script con el entorno virtual activo.
    3. Ejecuta:
         python src/cliente/main.py
    4. Selecciona el puerto serial y presiona "Conectar".
    5. La UI mostrará el estado del receiver según las líneas que imprima el Arduino:
         - [HANDSHAKE] inicia recepción
         - [DATOS] frames recibidos
         - [TRANSMISIÓN FINALIZADA] fin de la transferencia
         - [ACK]/[NACK] respuestas enviadas

Notes:
    - Este monitor lee e interpreta mensajes de depuración Serial del firmware del receiver.
    - Si quieres, puedes usar los comandos en el Arduino:
         's' para simular transmisión
         'r' para reiniciar el receptor
         'e' para mostrar estado
"""

from __future__ import annotations

from pathlib import Path
import re
import sys
import threading
import time
import tkinter as tk
from queue import Empty, Queue
from tkinter import ttk
from tkinter import messagebox
from datetime import datetime
import csv

import serial
import serial.tools.list_ports

# Agregar ruta al path para importar config
sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
from comun.config import DEFAULT_BAUDRATE, SERIAL_TIMEOUT


class ReceiverState:
    def __init__(self) -> None:
        self.estado = 'ESPERANDO'
        self.frames_recibidos = 0
        self.frames_totales = None
        self.bytes_recibidos = 0
        self.bytes_totales = None
        self.tasa_error = 0.0
        self.mensaje_lcd = 'Inicio de transmision...'
        self.estado_enlace = 'Estado de enlace optimo...'
        self.porcentaje_transferencia = None
        self.tiempo_inicio = None
        self.tiempo_transcurrido = 0
        self.ultimo_mensaje = ''
        self.ultimo_ack = None
        self.ultimo_nack = None
        self.chunks_recibidos: dict[int, bytes] = {}
        self.archivo_guardado = None
        self.log_lines: list[str] = []

    def reset(self) -> None:
        self.__init__()


class SerialMonitorThread(threading.Thread):
    def __init__(self, port: str, baudrate: int, queue: Queue[tuple[str, str]]) -> None:
        super().__init__(daemon=True)
        self.port = port
        self.baudrate = baudrate
        self.queue = queue
        self._stop_requested = threading.Event()

    def run(self) -> None:
        try:
            with serial.Serial(self.port, self.baudrate, timeout=SERIAL_TIMEOUT) as ser:
                while not self._stop_requested.is_set():
                    try:
                        raw = ser.readline()
                    except serial.SerialException as exc:
                        self.queue.put(('error', f'Serial error: {exc}'))
                        break

                    if not raw:
                        continue

                    line = raw.decode('utf-8', errors='replace').strip()
                    if line:
                        self.queue.put(('line', line))
        except Exception as exc:
            self.queue.put(('error', f'No se pudo abrir puerto {self.port}: {exc}'))

    def stop(self) -> None:
        self._stop_requested.set()


class App:
    STATUS_PATTERNS = {
        'estado': re.compile(r'^Estado:\s*(\w+)', re.IGNORECASE),
        'frames': re.compile(r'^Frames:\s*(\d+)/(\d+|\w+)', re.IGNORECASE),
        'bytes': re.compile(r'^Bytes:\s*(\d+)/(\d+|\w+)', re.IGNORECASE),
        'tasa_error': re.compile(r'^Tasa Error:\s*(\d+(?:\.\d+)?)%', re.IGNORECASE),
        'progreso': re.compile(r'^Progreso:\s*(\d+(?:\.\d+)?)%', re.IGNORECASE),
        'tiempo': re.compile(r'^Tiempo:\s*(\d+)s', re.IGNORECASE),
        'ack': re.compile(r'^\[ACK\].*ack=(\d+)', re.IGNORECASE),
        'nack': re.compile(r'^\[NACK\].*nack=(\d+)', re.IGNORECASE),
        'datos': re.compile(r'^\[DATOS\].*Frame\s*(\d+).*bytes=(\d+)', re.IGNORECASE),
        'file': re.compile(r'^\[FILE:(\d+):(\d+)\]\s*(.*)$', re.IGNORECASE),
        'final': re.compile(r'^\[TRANSMISIÓN FINALIZADA\]', re.IGNORECASE),
        'handshake': re.compile(r'^\[HANDSHAKE\]', re.IGNORECASE),
        'timeout': re.compile(r'^\[ERROR\] Timeout', re.IGNORECASE),
        'hamming_error': re.compile(r'Hamming:.*error no corregible', re.IGNORECASE),
        'decode_error': re.compile(r'Decodificaci.*:\s*(\d+)', re.IGNORECASE),
    }

    def __init__(self, root: tk.Tk) -> None:
        self.root = root
        root.title('Monitor Receiver Arduino')
        self.state = ReceiverState()
        self.queue: Queue[tuple[str, str]] = Queue()
        self.monitor_thread: SerialMonitorThread | None = None
        self.serial_port_var = tk.StringVar(value='')
        self.baudrate_var = tk.StringVar(value=str(DEFAULT_BAUDRATE))

        self.build_ui()
        self.refresh_ports()
        self.root.after(100, self.process_queue)
        self.root.after(1000, self.update_elapsed_time)

    def build_ui(self) -> None:
        top = tk.Frame(self.root, padx=10, pady=10)
        top.pack(fill='x')

        tk.Label(top, text='Puerto serial:').grid(row=0, column=0, sticky='w')
        self.port_menu = tk.OptionMenu(top, self.serial_port_var, '')
        self.port_menu.grid(row=0, column=1, sticky='we', padx=(5, 10))
        self.port_menu.config(width=25)

        tk.Button(top, text='Actualizar puertos', command=self.refresh_ports).grid(row=0, column=2)

        tk.Label(top, text='Baudrate:').grid(row=1, column=0, sticky='w', pady=(10, 0))
        baudrate_selector = ttk.Combobox(
            top,
            textvariable=self.baudrate_var,
            values=('4800', '9600', '19200'),
            width=10,
            state='readonly',
        )
        baudrate_selector.grid(row=1, column=1, sticky='w', pady=(10, 0))
        tk.Button(top, text='Conectar', command=self.toggle_connection).grid(row=1, column=2, pady=(10, 0))
        tk.Button(top, text='Reset', command=self.reset_receiver_state).grid(row=1, column=3, sticky='w', padx=(5, 0), pady=(10, 0))

        status_frame = tk.LabelFrame(self.root, text='Estado del Receiver', padx=10, pady=10)
        status_frame.pack(fill='x', padx=10, pady=(10, 0))

        lcd_frame = tk.LabelFrame(self.root, text='Display requerido', padx=10, pady=10)
        lcd_frame.pack(fill='x', padx=10, pady=(10, 0))

        self.lcd_message_label = tk.Label(lcd_frame, text='Inicio de transmision...', font=('Arial', 12, 'bold'))
        self.lcd_message_label.pack(anchor='w')
        self.lcd_link_label = tk.Label(lcd_frame, text='Estado de enlace optimo...')
        self.lcd_link_label.pack(anchor='w', pady=(4, 0))
        self.lcd_progress_label = tk.Label(lcd_frame, text='Progreso: -')
        self.lcd_progress_label.pack(anchor='w', pady=(4, 0))
        self.lcd_error_label = tk.Label(lcd_frame, text='Tasa de error estimada: 0.00 %')
        self.lcd_error_label.pack(anchor='w', pady=(4, 0))

        self.labels = {}
        fields = [
            ('Estado', 'estado'),
            ('Frames recibidos', 'frames_recibidos'),
            ('Frames totales', 'frames_totales'),
            ('Bytes recibidos', 'bytes_recibidos'),
            ('Bytes totales', 'bytes_totales'),
            ('Tasa error', 'tasa_error'),
            ('Tiempo', 'tiempo_transcurrido'),
            ('Último mensaje', 'ultimo_mensaje'),
            ('Último ACK', 'ultimo_ack'),
            ('Último NACK', 'ultimo_nack'),
        ]

        for index, (label_text, attr) in enumerate(fields):
            row = index // 2
            column = (index % 2) * 2
            tk.Label(status_frame, text=label_text + ':').grid(row=row, column=column, sticky='w', pady=2)
            self.labels[attr] = tk.Label(status_frame, text='-')
            self.labels[attr].grid(row=row, column=column + 1, sticky='w', pady=2)

        log_frame = tk.LabelFrame(self.root, text='Registro de eventos', padx=10, pady=10)
        log_frame.pack(fill='both', expand=True, padx=10, pady=(10, 10))

        self.log_text = tk.Text(log_frame, height=14, wrap='word', state='disabled')
        self.log_text.pack(fill='both', expand=True)

        self.update_status_widgets()

    def refresh_ports(self) -> None:
        ports = [port.device for port in serial.tools.list_ports.comports()]
        menu = self.port_menu['menu']
        menu.delete(0, 'end')
        if not ports:
            ports = ['<ninguno>']
        for port in ports:
            menu.add_command(label=port, command=lambda value=port: self.serial_port_var.set(value))
        self.serial_port_var.set(ports[0])

    def toggle_connection(self) -> None:
        if self.monitor_thread and self.monitor_thread.is_alive():
            self.disconnect()
        else:
            self.connect()

    def connect(self) -> None:
        port = self.serial_port_var.get()
        if not port or port == '<ninguno>':
            messagebox.showwarning('Puerto serial', 'Seleccione un puerto serial válido antes de conectar.')
            return
        try:
            baudrate = int(self.baudrate_var.get())
        except ValueError:
            messagebox.showwarning('Baudrate', 'Ingrese un valor numérico para baudrate.')
            return

        self.monitor_thread = SerialMonitorThread(port, baudrate, self.queue)
        self.monitor_thread.start()
        self.append_log(f'Conectado a {port} a {baudrate} baudios.')
        self.update_connect_button()

    def disconnect(self) -> None:
        if self.monitor_thread:
            self.monitor_thread.stop()
            self.monitor_thread.join(timeout=2)
            self.monitor_thread = None
            self.append_log('Desconectado.')
        self.update_connect_button()

    def reset_receiver_state(self) -> None:
        """Reset todos los valores del receptor a iniciales"""
        self.state.reset()
        self.update_status_widgets()
        self.log_text.config(state='normal')
        self.log_text.delete(1.0, tk.END)
        self.log_text.config(state='disabled')
        self.append_log('Estado reseteado a valores iniciales.')

    def update_connect_button(self) -> None:
        label = 'Desconectar' if self.monitor_thread and self.monitor_thread.is_alive() else 'Conectar'
        for widget in self.root.winfo_children():
            if isinstance(widget, tk.Frame):
                for child in widget.winfo_children():
                    if isinstance(child, tk.Button) and child['text'] in ('Conectar', 'Desconectar'):
                        child.config(text=label)
                        return

    def process_queue(self) -> None:
        while True:
            try:
                kind, data = self.queue.get_nowait()
            except Empty:
                break

            if kind == 'line':
                self.handle_line(data)
            elif kind == 'error':
                self.append_log(data)
                messagebox.showerror('Error serial', data)
                self.disconnect()

        self.update_status_widgets()
        self.root.after(100, self.process_queue)

    def update_elapsed_time(self) -> None:
        if self.state.tiempo_inicio is not None:
            self.state.tiempo_transcurrido = int(time.time() - self.state.tiempo_inicio)
        self.update_status_widgets()
        self.root.after(1000, self.update_elapsed_time)

    def handle_line(self, line: str) -> None:
        self.append_log(line)
        if self.STATUS_PATTERNS['handshake'].search(line):
            self.state.estado = 'RECIBIENDO'
            self.state.mensaje_lcd = 'Inicio de transmision...'
            self.state.estado_enlace = 'Estado de enlace optimo...'
            self.state.tiempo_inicio = time.time()
            self.state.chunks_recibidos = {}
            self.state.archivo_guardado = None
            return

        if self.STATUS_PATTERNS['final'].search(line):
            self.state.estado = 'COMPLETO'
            self.state.mensaje_lcd = 'Transmision Finalizada!'
            self.state.porcentaje_transferencia = 100.0
            self.guardar_archivo_reconstruido()
            self.state.reset()
            return

        for key, pattern in self.STATUS_PATTERNS.items():
            match = pattern.search(line)
            if not match:
                continue

            if key == 'estado':
                self.state.estado = match.group(1).upper()
            elif key == 'frames':
                self.state.frames_recibidos = int(match.group(1))
                if match.group(2).isdigit():
                    self.state.frames_totales = int(match.group(2))
            elif key == 'bytes':
                self.state.bytes_recibidos = int(match.group(1))
                if match.group(2).isdigit():
                    self.state.bytes_totales = int(match.group(2))
            elif key == 'tasa_error':
                self.state.tasa_error = float(match.group(1))
            elif key == 'progreso':
                self.state.porcentaje_transferencia = float(match.group(1))
            elif key == 'tiempo':
                self.state.tiempo_transcurrido = int(match.group(1))
            elif key == 'ack':
                self.state.ultimo_ack = int(match.group(1))
            elif key == 'nack':
                self.state.ultimo_nack = int(match.group(1))
                self.state.estado_enlace = 'Estado de enlace ruidoso...'
            elif key == 'datos':
                frame_index = int(match.group(1))
                self.state.frames_recibidos = max(self.state.frames_recibidos, frame_index + 1)
                self.state.bytes_recibidos += int(match.group(2))
                self.state.ultimo_mensaje = f'Frame {frame_index} recibido'
                self.state.mensaje_lcd = 'Transmision en curso...'
                if self.state.bytes_totales:
                    self.state.porcentaje_transferencia = min(
                        100.0,
                        (self.state.bytes_recibidos * 100.0) / self.state.bytes_totales,
                    )
            elif key == 'file':
                self.registrar_chunk_archivo(match)
            elif key == 'timeout':
                self.state.estado = 'ERROR'
                self.state.mensaje_lcd = 'Estado de enlace ruidoso...'
                self.state.estado_enlace = 'Estado de enlace ruidoso...'
            elif key in ('hamming_error', 'decode_error'):
                self.state.estado_enlace = 'Estado de enlace ruidoso...'
            break

    def registrar_chunk_archivo(self, match: re.Match[str]) -> None:
        seq = int(match.group(1))
        longitud = int(match.group(2))
        hex_payload = match.group(3).strip()

        if longitud == 0:
            self.state.chunks_recibidos[seq] = b''
            return

        try:
            datos = bytes(int(byte_hex, 16) for byte_hex in hex_payload.split())
        except ValueError:
            self.state.ultimo_mensaje = f'Payload invalido en frame {seq}'
            return

        if len(datos) != longitud:
            self.state.ultimo_mensaje = f'Frame {seq}: {len(datos)}/{longitud} bytes reconstruidos'
            return

        self.state.chunks_recibidos[seq] = datos

    def guardar_archivo_reconstruido(self) -> None:
        if not self.state.chunks_recibidos:
            self.state.ultimo_mensaje = 'No hay datos de archivo para guardar'
            return

        contenido = b''.join(
            self.state.chunks_recibidos[seq]
            for seq in sorted(self.state.chunks_recibidos)
        )
        if self.state.bytes_totales:
            contenido = contenido[:self.state.bytes_totales]

        salida_dir = Path(__file__).resolve().parents[2] / 'recibidos'
        salida_dir.mkdir(parents=True, exist_ok=True)
        
        timestamp = datetime.now().strftime('%Y%m%d_%H%M%S')
        nombre_archivo = f'archivo_recibido_{timestamp}.txt'
        salida = salida_dir / nombre_archivo
        salida.write_bytes(contenido)

        self.state.archivo_guardado = str(salida)
        self.state.ultimo_mensaje = f'Archivo guardado: {salida.name}'
        self.append_log(f'[ARCHIVO] Guardado en {salida}')
        
        # Guardar en CSV
        self.guardar_resultado_csv(nombre_archivo, len(contenido))

    def guardar_resultado_csv(self, nombre_archivo: str, bytes_recibidos: int) -> None:
        resultados_dir = Path(__file__).resolve().parents[2] / 'resultados'
        resultados_dir.mkdir(parents=True, exist_ok=True)
        csv_file = resultados_dir / 'pruebas_transferencia.csv'
        
        timestamp = datetime.now().strftime('%Y-%m-%d %H:%M:%S')
        duracion = (self.state.tiempo_transcurrido) if self.state.tiempo_transcurrido else 0
        
        headers = ['Timestamp', 'Baudrate', 'Payload', 'Ventana', 'Archivo', 'Bytes_Total', 'Bytes_Recibidos', 'Frames_Total', 'Frames_Recibidos', 'Duracion_seg', 'Tasa_Error_%', 'Estado']
        
        row = [
            timestamp,
            '4800',
            '-',
            '-',
            nombre_archivo,
            self.state.bytes_totales if self.state.bytes_totales else 0,
            bytes_recibidos,
            self.state.frames_totales if self.state.frames_totales else 0,
            self.state.frames_recibidos,
            f'{duracion:.2f}',
            f'{self.state.tasa_error:.2f}',
            'COMPLETO' if self.state.estado == 'COMPLETO' else self.state.estado
        ]
        
        file_exists = csv_file.exists()
        try:
            with open(csv_file, 'a', newline='', encoding='utf-8') as f:
                writer = csv.writer(f)
                if not file_exists:
                    writer.writerow(headers)
                writer.writerow(row)
        except Exception as e:
            self.append_log(f'[ERROR] No se pudo guardar CSV: {e}')

    def append_log(self, text: str) -> None:
        self.state.log_lines.append(text)
        if len(self.state.log_lines) > 200:
            self.state.log_lines.pop(0)
        self.log_text.config(state='normal')
        self.log_text.insert('end', text + '\n')
        self.log_text.see('end')
        self.log_text.config(state='disabled')

    def update_status_widgets(self) -> None:
        self.labels['estado'].config(text=self.state.estado)
        self.labels['frames_recibidos'].config(text=str(self.state.frames_recibidos))
        self.labels['frames_totales'].config(text=str(self.state.frames_totales or '-'))
        self.labels['bytes_recibidos'].config(text=str(self.state.bytes_recibidos))
        self.labels['bytes_totales'].config(text=str(self.state.bytes_totales or '-'))
        self.labels['tasa_error'].config(text=f'{float(self.state.tasa_error):.2f}%')
        self.labels['tiempo_transcurrido'].config(text=f'{self.state.tiempo_transcurrido}s')
        self.labels['ultimo_mensaje'].config(text=self.state.ultimo_mensaje or '-')
        self.labels['ultimo_ack'].config(text=str(self.state.ultimo_ack) if self.state.ultimo_ack is not None else '-')
        self.labels['ultimo_nack'].config(text=str(self.state.ultimo_nack) if self.state.ultimo_nack is not None else '-')
        self.lcd_message_label.config(text=self.state.mensaje_lcd)
        self.lcd_link_label.config(text=self.state.estado_enlace)
        if self.state.porcentaje_transferencia is None:
            self.lcd_progress_label.config(text='Progreso: -')
        else:
            self.lcd_progress_label.config(text=f'Progreso: {self.state.porcentaje_transferencia:.2f} %')
        self.lcd_error_label.config(text=f'Tasa de error estimada: {float(self.state.tasa_error):.2f} %')


def main() -> None:
    root = tk.Tk()
    App(root)
    root.mainloop()


if __name__ == '__main__':
    main()
