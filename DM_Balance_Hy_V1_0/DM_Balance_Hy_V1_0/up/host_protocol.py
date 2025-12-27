# -*- coding: gbk -*-
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
        
        # 匹配设备发送的格式: (x.yyy,y.yyy)
        self.rx_pattern = re.compile(r'\(([-+]?\d*\.?\d+),([-+]?\d*\.?\d+)\)')

    def find_device_port(self):
        """查找可能的 STM32 虚拟串口"""
        ports = list(serial.tools.list_ports.comports())
        for p in ports:
            # 这里可以根据 VID:PID 过滤，如果知道的话
            # STM32 默认 VID 通常是 0483
            if "STM" in p.description or "Virtual Port" in p.description:
                return p.device
        if ports:
            return ports[0].device
        return None

    def connect(self):
        if self.port is None:
            self.port = self.find_device_port()
            
        if self.port is None:
            raise Exception("未找到可用串口")

        try:
            self.serial = serial.Serial(self.port, self.baudrate, timeout=0.1)
            self.running = True
            self.rx_thread = threading.Thread(target=self._rx_loop, daemon=True)
            self.rx_thread.start()
            print(f"已连接到 {self.port}")
            return True
        except Exception as e:
            print(f"连接失败: {e}")
            return False

    def disconnect(self):
        self.running = False
        if self.rx_thread:
            self.rx_thread.join(timeout=1.0)
        if self.serial and self.serial.is_open:
            self.serial.close()
        print("已断开连接")

    def send_command(self, cmd_vel, cmd_wel, flag1=0, flag2=0, flag3=0, flag4=0):
        """
        发送指令到设备
        格式: (cmd_vel, cmd_wel, flag1, flag2, flag3, flag4)
        """
        if not self.serial or not self.serial.is_open:
            return False
        
        # 格式化字符串
        cmd_str = f"({cmd_vel:.3f},{cmd_wel:.3f},{int(flag1)},{int(flag2)},{int(flag3)},{int(flag4)})\n"
        try:
            self.serial.write(cmd_str.encode('utf-8'))
            return True
        except Exception as e:
            print(f"发送失败: {e}")
            return False

    def register_callback(self, callback):
        """注册接收数据回调函数 callback(x, y)"""
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
                                # 放入队列
                                self.rx_queue.put((x, y))
                                # 调用回调
                                for cb in self.callbacks:
                                    cb(x, y)
                            except ValueError:
                                pass
                else:
                    time.sleep(0.01)
            except Exception as e:
                print(f"接收错误: {e}")
                self.running = False
                break

if __name__ == "__main__":
    # 测试代码
    protocol = USBProtocol()
    
    def on_data_received(x, y):
        print(f"收到数据: X={x:.3f}, Y={y:.3f}")

    protocol.register_callback(on_data_received)
    
    if protocol.connect():
        try:
            print("开始发送测试指令... 按 Ctrl+C 退出")
            t = 0
            while True:
                # 发送模拟的正弦波速度指令
                import math
                vel = math.sin(t)
                wel = math.cos(t)
                protocol.send_command(vel, wel, 1, 0, 0, 0)
                t += 0.1
                time.sleep(0.1)
        except KeyboardInterrupt:
            pass
        finally:
            protocol.disconnect()
