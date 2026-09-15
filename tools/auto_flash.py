#!/usr/bin/env python3
"""Watch for newly attached CH340 serial adapters and flash STM32 firmware."""

from __future__ import annotations

import argparse
import shutil
import subprocess
import sys
import time
from pathlib import Path

try:
    import serial
    from serial.tools import list_ports
except ImportError:
    print("缺少依赖 pyserial，请执行: python -m pip install -r tools/requirements.txt", file=sys.stderr)
    raise SystemExit(2)


CH340_VID = 0x1A86
DEFAULT_CLI_PATHS = (
    Path(r"D:\Program Files\STMicroelectronics\STM32Cube\STM32CubeProgrammer\bin\STM32_Programmer_CLI.exe"),
    Path(r"C:\Program Files\STMicroelectronics\STM32Cube\STM32CubeProgrammer\bin\STM32_Programmer_CLI.exe"),
    Path(r"C:\Program Files (x86)\STMicroelectronics\STM32Cube\STM32CubeProgrammer\bin\STM32_Programmer_CLI.exe"),
)


def ch340_ports() -> dict[str, object]:
    """Return CH340/CH341 ports keyed by device name."""
    found = {}
    for port in list_ports.comports():
        text = " ".join((port.description or "", port.manufacturer or "", port.hwid or "")).lower()
        if port.vid == CH340_VID or "ch340" in text or "ch341" in text:
            found[port.device] = port
    return found


def find_cli(value: str | None) -> Path:
    if value:
        candidate = Path(value).expanduser().resolve()
        if candidate.is_file():
            return candidate
        raise FileNotFoundError(f"CubeProgrammer CLI 不存在: {candidate}")

    from_path = shutil.which("STM32_Programmer_CLI.exe") or shutil.which("STM32_Programmer_CLI")
    if from_path:
        return Path(from_path)
    for candidate in DEFAULT_CLI_PATHS:
        if candidate.is_file():
            return candidate
    raise FileNotFoundError("未找到 STM32_Programmer_CLI.exe，请用 --cli 指定路径")


def pulse_reset(port: str, baud: int, bootloader: bool, timeout: float,
                reset_pulse: float, boot_delay: float) -> bool:
    """Select BOOT using DTR, pulse NRST using RTS, then release the port.

    Board wiring:
      DTR asserted -> BOOT low; RTS asserted -> NRST low.
    pyserial's True means that the modem-control signal is asserted.
    """
    deadline = time.monotonic() + timeout
    while time.monotonic() < deadline:
        try:
            uart = serial.Serial(port=None, baudrate=baud, timeout=0.2)
            uart.dtr = not bootloader  # bootloader: deassert DTR -> BOOT high
            uart.rts = True            # assert RTS -> NRST low
            uart.port = port
            uart.open()
            try:
                time.sleep(reset_pulse)
                uart.rts = False       # release NRST; BOOT is sampled now
                time.sleep(boot_delay)
            finally:
                uart.close()
            return True
        except (serial.SerialException, OSError):
            time.sleep(0.25)
    return False


def wait_until_openable(port: str, baud: int, timeout: float) -> bool:
    """Probe the port without applying the board-specific boot sequence."""
    deadline = time.monotonic() + timeout
    while time.monotonic() < deadline:
        try:
            with serial.Serial(port=port, baudrate=baud, timeout=0.2):
                return True
        except (serial.SerialException, OSError):
            time.sleep(0.25)
    return False


def build_command(args: argparse.Namespace, cli: Path, port: str, firmware: Path) -> list[str]:
    # Keep both physical CH340 control outputs high while CubeProgrammer owns
    # the port: reset is released and BOOT remains high for the ROM loader.
    connect = f"port={port} br={args.baud} P={args.parity} db=8 sb=1"
    if args.auto_boot:
        connect += " rts=high dtr=high"
    command = [str(cli), "-c", *connect.split(), "-w", str(firmware)]
    if firmware.suffix.lower() == ".bin":
        command.append(args.address)
    if args.verify:
        command.append("-v")
    if args.reset:
        command.append("-rst")
    return command


def flash(args: argparse.Namespace, cli: Path, port: str, firmware: Path) -> bool:
    print(f"\n[{time.strftime('%H:%M:%S')}] 检测到 {port}，等待串口就绪...")
    if args.auto_boot:
        print("控制 DTR/RTS：BOOT=高，正在产生 NRST 复位脉冲...")
        ready = pulse_reset(port, args.baud, True, args.ready_timeout,
                            args.reset_pulse, args.boot_delay)
    else:
        ready = wait_until_openable(port, args.baud, args.ready_timeout)
    if not ready:
        print(f"[失败] {port} 无法打开（可能被其他程序占用）")
        return False

    command = build_command(args, cli, port, firmware)
    print(f"开始烧录: {firmware}")
    if args.dry_run:
        print("DRY RUN:", subprocess.list2cmdline(command))
        return True
    try:
        result = subprocess.run(command, check=False)
    except OSError as exc:
        print(f"[失败] 无法启动 CubeProgrammer: {exc}")
        return False
    if result.returncode == 0:
        if args.auto_boot:
            print("控制 DTR/RTS：BOOT=低，复位并启动用户程序...")
            if not pulse_reset(port, args.baud, False, args.ready_timeout,
                               args.reset_pulse, args.boot_delay):
                print(f"[警告] 固件已写入，但 {port} 无法执行烧录后硬件复位")
                return False
        print(f"[成功] {port} 烧录完成")
        return True
    print(f"[失败] CubeProgrammer 退出码: {result.returncode}")
    return False


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description="CH340 插入检测 + STM32CubeProgrammer 自动烧录")
    parser.add_argument("mode", choices=("once", "continuous"), help="once=单次；continuous=连续")
    parser.add_argument("firmware", type=Path, help="固件路径（.hex/.elf/.bin）")
    parser.add_argument("--cli", help="STM32_Programmer_CLI.exe 路径；不填则自动查找")
    parser.add_argument("--baud", type=int, default=115200, help="STM32 Bootloader 波特率")
    parser.add_argument("--parity", choices=("even", "odd", "none"), default="even")
    parser.add_argument("--address", default="0x08000000", help=".bin 烧录起始地址")
    parser.add_argument("--interval", type=float, default=0.5, help="串口扫描间隔（秒）")
    parser.add_argument("--ready-timeout", type=float, default=5.0, help="等待串口可打开的秒数")
    parser.add_argument("--reset-pulse", type=float, default=0.1, help="NRST 低电平脉宽（秒）")
    parser.add_argument("--boot-delay", type=float, default=0.2, help="释放 NRST 后等待时间（秒）")
    parser.add_argument("--no-auto-boot", dest="auto_boot", action="store_false",
                        help="不通过 DTR/RTS 控制 BOOT 和 NRST")
    parser.add_argument("--include-existing", action="store_true", help="启动时也处理已连接的 CH340")
    parser.add_argument("--no-verify", dest="verify", action="store_false", help="不校验固件")
    parser.add_argument("--no-reset", dest="reset", action="store_false", help="烧录后不复位 MCU")
    parser.add_argument("--dry-run", action="store_true", help="只显示命令，不执行烧录")
    parser.set_defaults(verify=True, reset=True, auto_boot=True)
    return parser.parse_args()


def main() -> int:
    args = parse_args()
    firmware = args.firmware.expanduser().resolve()
    if not firmware.is_file():
        print(f"固件不存在: {firmware}", file=sys.stderr)
        return 2
    if firmware.suffix.lower() not in {".hex", ".elf", ".bin", ".axf"}:
        print(f"不支持的固件格式: {firmware.suffix}", file=sys.stderr)
        return 2
    try:
        cli = find_cli(args.cli)
    except FileNotFoundError as exc:
        print(exc, file=sys.stderr)
        return 2

    present = ch340_ports()
    handled = set() if args.include_existing else set(present)
    print(f"CubeProgrammer: {cli}")
    print(f"固件: {firmware}")
    print("模式:", "单次烧录" if args.mode == "once" else "连续烧录（设备拔出后可再次烧录）")
    if present and not args.include_existing:
        print("启动时已有 CH340（将忽略到它拔出重插）:", ", ".join(present))
    print("正在等待新增 CH340，按 Ctrl+C 停止...")

    try:
        while True:
            current = ch340_ports()
            current_names = set(current)
            handled.intersection_update(current_names)
            for port in sorted(current_names - handled):
                handled.add(port)
                success = flash(args, cli, port, firmware)
                if args.mode == "once":
                    return 0 if success else 1
            time.sleep(max(args.interval, 0.1))
    except KeyboardInterrupt:
        print("\n已停止。")
        return 0


if __name__ == "__main__":
    raise SystemExit(main())
