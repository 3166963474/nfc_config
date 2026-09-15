#!/usr/bin/env python3
"""Automatically flash each target attached to a DAPLink/CMSIS-DAP probe."""

from __future__ import annotations

import argparse
import subprocess
import sys
import time
from pathlib import Path

try:
    import pyocd
    from pyocd.core.helpers import ConnectHelper
    from pyocd.core.session import Session
    from pyocd.coresight.dap import DPConnector
    from pyocd.probe.debug_probe import DebugProbe
    from elftools.elf.elffile import ELFFile
    from intelhex import IntelHex
except ImportError:
    print("缺少依赖 pyOCD，请执行: python -m pip install -r requirements.txt", file=sys.stderr)
    raise SystemExit(2)


SUPPORTED_SUFFIXES = {".hex", ".elf", ".axf", ".bin"}
TARGET_REMOVED_CONFIRM_COUNT = 3


def sound_result(success: bool, enabled: bool) -> None:
    """Play distinct success/failure patterns on Windows."""
    if not enabled:
        return
    try:
        import winsound

        pattern = ((1500, 120), (0, 70), (1900, 180)) if success else (
            (450, 280), (0, 80), (450, 280), (0, 80), (450, 450)
        )
        for frequency, duration_ms in pattern:
            if frequency == 0:
                time.sleep(duration_ms / 1000.0)
            else:
                winsound.Beep(frequency, duration_ms)
    except (ImportError, RuntimeError):
        # Terminal bell fallback for systems without winsound or a usable buzzer.
        print("\a", end="", flush=True)


def connected_probes() -> dict[str, str]:
    """Return all pyOCD-compatible probes keyed by their stable unique ID."""
    found: dict[str, str] = {}
    for probe in ConnectHelper.get_all_connected_probes(blocking=False):
        uid = str(probe.unique_id or "").strip()
        if not uid:
            continue
        description = str(getattr(probe, "description", "") or "CMSIS-DAP")
        found[uid] = description
    return found


def target_is_connected(args: argparse.Namespace, uid: str) -> bool:
    """Check the SWD-DP IDCODE without target discovery, reset, or halt."""
    probe = None
    session = None
    try:
        probes = ConnectHelper.get_all_connected_probes(blocking=False, unique_id=uid)
        probe = next((item for item in probes if str(item.unique_id) == uid), None)
        if probe is None:
            return False

        # A pyOCD probe must be associated with a Session before it can open.
        # init_board=False avoids core discovery, reset, and halt. DPConnector
        # sends the required JTAG-to-SWD/line-reset sequence before reading the
        # IDCODE, which a bare probe.read_dp() does not do reliably.
        session = Session(
            probe,
            auto_open=False,
            options={
                "frequency": args.frequency,
                "target_override": args.target,
                "no_config": True,
            },
        )
        session.open(init_board=False)
        probe.connect(DebugProbe.Protocol.SWD)
        connector = DPConnector(probe)
        connector.connect()
        idcode = connector.idr.idr
        return idcode not in (0x00000000, 0xFFFFFFFF)
    except Exception:
        return False
    finally:
        if session is not None:
            try:
                session.close()
            except Exception:
                pass


def build_command(args: argparse.Namespace, uid: str, firmware: Path) -> list[str]:
    command = [
        sys.executable,
        "-m",
        "pyocd",
        "load",
        str(firmware),
        "--target",
        args.target,
        "--uid",
        uid,
        "--frequency",
        str(args.frequency),
        "--erase",
        args.erase,
    ]
    if firmware.suffix.lower() == ".bin":
        command.extend(("--base-address", args.address))
    elif firmware.suffix.lower() == ".axf":
        command.extend(("--format", "elf"))
    # Verification is performed by this program after pyOCD writes. Keep the
    # core halted until that readback has completed.
    if args.verify or not args.reset:
        command.append("--no-reset")
    return command


def firmware_segments(firmware: Path, bin_address: str) -> list[tuple[int, bytes]]:
    """Load address/data segments for byte-for-byte readback verification."""
    suffix = firmware.suffix.lower()
    if suffix == ".bin":
        return [(int(bin_address, 0), firmware.read_bytes())]
    if suffix == ".hex":
        image = IntelHex(str(firmware))
        return [
            (start, bytes(image.tobinarray(start=start, end=end - 1)))
            for start, end in image.segments()
        ]

    segments: list[tuple[int, bytes]] = []
    with firmware.open("rb") as stream:
        elf = ELFFile(stream)
        for segment in elf.iter_segments():
            if segment["p_type"] != "PT_LOAD" or segment["p_filesz"] == 0:
                continue
            address = int(segment["p_paddr"] or segment["p_vaddr"])
            segments.append((address, segment.data()))
    return segments


def verify_and_reset(args: argparse.Namespace, uid: str, firmware: Path) -> bool:
    segments = firmware_segments(firmware, args.address)
    session = ConnectHelper.session_with_chosen_probe(
        unique_id=uid,
        target_override=args.target,
        frequency=args.frequency,
        blocking=False,
        connect_mode="halt",
    )
    if session is None:
        print("[失败] 写入后无法重新连接探针进行校验")
        return False

    print("正在逐字节回读校验...")
    with session:
        for address, expected in segments:
            for offset in range(0, len(expected), 1024):
                block = expected[offset:offset + 1024]
                actual = bytes(session.target.read_memory_block8(address + offset, len(block)))
                if actual != block:
                    first = next(i for i, pair in enumerate(zip(actual, block)) if pair[0] != pair[1])
                    fail_address = address + offset + first
                    print(f"[失败] 校验不一致，首个错误地址: 0x{fail_address:08X}")
                    return False
        if args.reset:
            session.target.reset()
    return True


def flash(args: argparse.Namespace, uid: str, description: str, firmware: Path) -> bool:
    print(f"\n[{time.strftime('%H:%M:%S')}] 检测到探针: {description}")
    print(f"UID: {uid}")
    command = build_command(args, uid, firmware)
    print(f"开始烧录: {firmware}")
    if args.dry_run:
        print("DRY RUN:", subprocess.list2cmdline(command))
        return True

    try:
        result = subprocess.run(command, check=False)
    except OSError as exc:
        print(f"[失败] 无法启动 pyOCD: {exc}")
        return False

    if result.returncode == 0:
        if args.verify:
            try:
                if not verify_and_reset(args, uid, firmware):
                    return False
            except Exception as exc:
                print(f"[失败] 回读校验异常: {exc}")
                return False
        stages = ["烧录"]
        if args.verify:
            stages.append("校验")
        if args.reset:
            stages.append("复位")
        print(f"[成功] {uid} {'、'.join(stages)}完成")
        return True
    print(f"[失败] pyOCD 退出码: {result.returncode}")
    return False


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description="DAPLink/CMSIS-DAP 插入检测 + pyOCD 自动烧录")
    parser.add_argument("mode", choices=("once", "continuous"), help="once=单次；continuous=连续")
    parser.add_argument("firmware", type=Path, help="固件路径（.hex/.elf/.axf/.bin）")
    parser.add_argument("--target", default="stm32f103rc", help="pyOCD 目标名称")
    parser.add_argument("--frequency", type=int, default=10_000_000, help="SWD 时钟频率（Hz）")
    parser.add_argument("--erase", choices=("auto", "chip", "sector"), default="auto",
                        help="擦除方式，默认 auto")
    parser.add_argument("--address", default="0x08000000", help=".bin 烧录起始地址")
    parser.add_argument("--interval", type=float, default=0.5, help="探针扫描间隔（秒）")
    parser.add_argument("--include-existing", action="store_true",
                        help="单次模式启动时也处理已连接的目标板（连续模式默认处理）")
    parser.add_argument("--uid", help="只处理指定UID的探针")
    parser.add_argument("--no-verify", dest="verify", action="store_false", help="烧录后不校验")
    parser.add_argument("--no-reset", dest="reset", action="store_false", help="烧录后不复位目标芯片")
    parser.add_argument("--dry-run", action="store_true", help="只显示命令，不执行烧录")
    parser.add_argument("--no-sound", dest="sound", action="store_false", help="关闭成功/失败声音")
    parser.set_defaults(verify=True, reset=True, sound=True)
    return parser.parse_args()


def main() -> int:
    args = parse_args()
    firmware = args.firmware.expanduser().resolve()
    if not firmware.is_file():
        print(f"固件不存在: {firmware}", file=sys.stderr)
        return 2
    if firmware.suffix.lower() not in SUPPORTED_SUFFIXES:
        print(f"不支持的固件格式: {firmware.suffix}", file=sys.stderr)
        return 2
    if args.frequency <= 0:
        print("--frequency 必须大于0", file=sys.stderr)
        return 2

    try:
        present = connected_probes()
    except Exception as exc:
        print(f"无法枚举DAPLink/CMSIS-DAP探针: {exc}", file=sys.stderr)
        return 2
    if args.uid:
        present = {uid: desc for uid, desc in present.items() if uid == args.uid}

    handle_existing = args.include_existing or args.mode == "continuous"
    handled = set() if handle_existing else set(present)
    print(f"pyOCD: {pyocd.__version__}")
    print(f"目标芯片: {args.target}，SWD频率: {args.frequency} Hz")
    print(f"固件: {firmware}")
    print("模式:", "单次烧录" if args.mode == "once" else "连续烧录（更换目标板后自动再次烧录）")
    if present and not handle_existing:
        print("启动时已有探针/目标板（单次模式将忽略到重新连接）:", ", ".join(present))
    print("正在等待DAPLink和目标板，按 Ctrl+C 停止...")

    last_error = ""
    target_missing_counts: dict[str, int] = {}
    try:
        while True:
            try:
                current = connected_probes()
                if args.uid:
                    current = {uid: desc for uid, desc in current.items() if uid == args.uid}
                last_error = ""
            except Exception as exc:
                message = str(exc)
                if message != last_error:
                    print(f"[警告] 探针扫描失败，将继续重试: {message}")
                    last_error = message
                time.sleep(max(args.interval, 0.1))
                continue

            current_ids = set(current)
            handled.intersection_update(current_ids)
            target_missing_counts = {
                uid: count for uid, count in target_missing_counts.items() if uid in current_ids
            }

            # A probe can remain connected to USB while its SWD cable is moved
            # from one product to the next. Require several failed checks so a
            # transient SWD error cannot accidentally re-arm the same board.
            for uid in sorted(current_ids & handled):
                if target_is_connected(args, uid):
                    target_missing_counts[uid] = 0
                else:
                    count = target_missing_counts.get(uid, 0) + 1
                    target_missing_counts[uid] = count
                    if count >= TARGET_REMOVED_CONFIRM_COUNT:
                        handled.discard(uid)
                        target_missing_counts.pop(uid, None)
                        print(f"[{time.strftime('%H:%M:%S')}] 目标板已移除，等待下一块板...")

            for uid in sorted(current_ids - handled):
                if not target_is_connected(args, uid):
                    continue
                handled.add(uid)
                success = flash(args, uid, current[uid], firmware)
                sound_result(success, args.sound)
                if args.mode == "once":
                    return 0 if success else 1
            time.sleep(max(args.interval, 0.1))
    except KeyboardInterrupt:
        print("\n已停止。")
        return 0


if __name__ == "__main__":
    raise SystemExit(main())
