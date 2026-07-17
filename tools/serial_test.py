#!/usr/bin/env python3
"""
EFilterWheel 串口测试工具

用于验证 Arduino Nano 滤镜轮的所有串口命令和错误码。
支持交互模式和脚本模式。

用法:
  python serial_test.py COM3              # 交互模式
  python serial_test.py COM3 --script test_sequence.txt  # 脚本模式
  python serial_test.py --list            # 列出可用串口

依赖:
  pip install pyserial
"""

import sys
import time
import argparse
import serial
import serial.tools.list_ports

# ============================================================================
# 配置
# ============================================================================
BAUD_RATE = 57600
TIMEOUT = 5.0  # 默认响应超时（秒）
HOMING_TIMEOUT = 60.0  # 回零超时

# ============================================================================
# 命令定义
# ============================================================================
COMMANDS = {
    "ID?":       "查询设备信息",
    "STATE?":    "查询设备状态",
    "POS?":      "查询当前槽位",
    "HOME":      "启动回零",
    "GOTO":      "移动到槽位 (需要参数: 0-6)",
    "STOP":      "紧急停止",
    "SLOTS":     "设置槽位数 (5 或 7)",
    "CAL?":      "查询标定参数",
    "CAL SLOT":  "设置槽位步数 (slot steps)",
    "CAL SPEED": "设置速度 (200-2000 pps)",
    "CAL DIR":   "设置方向 (CW/CCW)",
    "SAVE":      "保存配置到 EEPROM",
    "RESET":     "重启设备",
    "HELP":      "显示命令帮助",
}

# ============================================================================
# 串口工具类
# ============================================================================
class FilterWheelSerial:
    def __init__(self, port: str, baud: int = BAUD_RATE):
        self.port = port
        self.baud = baud
        self.ser: serial.Serial | None = None

    def open(self):
        try:
            self.ser = serial.Serial(self.port, self.baud, timeout=1.0)
            # 清空缓冲
            self.ser.reset_input_buffer()
            # 等待设备启动
            time.sleep(2.0)
            print(f"[INFO] Connected to {self.port} at {self.baud} baud")
        except serial.SerialException as e:
            print(f"[ERROR] Failed to open {self.port}: {e}")
            sys.exit(1)

    def close(self):
        if self.ser and self.ser.is_open:
            self.ser.close()

    def send_cmd(self, cmd: str, timeout: float = TIMEOUT) -> list[str]:
        """发送命令并收集响应行"""
        if not self.ser or not self.ser.is_open:
            return ["ERROR: Not connected"]

        # 发送命令
        full_cmd = cmd.strip() + "\n"
        self.ser.write(full_cmd.encode("utf-8"))
        print(f">>> {cmd.strip()}")

        # 读取响应
        lines = []
        deadline = time.time() + timeout
        while time.time() < deadline:
            line = self._read_line()
            if line:
                lines.append(line)
                print(f"    {line}")
                # 如果收到 OK 或 ERR 且不是多行帮助，退出
                if (line.startswith("OK ") or line.startswith("ERR ")) and cmd != "HELP":
                    # 但 HOME 命令的最终响应是 "OK HOMED"，等它
                    if cmd == "HOME" and "HOMED" not in line:
                        continue
                    # GOTO 命令的响应在移动完成后
                    if cmd.startswith("GOTO") and "GOTO" not in line and "ERR" not in line:
                        continue
                    break
                if "End of help" in line:
                    break
            else:
                time.sleep(0.05)

        return lines

    def _read_line(self) -> str:
        """读取一行（以 \\n 结尾）"""
        if not self.ser or not self.ser.is_open:
            return ""
        try:
            line = self.ser.readline()
            if line:
                return line.decode("utf-8", errors="replace").strip()
        except serial.SerialException:
            pass
        return ""

    def wait_for_homed(self, timeout: float = HOMING_TIMEOUT) -> bool:
        """等待回零完成"""
        deadline = time.time() + timeout
        while time.time() < deadline:
            line = self._read_line()
            if line:
                print(f"    {line}")
                if "HOMED" in line:
                    return True
                if "ERR" in line:
                    return False
            time.sleep(0.1)
        return False

# ============================================================================
# 测试序列
# ============================================================================
def run_test_suite(fw: FilterWheelSerial):
    """运行标准测试套件"""
    print("\n" + "=" * 60)
    print("  EFilterWheel Test Suite")
    print("=" * 60)

    tests = [
        ("1. 设备信息", "ID?"),
        ("2. 状态查询", "STATE?"),
        ("3. 位置查询", "POS?"),
        ("4. 命令帮助", "HELP"),
    ]

    passed = 0
    failed = 0

    for name, cmd in tests:
        print(f"\n--- {name} ---")
        lines = fw.send_cmd(cmd)
        if any("OK" in l for l in lines):
            passed += 1
        else:
            failed += 1
            print(f"  [FAIL] {name}")

    # 等待回零完成
    print("\n--- 等待回零完成 ---")
    if fw.wait_for_homed():
        passed += 1
    else:
        failed += 1
        print("  [FAIL] Homing")

    # 槽位切换测试
    print("\n--- 槽位切换测试 ---")
    for slot in range(5):
        lines = fw.send_cmd(f"GOTO {slot}", timeout=30.0)
        if any("OK" in l for l in lines):
            passed += 1
        else:
            failed += 1
            print(f"  [FAIL] GOTO {slot}")
        time.sleep(1)

    # 验证位置
    lines = fw.send_cmd("POS?")
    if any("OK" in l for l in lines):
        passed += 1
    else:
        failed += 1

    print(f"\n{'=' * 60}")
    print(f"  Results: {passed} passed, {failed} failed")
    print(f"{'=' * 60}")

    return failed == 0

# ============================================================================
# 交互模式
# ============================================================================
def interactive_mode(fw: FilterWheelSerial):
    """交互式命令行"""
    print("\n" + "=" * 60)
    print("  EFilterWheel Interactive Console")
    print("  输入命令，输入 'help' 查看命令列表，'quit' 退出")
    print("=" * 60)

    while True:
        try:
            cmd = input("\nCMD> ").strip()
        except (EOFError, KeyboardInterrupt):
            print("\nBye!")
            break

        if not cmd:
            continue

        if cmd.lower() in ("quit", "exit", "q"):
            break

        if cmd.lower() == "help":
            print("\n可用命令:")
            for c, desc in COMMANDS.items():
                print(f"  {c:12s} - {desc}")
            continue

        # 特殊处理 HOME 命令（等待 HOMED）
        if cmd.upper().startswith("HOME"):
            fw.send_cmd("HOME")
            print("  等待回零完成...")
            fw.wait_for_homed()
        elif cmd.upper().startswith("GOTO"):
            fw.send_cmd(cmd, timeout=30.0)
        else:
            fw.send_cmd(cmd)

# ============================================================================
# 脚本模式
# ============================================================================
def script_mode(fw: FilterWheelSerial, script_path: str):
    """从文件读取命令序列执行"""
    print(f"\n[INFO] Running script: {script_path}")

    with open(script_path, "r", encoding="utf-8") as f:
        lines = f.readlines()

    for i, line in enumerate(lines):
        line = line.strip()
        # 跳过注释和空行
        if not line or line.startswith("#"):
            continue

        # 解析命令和可选延时
        parts = line.split("|")
        cmd = parts[0].strip()
        delay = float(parts[1].strip()) if len(parts) > 1 else 0.5

        print(f"\n[{i+1}] ", end="")
        fw.send_cmd(cmd)

        if delay > 0:
            time.sleep(delay)

# ============================================================================
# 入口
# ============================================================================
def main():
    parser = argparse.ArgumentParser(
        description="EFilterWheel 串口测试工具",
        formatter_class=argparse.RawDescriptionHelpFormatter,
        epilog="""
示例:
  %(prog)s COM3                       交互模式
  %(prog)s COM3 --suite               运行测试套件
  %(prog)s COM3 --script test.txt     脚本模式
  %(prog)s --list                     列出可用串口
        """
    )
    parser.add_argument("port", nargs="?", help="串口名称 (如 COM3, /dev/ttyUSB0)")
    parser.add_argument("--baud", type=int, default=BAUD_RATE, help=f"波特率 (默认: {BAUD_RATE})")
    parser.add_argument("--suite", action="store_true", help="运行标准测试套件")
    parser.add_argument("--script", type=str, help="从脚本文件读取命令执行")
    parser.add_argument("--list", action="store_true", help="列出可用串口")

    args = parser.parse_args()

    if args.list:
        print("可用串口:")
        for port in serial.tools.list_ports.comports():
            print(f"  {port.device} - {port.description}")
        return

    if not args.port:
        parser.print_help()
        print("\n[ERROR] 请指定串口")
        sys.exit(1)

    # 连接设备
    fw = FilterWheelSerial(args.port, args.baud)
    fw.open()

    try:
        if args.suite:
            run_test_suite(fw)
        elif args.script:
            script_mode(fw, args.script)
        else:
            interactive_mode(fw)
    finally:
        fw.close()
        print("\n[INFO] Disconnected")

if __name__ == "__main__":
    main()
