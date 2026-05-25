from __future__ import annotations

import csv
import os
import threading
import time
import tkinter as tk
from datetime import datetime
from pathlib import Path
from tkinter import filedialog, messagebox, ttk

import serial
import serial.tools.list_ports


class App:
    def __init__(self, root: tk.Tk) -> None:
        self.root = root
        root.title('Sender Arduino Nano')

        self.ser: serial.Serial | None = None
        self.serial_lock = threading.Lock()
        self.reader_thread: threading.Thread | None = None
        self.stop_reader = threading.Event()
        self.tiempo_inicio: float | None = None
        self.pending_file_data: bytes = b''
        self.pending_payload_size = 0
        
        # Variables para tracking de transferencia
        self.current_filename = ''
        self.current_file_bytes = 0
        self.current_frames_total = 0
        self.transfer_start_time = None
        self.transfer_end_time = None
        
        self.serial_port_var = tk.StringVar(value='')
        self.baudrate_var = tk.StringVar(value='4800')
        self.send_file_path = tk.StringVar(value='')
        self.sliding_window_size = tk.StringVar(value='3')
        self.payload_size_var = tk.StringVar(value='100')
        self.status_var = tk.StringVar(value='Desconectado.')
        self.sending = False
        self.sender_status_vars = {
            'estado': tk.StringVar(value='PREPARANDO'),
            'frames_enviados': tk.StringVar(value='0'),
            'frames_totales': tk.StringVar(value='-'),
            'bytes_enviados': tk.StringVar(value='0'),
            'bytes_totales': tk.StringVar(value='-'),
            'tasa_error': tk.StringVar(value='0.00%'),
            'tiempo': tk.StringVar(value='0s'),
            'ultimo_mensaje': tk.StringVar(value='-'),
            'ultimo_ack': tk.StringVar(value='-'),
            'ultimo_nack': tk.StringVar(value='-'),
        }

        self.build_ui()
        self.refresh_ports()
        self.root.after(1000, self.update_elapsed_time)

    def build_ui(self) -> None:
        frame = tk.Frame(self.root, padx=10, pady=10)
        frame.pack(fill='x')

        tk.Label(frame, text='Puerto serial:').grid(row=0, column=0, sticky='w')
        self.port_menu = tk.OptionMenu(frame, self.serial_port_var, '')
        self.port_menu.grid(row=0, column=1, sticky='we', padx=(5, 10))
        self.port_menu.config(width=25)
        tk.Button(frame, text='Actualizar puertos', command=self.refresh_ports).grid(row=0, column=2)

        tk.Label(frame, text='Baudrate:').grid(row=1, column=0, sticky='w', pady=(10, 0))
        ttk.Combobox(
            frame,
            textvariable=self.baudrate_var,
            values=('4800', '9600', '19200', '38400'),
            width=10,
            state='readonly',
        ).grid(row=1, column=1, sticky='w', pady=(10, 0))
        self.connect_button = tk.Button(frame, text='Conectar', command=self.toggle_connection)
        self.connect_button.grid(row=1, column=2, pady=(10, 0))

        tk.Label(frame, text='Archivo:').grid(row=2, column=0, sticky='w', pady=(10, 0))
        self.file_label = tk.Label(frame, text='(Seleccione un archivo)', anchor='w')
        self.file_label.grid(row=2, column=1, sticky='we', pady=(10, 0))
        tk.Button(frame, text='Seleccionar...', command=self.select_file).grid(row=2, column=2, pady=(10, 0))

        tk.Label(frame, text='Ventana deslizante:').grid(row=3, column=0, sticky='w', pady=(10, 0))
        ttk.Combobox(
            frame,
            textvariable=self.sliding_window_size,
            values=('3', '4', '5'),
            width=10,
            state='readonly',
        ).grid(row=3, column=1, sticky='w', pady=(10, 0))

        tk.Label(frame, text='Carga util por trama (bytes):').grid(row=4, column=0, sticky='w', pady=(10, 0))
        ttk.Combobox(
            frame,
            textvariable=self.payload_size_var,
            values=('100', '400', '1000'),
            width=10,
            state='readonly',
        ).grid(row=4, column=1, sticky='w', pady=(10, 0))
        tk.Button(frame, text='Enviar', command=self.send_file).grid(row=4, column=2, sticky='w', pady=(10, 0))
        self.cancel_button = tk.Button(frame, text='Cancelar', command=self.cancel_send, state='disabled')
        self.cancel_button.grid(row=4, column=3, sticky='w', padx=(5, 0), pady=(10, 0))

        tk.Label(frame, textvariable=self.status_var, anchor='w').grid(
            row=5,
            column=0,
            columnspan=3,
            sticky='we',
            pady=(12, 0),
        )

        frame.columnconfigure(1, weight=1)

        status_frame = tk.LabelFrame(self.root, text='Estado del Sender', padx=10, pady=10)
        status_frame.pack(fill='x', padx=10, pady=(0, 10))

        fields = [
            ('Estado', 'estado'),
            ('Frames enviados', 'frames_enviados'),
            ('Frames totales', 'frames_totales'),
            ('Bytes enviados', 'bytes_enviados'),
            ('Bytes totales', 'bytes_totales'),
            ('Tasa error', 'tasa_error'),
            ('Tiempo', 'tiempo'),
            ('Ultimo mensaje', 'ultimo_mensaje'),
            ('Ultimo ACK', 'ultimo_ack'),
            ('Ultimo NACK', 'ultimo_nack'),
        ]

        for index, (label_text, key) in enumerate(fields):
            row = index // 2
            column = (index % 2) * 2
            tk.Label(status_frame, text=label_text + ':').grid(row=row, column=column, sticky='w', pady=2)
            tk.Label(status_frame, textvariable=self.sender_status_vars[key]).grid(
                row=row,
                column=column + 1,
                sticky='w',
                pady=2,
                padx=(4, 20),
            )

    def select_file(self) -> None:
        file_path = filedialog.askopenfilename(
            title='Seleccione archivo para enviar',
            filetypes=(('Text Files', '*.txt'), ('All Files', '*.*')),
        )
        if file_path:
            self.send_file_path.set(file_path)
            self.file_label.config(text=file_path)

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
        if self.ser and self.ser.is_open:
            self.disconnect()
        else:
            self.connect()

    def connect(self) -> None:
        port = self.serial_port_var.get()
        if not port or port == '<ninguno>':
            messagebox.showwarning('Puerto serial', 'Seleccione un puerto serial valido antes de conectar.')
            return

        try:
            baudrate_deseado = int(self.baudrate_var.get())
        except ValueError:
            messagebox.showwarning('Baudrate', 'Seleccione un baudrate valido.')
            return

        # Conectar siempre a 4800 primero (baudrate inicial del Arduino)
        try:
            self.ser = serial.Serial(port, 4800, timeout=1.0)  # 1 segundo para mejor estabilidad
            time.sleep(1)  # Esperar a que Arduino esté listo
        except Exception as exc:
            messagebox.showerror('Error de conexion', str(exc))
            return

        # Si el baudrate deseado es diferente a 4800, cambiar
        if baudrate_deseado != 4800:
            self.status_var.set(f'Cambiando baudrate a {baudrate_deseado}...')
            self.root.update()

            try:
                # Enviar comando SETBAUD al sender
                comando = f'SETBAUD:{baudrate_deseado}\n'
                self.ser.write(comando.encode())
                self.ser.flush()
                time.sleep(1.5)  # Esperar reinicio del Arduino
                
                # Desconectar y reconectar con nuevo baudrate
                self.ser.close()
                time.sleep(0.8)
                self.ser = serial.Serial(port, baudrate_deseado, timeout=1.0)  # 1 segundo para mejor estabilidad
                time.sleep(0.5)
            except Exception as exc:
                messagebox.showerror('Error al cambiar baudrate', str(exc))
                if self.ser and self.ser.is_open:
                    self.ser.close()
                return

        self.connect_button.config(text='Desconectar')
        self.status_var.set(f'Conectado a {port} @ {baudrate_deseado} bps.')
        self.sender_status_vars['estado'].set('CONECTADO')
        self.sender_status_vars['ultimo_mensaje'].set(f'Puerto {port} listo')
        self.start_serial_reader()

    def disconnect(self) -> None:
        self.stop_serial_reader()
        if self.ser and self.ser.is_open:
            self.ser.close()
        self.ser = None
        self.sending = False
        self.cancel_button.config(state='disabled')
        self.connect_button.config(text='Conectar')
        self.status_var.set('Desconectado.')
        self.sender_status_vars['estado'].set('PREPARANDO')
        self.sender_status_vars['ultimo_mensaje'].set('Desconectado')
        self.sender_status_vars['tiempo'].set('0s')
        self.tiempo_inicio = None

    def start_serial_reader(self) -> None:
        self.stop_reader.clear()
        self.reader_thread = threading.Thread(target=self.read_serial_lines, daemon=True)
        self.reader_thread.start()

    def stop_serial_reader(self) -> None:
        self.stop_reader.set()
        if self.reader_thread and self.reader_thread.is_alive():
            self.reader_thread.join(timeout=1)
        self.reader_thread = None

    def read_serial_lines(self) -> None:
        while not self.stop_reader.is_set() and self.ser and self.ser.is_open:
            try:
                raw = self.ser.readline()
            except serial.SerialException:
                self.root.after(0, self.on_serial_error)
                return

            if not raw:
                continue

            line = raw.decode('utf-8', errors='replace').strip()
            if line:
                if line.startswith('[REQ]'):
                    self.handle_chunk_request_from_reader(line)
                else:
                    self.root.after(0, self.handle_sender_line, line)

    def on_serial_error(self) -> None:
        self.sender_status_vars['estado'].set('ERROR')
        self.sender_status_vars['ultimo_mensaje'].set('Error leyendo serial')

    def handle_sender_line(self, line: str) -> None:
        if line.startswith('[REQ]'):
            self.handle_chunk_request(line)
        elif line.startswith('[ACK]'):
            ack = line.rsplit('=', 1)[-1] if '=' in line else line
            self.sender_status_vars['ultimo_ack'].set(ack)
        elif line.startswith('[NACK]'):
            nack = line.rsplit('=', 1)[-1] if '=' in line else line
            self.sender_status_vars['ultimo_nack'].set(nack)
            self.sender_status_vars['tasa_error'].set('> 0.00%')
        elif '[TIMEOUT]' in line:
            # TIMEOUT de reintento no implica fallo final; el firmware puede recuperarse.
            self.sender_status_vars['ultimo_mensaje'].set(line[:60])
        elif '[ERROR]' in line:
            self.sender_status_vars['estado'].set('ERROR')
            self.sender_status_vars['tasa_error'].set('> 0.00%')
            self.sender_status_vars['ultimo_mensaje'].set(line[:60])
            self.sending = False
            self.cancel_button.config(state='disabled')
            self.status_var.set('Transferencia con error. Revisa el monitor serial.')
            # Guardar resultado de error
            self.transfer_end_time = time.time()
            self.root.after(100, self.save_test_results, 'ERROR')
        elif 'ACK valido' in line or 'Ack válido' in line:
            self.sender_status_vars['ultimo_ack'].set(line)
        elif 'Handshake completado' in line:
            self.sender_status_vars['ultimo_mensaje'].set('Handshake completado')
        elif 'Trama FIN enviada' in line:
            self.sender_status_vars['estado'].set('COMPLETO')
            self.sender_status_vars['ultimo_mensaje'].set('FIN enviado')
            self.status_var.set('Transferencia completada.')
            self.sending = False
            self.cancel_button.config(state='disabled')
        elif '[INFO] Transferencia finalizada' in line:
            self.sender_status_vars['estado'].set('LISTO')
            self.sender_status_vars['ultimo_mensaje'].set('Listo para nuevo archivo')
            # Guardar resultado exitoso
            self.transfer_end_time = time.time()
            self.root.after(100, self.save_test_results, 'COMPLETO')
            self.root.after(500, self.reset_sender_state)  # Resetear después de pequeño delay

    def handle_chunk_request(self, line: str) -> None:
        self.handle_chunk_request_from_reader(line)

    def handle_chunk_request_from_reader(self, line: str) -> None:
        self.root.after(0, self.sender_status_vars['ultimo_mensaje'].set, f'Solicitud recibida: {line}')
        if not self.ser or not self.ser.is_open:
            return

        parts = line.split()
        if len(parts) != 3:
            self.root.after(0, self.sender_status_vars['ultimo_mensaje'].set, 'Solicitud de chunk invalida')
            return

        try:
            offset = int(parts[1])
            length = int(parts[2])
        except ValueError:
            self.root.after(0, self.sender_status_vars['ultimo_mensaje'].set, 'Solicitud de chunk invalida')
            return

        if offset < 0 or length < 0 or offset + length > len(self.pending_file_data):
            self.root.after(0, self.sender_status_vars['ultimo_mensaje'].set, 'Chunk fuera de rango')
            return

        chunk = self.pending_file_data[offset:offset + length]
        chunk_line = f'DATA {offset} {length} {chunk.hex().upper()}\n'
        try:
            try:
                baudrate = int(self.baudrate_var.get())
            except (ValueError, AttributeError):
                baudrate = 4800
            
            # Calculate dynamic delay based on baudrate and chunk size
            # CRITICAL: Wait long enough for Arduino to process and send response
            bytes_to_send = len(chunk_line)
            bits_per_byte = 10
            bits_to_send = bytes_to_send * bits_per_byte
            ms_per_bit = 1000.0 / baudrate
            transmission_time = (bits_to_send * ms_per_bit) / 1000.0
            # Agregar tiempo de procesamiento del Arduino (mínimo 1 segundo)
            dynamic_delay = max(1.0, transmission_time + 1.0)
            
            with self.serial_lock:
                self.ser.write(chunk_line.encode('ascii'))
                self.ser.flush()
            time.sleep(dynamic_delay)
        except Exception as exc:
            self.root.after(0, self.sender_status_vars['estado'].set, 'ERROR')
            self.root.after(0, self.sender_status_vars['ultimo_mensaje'].set, f'Error enviando chunk: {exc}')
            return

        frames_preparados = (offset + length + self.pending_payload_size - 1) // self.pending_payload_size
        self.root.after(0, self.sender_status_vars['frames_enviados'].set, str(frames_preparados))
        self.root.after(0, self.sender_status_vars['bytes_enviados'].set, str(offset + length))
        self.root.after(0, self.sender_status_vars['ultimo_mensaje'].set, f'Chunk {offset}:{offset + length} entregado ({len(chunk)} bytes)')

    def update_elapsed_time(self) -> None:
        if self.tiempo_inicio is not None and self.sending:
            elapsed = int(time.time() - self.tiempo_inicio)
            self.sender_status_vars['tiempo'].set(f'{elapsed}s')
        self.root.after(1000, self.update_elapsed_time)

    def cancel_send(self) -> None:
        """Cancelar envío en progreso"""
        self.sending = False
        self.cancel_button.config(state='disabled')
        self.sender_status_vars['estado'].set('CANCELADO')
        self.sender_status_vars['ultimo_mensaje'].set('Envío cancelado por usuario')
        self.status_var.set('Envío cancelado.')

    def send_file(self) -> None:
        if not self.ser or not self.ser.is_open:
            messagebox.showwarning('Error', 'No hay conexion serial activa.')
            return

        file_path = self.send_file_path.get()
        if not file_path:
            messagebox.showwarning('Error', 'No se ha seleccionado ningun archivo.')
            return

        try:
            with open(file_path, 'rb') as file:
                file_data = file.read()
        except Exception as exc:
            messagebox.showerror('Error', f'No se pudo leer el archivo:\n{exc}')
            return

        try:
            payload_size = int(self.payload_size_var.get())
            window_size = int(self.sliding_window_size.get())
        except ValueError:
            messagebox.showwarning('Parametros', 'Seleccione valores validos para carga util y ventana.')
            return

        if payload_size <= 0:
            messagebox.showwarning('Parametros', 'La carga util por trama debe ser mayor que cero.')
            return

        if not file_data:
            messagebox.showwarning('Archivo', 'El archivo seleccionado esta vacio.')
            return

        filename = os.path.basename(file_path)
        total_frames = (len(file_data) + payload_size - 1) // payload_size
        self.pending_file_data = file_data
        self.pending_payload_size = payload_size
        
        # Registrar información de la transferencia
        self.current_filename = filename
        self.current_file_bytes = len(file_data)
        self.current_frames_total = total_frames
        self.transfer_start_time = time.time()
        self.transfer_end_time = None

        self.sender_status_vars['estado'].set('ENVIANDO')
        self.sender_status_vars['frames_enviados'].set('0')
        self.sender_status_vars['frames_totales'].set(str(total_frames))
        self.sender_status_vars['bytes_enviados'].set('0')
        self.sender_status_vars['bytes_totales'].set(str(len(file_data)))
        self.sender_status_vars['tasa_error'].set('0.00%')
        self.sender_status_vars['tiempo'].set('0s')
        self.sender_status_vars['ultimo_ack'].set('-')
        self.sender_status_vars['ultimo_nack'].set('-')
        self.sender_status_vars['ultimo_mensaje'].set('Iniciando envio')
        self.sending = True
        self.cancel_button.config(state='normal')
        self.tiempo_inicio = time.time()
        self.root.update_idletasks()

        try:
            # Clear any pending data from previous transfer
            self.ser.reset_input_buffer()
            self.ser.reset_output_buffer()
            time.sleep(0.5)
            
            start_cmd = f'START {filename} {len(file_data)} {payload_size} {window_size}\n'
            with self.serial_lock:
                self.ser.write(start_cmd.encode('utf-8'))
                self.ser.flush()
            # CRITICAL: Wait for Arduino to process START and begin handshake
            # At 4800 bps this takes time - minimum 2 seconds
            time.sleep(2.0)
        except Exception as exc:
            messagebox.showerror('Error de envio', str(exc))
            self.status_var.set(f'Error enviando archivo: {exc}')
            self.sender_status_vars['estado'].set('ERROR')
            self.sender_status_vars['ultimo_mensaje'].set('Error de envio')
            return

        self.status_var.set(
            f'Listo: {len(file_data)} bytes en Python, ventana {window_size}, baudrate {self.baudrate_var.get()} bps.'
        )
        self.sender_status_vars['ultimo_mensaje'].set('Esperando solicitudes del Nano')

    def save_test_results(self, estado_final: str) -> None:
        """Guardar resultados de transferencia en CSV sin sobrescribir."""
        try:
            # Crear carpeta resultados si no existe
            resultados_dir = Path('resultados')
            resultados_dir.mkdir(exist_ok=True)
            
            # Archivo CSV
            csv_file = resultados_dir / 'pruebas_transferencia.csv'
            
            # Calcular duración
            duracion_segundos = 0
            if self.transfer_start_time and self.transfer_end_time:
                duracion_segundos = self.transfer_end_time - self.transfer_start_time
            
            # Datos a guardar
            timestamp = datetime.now().strftime('%Y-%m-%d %H:%M:%S')
            baudrate = self.baudrate_var.get()
            payload_size = self.payload_size_var.get()
            window_size = self.sliding_window_size.get()
            frames_enviados = self.sender_status_vars['frames_enviados'].get()
            frames_totales = self.sender_status_vars['frames_totales'].get()
            bytes_enviados = self.sender_status_vars['bytes_enviados'].get()
            bytes_totales = self.sender_status_vars['bytes_totales'].get()
            tasa_error = self.sender_status_vars['tasa_error'].get()
            
            # Headers del CSV
            headers = [
                'Fecha/Hora',
                'Baudrate (bps)',
                'Payload (bytes)',
                'Ventana',
                'Archivo',
                'Bytes Totales',
                'Bytes Enviados',
                'Frames Totales',
                'Frames Enviados',
                'Duración (s)',
                'Tasa Error',
                'Estado'
            ]
            
            # Crear o abrir CSV en modo append
            file_exists = csv_file.exists()
            with open(csv_file, 'a', newline='', encoding='utf-8') as f:
                writer = csv.writer(f, delimiter=',')
                
                # Escribir headers si es la primera vez
                if not file_exists:
                    writer.writerow(headers)
                
                # Escribir datos de la prueba
                writer.writerow([
                    timestamp,
                    baudrate,
                    payload_size,
                    window_size,
                    self.current_filename,
                    bytes_totales,
                    bytes_enviados,
                    frames_totales,
                    frames_enviados,
                    f'{duracion_segundos:.2f}',
                    tasa_error,
                    estado_final
                ])
            
            # Mostrar confirmación
            messagebox.showinfo(
                'Resultados Guardados',
                f'Prueba registrada en:\n{csv_file.absolute()}\n\n'
                f'Baudrate: {baudrate} bps\n'
                f'Payload: {payload_size} bytes\n'
                f'Ventana: {window_size}\n'
                f'Duración: {duracion_segundos:.2f}s\n'
                f'Tasa error: {tasa_error}\n'
                f'Estado: {estado_final}'
            )
        except Exception as exc:
            messagebox.showerror('Error al guardar resultados', str(exc))

    def reset_sender_state(self) -> None:
        """Resetear estado del sender después de completar transferencia."""
        self.sending = False
        self.cancel_button.config(state='disabled')
        self.pending_file_data = b''
        self.pending_payload_size = 0
        self.tiempo_inicio = None
        self.sender_status_vars['estado'].set('PREPARANDO')
        self.sender_status_vars['frames_enviados'].set('0')
        self.sender_status_vars['frames_totales'].set('-')
        self.sender_status_vars['bytes_enviados'].set('0')
        self.sender_status_vars['bytes_totales'].set('-')
        self.sender_status_vars['tasa_error'].set('0.00%')
        self.sender_status_vars['tiempo'].set('0s')
        self.sender_status_vars['ultimo_ack'].set('-')
        self.sender_status_vars['ultimo_nack'].set('-')
        self.sender_status_vars['ultimo_mensaje'].set('Listo para nuevo archivo')
        self.status_var.set('Listo para enviar nuevo archivo.')


def main() -> None:
    root = tk.Tk()
    App(root)
    root.mainloop()


if __name__ == '__main__':
    main()
