from __future__ import annotations

import os
import sys
import threading
import time
import tkinter as tk
from tkinter import filedialog, messagebox, ttk
from datetime import datetime
import csv
from pathlib import Path

import serial
import serial.tools.list_ports

# Agregar ruta al path para importar config
sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
from comun.config import DEFAULT_BAUDRATE, SERIAL_TIMEOUT, calculate_chunk_delay


class App:
    def __init__(self, root: tk.Tk) -> None:
        self.root = root
        root.title('Sender Arduino Nano')
        root.geometry('1000x900')  # Aumentar altura para el monitor serial

        self.ser: serial.Serial | None = None
        self.serial_lock = threading.Lock()
        self.reader_thread: threading.Thread | None = None
        self.stop_reader = threading.Event()
        self.tiempo_inicio: float | None = None
        self.pending_file_data: bytes = b''
        self.pending_payload_size = 0
        self.serial_port_var = tk.StringVar(value='')
        self.baudrate_var = tk.StringVar(value=str(DEFAULT_BAUDRATE))
        self.send_file_path = tk.StringVar(value='')
        self.sliding_window_size = tk.StringVar(value='3')
        self.payload_size_var = tk.StringVar(value='100')
        self.status_var = tk.StringVar(value='Desconectado.')
        self.sending = False
        self.nombre_archivo_actual = ''
        self.frames_totales_actual = 0
        self.bytes_totales_actual = 0
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
        self.serial_monitor_text: tk.Text | None = None

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
            values=('4800', '9600', '19200'),
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
        tk.Button(frame, text='Reset', command=self.reset_all_values).grid(row=4, column=4, sticky='w', padx=(5, 0), pady=(10, 0))

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

        # ========== SERIAL MONITOR PANEL ==========
        monitor_frame = tk.LabelFrame(self.root, text='Serial Monitor del Nano', padx=10, pady=10)
        monitor_frame.pack(fill='both', expand=True, padx=10, pady=(0, 10))

        # Scrollbar
        scrollbar = tk.Scrollbar(monitor_frame)
        scrollbar.pack(side='right', fill='y')

        # Text widget con scrollbar
        self.serial_monitor_text = tk.Text(
            monitor_frame,
            height=12,
            width=80,
            yscrollcommand=scrollbar.set,
            state='disabled',
            bg='#f0f0f0',
            font=('Courier New', 9),
        )
        self.serial_monitor_text.pack(side='left', fill='both', expand=True)
        scrollbar.config(command=self.serial_monitor_text.yview)

        # Botón para limpiar monitor
        button_frame = tk.Frame(self.root)
        button_frame.pack(fill='x', padx=10, pady=(0, 10))
        tk.Button(button_frame, text='Limpiar Monitor', command=self.clear_serial_monitor).pack(side='left')

    def clear_serial_monitor(self) -> None:
        """Limpiar el contenido del monitor serial"""
        if self.serial_monitor_text:
            self.serial_monitor_text.config(state='normal')
            self.serial_monitor_text.delete('1.0', 'end')
            self.serial_monitor_text.config(state='disabled')

    def add_to_serial_monitor(self, line: str) -> None:
        """Agregar una línea al monitor serial"""
        if self.serial_monitor_text:
            self.serial_monitor_text.config(state='normal')
            self.serial_monitor_text.insert('end', line + '\n')
            # Scroll automático al final
            self.serial_monitor_text.see('end')
            self.serial_monitor_text.config(state='disabled')

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
            self.ser = serial.Serial(port, 4800, timeout=2.0)
            time.sleep(1.0)  # Esperar a que Arduino esté listo
        except Exception as exc:
            messagebox.showerror('Error de conexion', str(exc))
            return

        # Si el baudrate deseado es diferente a 4800, cambiar
        if baudrate_deseado != 4800:
            self.status_var.set(f'Cambiando baudrate a {baudrate_deseado}...')
            self.root.update()

            try:
                # Limpiar buffers antes de enviar comando
                self.ser.reset_input_buffer()
                self.ser.reset_output_buffer()
                time.sleep(0.3)
                
                # Enviar comando SETBAUD al sender
                comando = f'SETBAUD:{baudrate_deseado}\n'
                self.ser.write(comando.encode())
                self.ser.flush()
                time.sleep(1.5)  # Esperar reinicio del Arduino
                
                # Desconectar y reconectar con nuevo baudrate
                self.ser.close()
                time.sleep(0.8)
                self.ser = serial.Serial(port, baudrate_deseado, timeout=2.0)
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
        self.clear_serial_monitor()
        self.add_to_serial_monitor(f"=== CONECTADO A {port} @ {baudrate_deseado} bps ===")
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
                # Agregar al monitor serial CON timestamp
                timestamp = datetime.now().strftime("%H:%M:%S")
                self.root.after(0, self.add_to_serial_monitor, f"[{timestamp}] {line}")
                
                # Procesar comandos
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
            self.guardar_resultado_csv('ERROR')
            self.reset_transfer_values()
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
            self.guardar_resultado_csv('COMPLETO')
            self.reset_transfer_values()
        elif '[INFO] Transferencia finalizada' in line:
            self.sender_status_vars['estado'].set('LISTO')
            self.sender_status_vars['ultimo_mensaje'].set('Listo para nuevo archivo')

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
            with self.serial_lock:
                self.ser.write(chunk_line.encode('ascii'))
                self.ser.flush()
            
            # Usar delay dinámico basado en baudrate y tamaño del chunk
            baudrate = int(self.baudrate_var.get())
            delay = calculate_chunk_delay(baudrate, length)
            time.sleep(delay)
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

    def guardar_resultado_csv(self, estado: str) -> None:
        """Guardar resultados de la transferencia en CSV"""
        if not self.tiempo_inicio:
            return
        
        resultados_dir = Path(__file__).resolve().parents[2] / 'resultados'
        resultados_dir.mkdir(parents=True, exist_ok=True)
        csv_file = resultados_dir / 'pruebas_transferencia.csv'
        
        timestamp = datetime.now().strftime('%Y-%m-%d %H:%M:%S')
        duracion = time.time() - self.tiempo_inicio
        
        frames_enviados = int(self.sender_status_vars['frames_enviados'].get() or 0)
        bytes_enviados = int(self.sender_status_vars['bytes_enviados'].get() or 0)
        tasa_error_str = self.sender_status_vars['tasa_error'].get().replace('%', '')
        try:
            tasa_error = float(tasa_error_str)
        except:
            tasa_error = 0.0
        
        headers = ['Timestamp', 'Baudrate', 'Payload', 'Ventana', 'Archivo', 'Bytes_Total', 'Bytes_Enviados', 'Frames_Total', 'Frames_Enviados', 'Duracion_seg', 'Tasa_Error_%', 'Estado']
        
        row = [
            timestamp,
            self.baudrate_var.get(),
            self.payload_size_var.get(),
            self.sliding_window_size.get(),
            self.nombre_archivo_actual,
            self.bytes_totales_actual,
            bytes_enviados,
            self.frames_totales_actual,
            frames_enviados,
            f'{duracion:.2f}',
            f'{tasa_error:.2f}',
            estado
        ]
        
        file_exists = csv_file.exists()
        try:
            with open(csv_file, 'a', newline='', encoding='utf-8') as f:
                writer = csv.writer(f)
                if not file_exists:
                    writer.writerow(headers)
                writer.writerow(row)
        except Exception as e:
            self.status_var.set(f'Error al guardar CSV: {e}')

    def reset_transfer_values(self) -> None:
        """Reset solo los valores de la transferencia actual"""
        self.sending = False
        self.tiempo_inicio = None
        self.pending_file_data = b''
        self.pending_payload_size = 0
        self.nombre_archivo_actual = ''
        self.frames_totales_actual = 0
        self.bytes_totales_actual = 0
        self.sender_status_vars['estado'].set('PREPARANDO')
        self.sender_status_vars['frames_enviados'].set('0')
        self.sender_status_vars['frames_totales'].set('-')
        self.sender_status_vars['bytes_enviados'].set('0')
        self.sender_status_vars['bytes_totales'].set('-')
        self.sender_status_vars['tasa_error'].set('0.00%')
        self.sender_status_vars['tiempo'].set('0s')
        self.sender_status_vars['ultimo_mensaje'].set('-')
        self.sender_status_vars['ultimo_ack'].set('-')
        self.sender_status_vars['ultimo_nack'].set('-')
        self.cancel_button.config(state='disabled')

    def reset_all_values(self) -> None:
        """Reset todas las variables a valores iniciales (incluyendo conexión)"""
        self.reset_transfer_values()
        self.send_file_path.set('')
        self.file_label.config(text='(Seleccione un archivo)')
        self.payload_size_var.set('100')
        self.sliding_window_size.set('3')
        self.baudrate_var.set('4800')
        self.status_var.set('Desconectado.')

    def cancel_send(self) -> None:
        """Cancelar envío en progreso"""
        self.sending = False
        self.cancel_button.config(state='disabled')
        self.status_var.set('Envío cancelado.')
        self.reset_all_values()

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
        self.nombre_archivo_actual = filename
        self.frames_totales_actual = total_frames
        self.bytes_totales_actual = len(file_data)

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
            start_cmd = f'START {filename} {len(file_data)} {payload_size} {window_size}\n'
            with self.serial_lock:
                self.ser.write(start_cmd.encode('utf-8'))
                self.ser.flush()
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


def main() -> None:
    root = tk.Tk()
    App(root)
    root.mainloop()


if __name__ == '__main__':
    main()
