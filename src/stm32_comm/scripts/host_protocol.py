#!/usr/bin/env python3
# -*- coding: utf-8 -*-
import serial
import serial.tools.list_ports
import threading
import time
import re
import queue

class USBProtocol:
    def __init__(self, port=None, baudrate=115200):
        self.port = port
        self.baudrate = baudrate
        self.serial = None
        self.running = False
        self.rx_thread = None
        self.rx_queue = queue.Queue()
        self.callbacks = []
        
        # Match device return format: (x.yyy,y.yyy)
        self.rx_pattern = re.compile(r'\(([-+]?\d*\.?\d+),([-+]?\d*\.?\d+)\)')

    def find_device_port(self):
        """Find potential STM32 serial port"""
        ports = list(serial.tools.list_ports.comports())
        for p in ports:
            # Filter by VID:PID if known
            # STM32 default VID is usually 0483
            # Also check for common USB-Serial chips (CH340, CP210x, FTDI)
            desc = p.description.lower()
            hwid = p.hwid.lower()
            if "stm" in desc or "virtual port" in desc or \
               "ch340" in desc or "cp210" in desc or "ftdi" in desc or \
               "usb" in desc:
                return p.device
        # If no specific match, return the first available USB serial port
        for p in ports:
            if "USB" in p.description or "USB" in p.hwid:
                return p.device
        
        if ports:
            return ports[0].device
        return None

    def connect(self):
        if self.port is None:
            self.port = self.find_device_port()
            
        if self.port is None:
            raise Exception("未找到可用串口 (No serial port found)")

        try:
            self.serial = serial.Serial(self.port, self.baudrate, timeout=0.1)
            self.running = True
            self.rx_thread = threading.Thread(target=self._rx_loop, daemon=True)
            self.rx_thread.start()
            print(f"Connected to {self.port}")
            return True
        except Exception as e:
            print(f"Connection failed: {e}")
            return False

    def disconnect(self):
        self.running = False
        if self.rx_thread:
            self.rx_thread.join(timeout=1.0)
        if self.serial and self.serial.is_open:
            self.serial.close()
        print("Disconnected")

    def send_command(self, cmd_vel, cmd_wel, flag1=0, flag2=0, flag3=0, flag4=0):
        """
        Send command to device
        Format: (cmd_vel, cmd_wel, flag1, flag2, flag3, flag4)
        """
        if not self.serial or not self.serial.is_open:
            return False
        
        # Format string
        cmd_str = f"({cmd_vel:.3f},{cmd_wel:.3f},{int(flag1)},{int(flag2)},{int(flag3)},{int(flag4)})\n"
        try:
            self.serial.write(cmd_str.encode('utf-8'))
            return True
        except Exception as e:
            print(f"Send failed: {e}")
            return False

    def register_callback(self, callback):
        """Register data callback(x, y)"""
        self.callbacks.append(callback)

    def _rx_loop(self):
        buffer = ""
        while self.running and self.serial and self.serial.is_open:
            try:
                if self.serial.in_waiting:
                    data = self.serial.read(self.serial.in_waiting).decode('utf-8', errors='ignore')
                    buffer += data
                    
                    while '\n' in buffer:
                        line, buffer = buffer.split('\n', 1)
                        line = line.strip()
                        if not line:
                            continue
                            
                        match = self.rx_pattern.search(line)
                        if match:
                            try:
                                x = float(match.group(1))
                                y = float(match.group(2))
                                # Enqueue
                                self.rx_queue.put((x, y))
                                # Callback
                                for cb in self.callbacks:
                                    cb(x, y)
                            except ValueError:
                                pass
                else:
                    time.sleep(0.01)
            except Exception as e:
                print(f"Receive error: {e}")
                self.running = False
                break

if __name__ == "__main__":
    # Test code
    protocol = USBProtocol()
    
    def on_data_received(x, y):
        print(f"Received: X={x:.3f}, Y={y:.3f}")

    protocol.register_callback(on_data_received)
    
    try:
        if protocol.connect():
            print("Start sending sine wave... Press Ctrl+C to exit")
            t = 0
            while True:
                import math
                vel = math.sin(t)
                wel = math.cos(t)
                protocol.send_command(vel, wel, 1, 0, 0, 0)
                t += 0.1
                time.sleep(0.1)
    except Exception as e:
        print(e)
    except KeyboardInterrupt:
        pass
    finally:
        protocol.disconnect()
