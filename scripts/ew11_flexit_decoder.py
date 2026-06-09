#!/usr/bin/env python3
"""
Proof-of-concept decoder for Flexit CU60 / CI600 (CS60 bus) sensor data,
read through an Elfin EW11 RS485->WiFi/TCP adapter.

Unlike scripts/tcp_bridge_monitor.py (which talks to the ESPHome TCP bridge and
expects a 3-byte 'T'/'R'+length direction header), the EW11 is a *transparent*
serial<->TCP bridge: port 8899 delivers the raw Modbus RTU byte stream straight
off the RS485 wire, with both directions interleaved.

The CS60 controller does NOT honour the Modbus RTU interframe gap; it blasts
frames back-to-back. So we can't delimit by timing. Instead we reframe the same
way the ESPHome component does (see expected_frame_length() in
flexit_modbus_server.cpp): size each frame by its function code and let the CRC
confirm the boundary, resyncing one byte at a time when it doesn't.

The sensor readings live in holding registers that the CS60 continuously writes
(function 0x10 / 0x06 / 0x65). We watch those writes, keep a register table, and
print decoded values. Read responses (0x03) are decoded too when present.

Usage:
    python3 scripts/ew11_flexit_decoder.py --host 172.16.36.5 --port 8899
    python3 scripts/ew11_flexit_decoder.py --host 172.16.36.5 --raw      # hex dump frames
    python3 scripts/ew11_flexit_decoder.py --host 172.16.36.5 --changes  # only on value change
"""

import argparse
import socket
import sys
from datetime import datetime

# ---------------------------------------------------------------------------
# Holding register map (mirrors HoldingRegisterIndex in flexit_modbus_server.h)
# Each entry: address -> (name, kind)
#   kind: "temp"  -> signed value / 10.0  (degrees C)
#         "pct"   -> percentage
#         "mode"  -> ventilation mode enum
#         "raw"   -> plain integer
# ---------------------------------------------------------------------------
MODE_STRINGS = ["Stop", "Min", "Normal", "Max"]

REGISTERS = {
    0xBE: ("Temperature setpoint", "temp"),
    0xBF: ("Mode", "mode"),
    0xC0: ("Unknown 1 (max-timer?)", "raw"),
    0xC1: ("Unknown 2", "raw"),
    0xC2: ("Temperature setpoint 2", "temp"),
    # On the CU60/CI600 this register tracks the panel's "inside air" reading
    # (the header enum names it supply air; that label fits other Flexit models).
    0xC3: ("Inside/supply air temperature", "temp"),
    0xC4: ("Extract air temperature", "temp"),  # unconnected on CU60 -> reads garbage
    0xC5: ("Outdoor air temperature", "temp"),
    0xC6: ("Return water temperature", "temp"),
    0xC7: ("Cooling", "pct"),
    0xC8: ("Heat exchanger", "pct"),
    0xC9: ("Heating", "pct"),
    0xCA: ("Supply fan", "pct"),
    0x104: ("ALARM supply sensor faulty", "raw"),
    0x105: ("ALARM extract sensor faulty", "raw"),
    0x106: ("ALARM outdoor sensor faulty", "raw"),
    0x107: ("ALARM return-water sensor faulty", "raw"),
    0x108: ("ALARM overheat triggered", "raw"),
    0x109: ("ALARM external smoke triggered", "raw"),
    0x10A: ("ALARM water coil faulty", "raw"),
    0x10B: ("ALARM heat exchanger faulty", "raw"),
    0x10C: ("ALARM filter change", "raw"),
    0x10E: ("Heater status", "raw"),
}

# Pairs of (high, low) registers holding a 32-bit seconds counter -> hours.
RUNTIME_HOURS = {
    0x14C: "Runtime Stop",
    0x14E: "Runtime Min",
    0x150: "Runtime Normal",
    0x152: "Runtime Max",
    0x154: "Runtime Rotor",
    0x156: "Runtime Heater",
    0x15C: "Runtime total",
    0x15E: "Runtime filter",
}

MIN_FRAME_LENGTH = 5
MAX_FRAME_LENGTH = 256


def modbus_crc(data: bytes) -> int:
    """Modbus RTU CRC16 (low byte first on the wire)."""
    crc = 0xFFFF
    for byte in data:
        crc ^= byte
        for _ in range(8):
            if crc & 0x0001:
                crc = (crc >> 1) ^ 0xA001
            else:
                crc >>= 1
    return crc


def crc_ok(frame: bytes) -> bool:
    """frame includes the trailing 2 CRC bytes (little-endian)."""
    if len(frame) < 4:
        return False
    calc = modbus_crc(frame[:-2])
    recv = frame[-2] | (frame[-1] << 8)
    return calc == recv


def expected_frame_length(buf: bytes) -> int:
    """
    Total expected length of the frame starting at buf[0], or 0 if buf[1] is not
    a function code we delimit (caller should resync by +1). Mirrors
    expected_frame_length() in flexit_modbus_server.cpp.

    Returns a length > len(buf) when more bytes are still needed.
    """
    if len(buf) < 2:
        return len(buf) + 1  # need at least the function code
    function = buf[1]

    if function in (0x01, 0x03, 0x04, 0x06, 0x65):
        return 8
    if function == 0x10:  # write multiple registers, sized by byte-count field
        if len(buf) < 7:
            return len(buf) + 1  # need frame[6] (byte count) first
        byte_count = buf[6]
        length = byte_count + 9  # 7 header + data + 2 CRC
        return length if length <= MAX_FRAME_LENGTH else 0
    if function & 0x80:
        return 5  # exception response
    return 0  # unknown -> resync


def signed16(v: int) -> int:
    return v - 65536 if v >= 32768 else v


def fmt_value(addr: int, value: int) -> str:
    name, kind = REGISTERS.get(addr, (None, "raw"))
    if name is None:
        return f"  0x{addr:04X} = {value}"
    if kind == "temp":
        return f"  0x{addr:04X} {name}: {signed16(value) / 10.0:.1f} C"
    if kind == "pct":
        return f"  0x{addr:04X} {name}: {value} %"
    if kind == "mode":
        m = MODE_STRINGS[value] if value < len(MODE_STRINGS) else f"invalid({value})"
        return f"  0x{addr:04X} {name}: {m}"
    return f"  0x{addr:04X} {name}: {value}"


class RegisterTable:
    """Holds the latest value seen for each register and reports changes."""

    def __init__(self):
        self.values: dict[int, int] = {}

    def set(self, addr: int, value: int) -> bool:
        changed = self.values.get(addr) != value
        self.values[addr] = value
        return changed

    def runtime_hours(self, high_addr: int):
        hi = self.values.get(high_addr)
        lo = self.values.get(high_addr + 1)
        if hi is None or lo is None:
            return None
        return ((hi << 16) | lo) / 3600.0


def decode_frame(frame: bytes, table: RegisterTable, only_changes: bool):
    """Decode one CRC-valid Modbus frame and print any register updates."""
    slave = frame[0]
    fn = frame[1]
    ts = datetime.now().strftime("%H:%M:%S.%f")[:-3]
    updates: list[tuple[int, int]] = []

    if fn in (0x06, 0x65):  # write single register / custom write
        addr = (frame[2] << 8) | frame[3]
        value = (frame[4] << 8) | frame[5]
        updates.append((addr, value))

    elif fn == 0x10:  # write multiple registers
        start = (frame[2] << 8) | frame[3]
        count = (frame[4] << 8) | frame[5]
        byte_count = frame[6]
        data = frame[7:7 + byte_count]
        for i in range(count):
            if 2 * i + 1 < len(data):
                value = (data[2 * i] << 8) | data[2 * i + 1]
                updates.append((start + i, value))

    elif fn == 0x03:  # read holding registers response (byte-count form, len != 8)
        if len(frame) != 8:
            byte_count = frame[2]
            data = frame[3:3 + byte_count]
            # Response has no start address; we can't map to register names here,
            # so just dump the raw register values for reference.
            regs = [(data[i] << 8) | data[i + 1] for i in range(0, len(data) - 1, 2)]
            print(f"[{ts}] id=0x{slave:02X} 0x03 read response: {regs}")
        return

    else:
        return  # requests / coil polls / exceptions: nothing to extract here

    printed_header = False
    for addr, value in updates:
        changed = table.set(addr, value)
        if only_changes and not changed:
            continue
        if not printed_header:
            print(f"[{ts}] id=0x{slave:02X} fn=0x{fn:02X} write:")
            printed_header = True
        line = fmt_value(addr, value)
        if addr in RUNTIME_HOURS or (addr - 1) in RUNTIME_HOURS:
            base = addr if addr in RUNTIME_HOURS else addr - 1
            hrs = table.runtime_hours(base)
            if hrs is not None:
                line = f"  0x{base:04X} {RUNTIME_HOURS[base]}: {hrs:.1f} h"
        marker = "  *" if changed else ""
        print(f"{line}{marker}")


def recv_frames(host: str, port: int, raw: bool, only_changes: bool):
    print(f"Connecting to {host}:{port} ...")
    sock = socket.create_connection((host, port), timeout=10.0)
    print("Connected. Decoding Flexit CS60 bus (Ctrl-C to stop)\n")

    table = RegisterTable()
    buf = bytearray()

    try:
        while True:
            chunk = sock.recv(4096)
            if not chunk:
                print("\nConnection closed by remote host.")
                return
            buf.extend(chunk)

            # Reframe: same strategy as the ESPHome onRawBuffer handler.
            offset = 0
            while len(buf) - offset >= MIN_FRAME_LENGTH:
                view = bytes(buf[offset:])
                length = expected_frame_length(view)

                if length == 0:            # unknown start -> resync
                    offset += 1
                    continue
                if length > len(view):     # frame not fully arrived yet
                    break
                frame = view[:length]
                if not crc_ok(frame):      # right size, bad CRC -> resync
                    offset += 1
                    continue

                if raw:
                    ts = datetime.now().strftime("%H:%M:%S.%f")[:-3]
                    hexs = " ".join(f"{b:02X}" for b in frame)
                    print(f"[{ts}] ({length:3d}B) {hexs}")
                else:
                    decode_frame(frame, table, only_changes)

                offset += length

            # Drop consumed bytes, keep any trailing partial frame.
            del buf[:offset]
            # Guard against unbounded growth if we never resync.
            if len(buf) > 4 * MAX_FRAME_LENGTH:
                del buf[:-MAX_FRAME_LENGTH]
    finally:
        sock.close()


def scan_health(host: str, port: int, seconds: float):
    """
    Diagnostic: capture a few seconds and report how many CRC-valid Modbus frames
    are present (raw and under bit-inversion / bit-reversal). Use this to verify
    the EW11 serial settings: a healthy 115200 8N1 link shows many valid frames
    (function codes 01/03/06/10/65). All-zero counts + a stream dominated by 0xFF
    /0x55 means a baud-rate or wiring mismatch -- change the EW11 baud and re-scan.
    """
    rev = [int(f"{i:08b}"[::-1], 2) for i in range(256)]
    print(f"Capturing {seconds:.0f}s from {host}:{port} ...")
    sock = socket.create_connection((host, port), timeout=10.0)
    sock.settimeout(seconds)
    buf = bytearray()
    import time
    t0 = time.time()
    while time.time() - t0 < seconds:
        try:
            chunk = sock.recv(4096)
        except socket.timeout:
            break
        if not chunk:
            break
        buf.extend(chunk)
    sock.close()

    from collections import Counter
    top = Counter(buf).most_common(6)
    print(f"captured {len(buf)} bytes; top bytes: {[(f'{b:02X}', n) for b, n in top]}")

    def count_valid(data: bytes) -> tuple[int, Counter]:
        off, frames, fns = 0, 0, Counter()
        while len(data) - off >= MIN_FRAME_LENGTH:
            view = data[off:]
            length = expected_frame_length(view)
            if length == 0 or length > len(view):
                off += 1
                continue
            frame = view[:length]
            if crc_ok(frame):
                frames += 1
                fns[frame[1]] += 1
                off += length
            else:
                off += 1
        return frames, fns

    transforms = {
        "raw": bytes(buf),
        "bit-inverted (A/B swap)": bytes(b ^ 0xFF for b in buf),
        "bit-reversed": bytes(rev[b] for b in buf),
    }
    best = 0
    for label, data in transforms.items():
        n, fns = count_valid(data)
        best = max(best, n)
        detail = ", ".join(f"{k:02X}:{v}" for k, v in fns.most_common()) or "-"
        print(f"  {label:24s}: {n:4d} valid frames  [{detail}]")
    if best == 0:
        print("\nNo valid frames at the EW11's current baud. Change the baud rate on the")
        print("EW11 (try 9600, 19200, 38400, 57600, 115200, 250000) and re-run --scan.")
    else:
        print("\nValid frames found -> serial settings are correct; run without --scan to decode.")


def main():
    p = argparse.ArgumentParser(description=__doc__,
                                formatter_class=argparse.RawDescriptionHelpFormatter)
    p.add_argument("--host", default="172.16.36.5", help="EW11 IP address")
    p.add_argument("--port", type=int, default=8899, help="EW11 TCP port (default 8899)")
    p.add_argument("--raw", action="store_true", help="Hex-dump each valid frame instead of decoding")
    p.add_argument("--changes", action="store_true", help="Only print registers whose value changed")
    p.add_argument("--scan", action="store_true",
                   help="Diagnostic: report valid-frame counts to verify serial settings")
    p.add_argument("--scan-seconds", type=float, default=5.0, help="Capture window for --scan")
    args = p.parse_args()

    try:
        if args.scan:
            scan_health(args.host, args.port, args.scan_seconds)
        else:
            recv_frames(args.host, args.port, args.raw, args.changes)
    except KeyboardInterrupt:
        print("\nStopped.")
    except (ConnectionError, OSError) as e:
        print(f"ERROR: {e}")
        sys.exit(1)


if __name__ == "__main__":
    main()
