#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
USB CDC 测试脚本
- 向设备发送指令帧: (cmd_vel,cmd_wel,flag1,flag2,flag3,flag4)\n
- 从设备接收数据帧: (x,y)\r\n

功能:
- 列出可用串口: --list
- 自动/指定串口连接: --port COMx
- 周期性发送测试帧: --tx-interval 100 (ms)
- 交互模式从命令行输入发送: --interactive

依赖:
- pyserial
"""
from __future__ import annotations
import argparse
import threading
import time
import re
import sys
import math
from typing import Optional

try:
    import serial
    import serial.tools.list_ports as list_ports
except Exception as e:
    print("[ERROR] 需要安装 pyserial: pip install pyserial", file=sys.stderr)
    raise

FRAME_TX_FMT = "({:.3f},{:.3f},{:d},{:d},{:d},{:d})\n"  # 发送格式
FRAME_RX_REGEX = re.compile(r"\(\s*([-+]?\d*\.?\d+)\s*,\s*([-+]?\d*\.?\d+)\s*\)")


def list_serial_ports() -> None:
    ports = list(list_ports.comports())
    if not ports:
        print("未发现可用串口。")
        return
    print("可用串口：")
    for p in ports:
        desc = f"{p.device} : {p.description}"
        if p.vid is not None and p.pid is not None:
            desc += f"  VID:PID={p.vid:04X}:{p.pid:04X}"
        print("  ", desc)


def pick_default_port() -> Optional[str]:
    """尽力推断一个可能的 CDC 端口。"""
    candidates = list(list_ports.comports())
    if not candidates:
        return None
    # 优先 ST 的 CDC 设备 (常见 VID:PID 0483:5740)
    for p in candidates:
        if (p.vid, p.pid) == (0x0483, 0x5740):
            return p.device
    # 其次包含 USB 或 CDC 字样的
    for p in candidates:
        text = f"{p.description} {p.manufacturer} {p.hwid}".lower()
        if any(k in text for k in ["usb", "cdc", "stmicro", "serial"]):
            return p.device
    # 否则返回第一个
    return candidates[0].device


class SerialWorker:
    def __init__(self, port: str, baud: int = 115200, timeout: float = 0.1):
        self.port = port
        self.baud = baud
        self.timeout = timeout
        self.ser: Optional[serial.Serial] = None
        self._stop = threading.Event()
        self.rx_thread: Optional[threading.Thread] = None

    def open(self) -> None:
        self.ser = serial.Serial(self.port, self.baud, timeout=self.timeout)
        # CDC 虚拟串口的波特率并不实际生效，但 pyserial 需要一个数值
        print(f"[INFO] 已打开串口 {self.port} @ {self.baud}")

    def close(self) -> None:
        self._stop.set()
        if self.rx_thread and self.rx_thread.is_alive():
            self.rx_thread.join(timeout=1.0)
        if self.ser and self.ser.is_open:
            self.ser.close()
            print("[INFO] 串口已关闭")

    def start_rx(self) -> None:
        assert self.ser is not None
        self.rx_thread = threading.Thread(target=self._rx_loop, daemon=True)
        self.rx_thread.start()

    def _rx_loop(self) -> None:
        assert self.ser is not None
        buf = bytearray()
        while not self._stop.is_set():
            try:
                chunk = self.ser.read_until(expected=b"\n")
                if not chunk:
                    continue
                buf.extend(chunk)
                # 尝试逐行解析 (x,y)
                while True:
                    nl = buf.find(b"\n")
                    if nl == -1:
                        break
                    line = buf[:nl].decode(errors="ignore").strip("\r\n\0 ")
                    del buf[: nl + 1]
                    self._handle_line(line)
            except serial.SerialException as e:
                print(f"[ERR] 串口异常: {e}")
                time.sleep(0.2)
            except Exception as e:
                print(f"[ERR] 解析异常: {e}")

    @staticmethod
    def _handle_line(line: str) -> None:
        # 仅匹配形如 (x,y) 的帧
        m = FRAME_RX_REGEX.search(line)
        if m:
            try:
                x = float(m.group(1))
                y = float(m.group(2))
                print(f"[RX] x={x:.3f}, y={y:.3f}")
            except ValueError:
                pass
        else:
            # 打印其他调试输出
            if line:
                print(f"[RX-RAW] {line}")

    def send_frame(self, cmd_vel: float, cmd_wel: float, f1: int, f2: int, f3: int, f4: int) -> None:
        assert self.ser is not None
        frame = FRAME_TX_FMT.format(cmd_vel, cmd_wel, int(f1), int(f2), int(f3), int(f4))
        self.ser.write(frame.encode())
        # 可选 flush：CDC 通常不必
        # self.ser.flush()
        print(f"[TX] {frame.strip()}")


def run_periodic(worker: SerialWorker, interval_ms: int, amplitude: float) -> None:
    """周期发送正弦测试帧，并持续接收打印。"""
    worker.start_rx()
    t0 = time.time()
    try:
        while True:
            t = time.time() - t0
            vel = amplitude * math.sin(2 * math.pi * 0.2 * t)  # 0.2Hz
            wel = amplitude * math.cos(2 * math.pi * 0.2 * t)
            worker.send_frame(vel, wel, 1, 0, 0, 1)
            time.sleep(max(0.0, interval_ms / 1000.0))
    except KeyboardInterrupt:
        print("\n[INFO] 停止发送")


def run_interactive(worker: SerialWorker) -> None:
    """交互模式：输入六个字段回车发送。示例: 0.2 0.05 1 0 0 1"""
    worker.start_rx()
    print("输入六个字段并回车发送: cmd_vel cmd_wel f1 f2 f3 f4；Ctrl+C 退出")
    try:
        while True:
            line = input("> ").strip()
            if not line:
                continue
            parts = line.replace(",", " ").split()
            if len(parts) != 6:
                print("格式错误，应为 6 个值，例如: 0.2 0.05 1 0 0 1")
                continue
            try:
                vel = float(parts[0]); wel = float(parts[1])
                f1 = int(parts[2]); f2 = int(parts[3]); f3 = int(parts[4]); f4 = int(parts[5])
            except ValueError:
                print("数值解析失败，请重试")
                continue
            worker.send_frame(vel, wel, f1, f2, f3, f4)
    except KeyboardInterrupt:
        print("\n[INFO] 退出交互模式")


def main():
    ap = argparse.ArgumentParser(description="USB CDC 发送/接收测试")
    ap.add_argument("--list", action="store_true", help="列出可用串口并退出")
    ap.add_argument("--port", type=str, default=None, help="串口号，如 COM5 (Windows) 或 /dev/ttyACM0 (Linux)")
    ap.add_argument("--baud", type=int, default=115200, help="波特率（CDC 虚拟串口可忽略）")
    ap.add_argument("--tx-interval", type=int, default=100, help="周期发送间隔 ms")
    ap.add_argument("--amplitude", type=float, default=0.2, help="正弦测试幅值")
    ap.add_argument("--interactive", action="store_true", help="交互模式从控制台输入发送")
    args = ap.parse_args()

    if args.list:
        list_serial_ports()
        return

    port = args.port or pick_default_port()
    if not port:
        print("[ERROR] 未能找到可用串口，请用 --list 查看并通过 --port 指定")
        sys.exit(2)

    worker = SerialWorker(port=port, baud=args.baud)
    try:
        worker.open()
        if args.interactive:
            run_interactive(worker)
        else:
            run_periodic(worker, interval_ms=args.tx_interval, amplitude=args.amplitude)
    finally:
        worker.close()


if __name__ == "__main__":
    main()
