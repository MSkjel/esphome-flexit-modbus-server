"""
TCP Bridge Monitor for Flexit Modbus Server
Connects to the ESP TCP bridge, or reads a capture saved with --save, and displays the bus traffic with Modbus
frame decoding and the ESP's times.
"""

import argparse
import socket
import sys
from collections.abc import Callable, Iterator
from contextlib import nullcontext

# (server address, coil number)
Coil = tuple[int, int]


BYTE_US = 10e6 / 115200  # a byte on the bus, start and stop bit included

FUNCTION_CODES = {
    0x01: "Read Coils",
    0x03: "Read Holding Registers",
    0x04: "Read Input Registers",
    0x06: "Write Single Register",
    0x10: "Write Multiple Registers",
    0x64: "Panel Update",
    0x65: "CX60 Write",  # the value the CX60 took for a register it read
}


class ModbusFrameParser:
    """Decode the bus's frames. Keeps each server's last read, to tell its reply apart."""

    def __init__(self):
        # (server, function) -> (start, quantity) of its last read
        self.reads: dict[tuple[int, int], tuple[int, int]] = {}

    @staticmethod
    def signed(value: int) -> int:
        return value - 65536 if value >= 32768 else value

    def decode_frame(
        self, frame: bytes, verbose: bool = False
    ) -> tuple[str, dict[Coil, bool] | None]:
        """
        Decode Modbus frame into human-readable format.
        Returns: (decoded_string, coil states by server and coil, or None)
        """
        if len(frame) < 4:
            return "Invalid frame", None

        server = frame[0]
        function_code = frame[1]
        fn_name = FUNCTION_CODES.get(function_code, f"Unknown (0x{function_code:02X})")
        head = f"ID:{server:02X} {fn_name}"

        if function_code in (0x01, 0x03, 0x04):
            read = self.reads.pop((server, function_code), None)
            # A reply has its byte count where a request has the start's high byte
            if read and frame[2] + 5 == len(frame):
                return self.decode_reply(
                    head, server, function_code, frame, read, verbose
                )
            if len(frame) == 8:
                start = (frame[2] << 8) | frame[3]
                quantity = (frame[4] << 8) | frame[5]
                self.reads[(server, function_code)] = (start, quantity)
                return f"{head} Request Addr:0x{start:04X} Qty:{quantity}", None

        if function_code in (0x06, 0x65) and len(frame) == 8:
            addr = (frame[2] << 8) | frame[3]
            value = (frame[4] << 8) | frame[5]
            return f"{head} Addr:0x{addr:04X} Val:{self.signed(value)}", None

        if function_code == 0x10 and len(frame) >= 9:
            start = (frame[2] << 8) | frame[3]
            values = [
                self.signed((frame[i] << 8) | frame[i + 1])
                for i in range(7, len(frame) - 3, 2)
            ]
            shown = values if verbose else values[:10]
            more = "..." if len(shown) < len(values) else ""
            return (
                f"{head} Addr:0x{start:04X} Qty:{len(values)} Vals:[{', '.join(map(str, shown))}{more}]",
                None,
            )

        if function_code == 0x64 and len(frame) >= 6:
            # [addr 64 sub len data[len] crc], both ways
            data = frame[4:-2]
            text = f"{head} Sub:{frame[2]} Len:{frame[3]}"
            return (
                f"{text} Data:{format_hex(data)}" if verbose and data else text
            ), None

        return f"{head} Len:{len(frame)}", None

    def decode_reply(
        self,
        head: str,
        server: int,
        function_code: int,
        frame: bytes,
        read: tuple[int, int],
        verbose: bool,
    ) -> tuple[str, dict[Coil, bool] | None]:
        start, quantity = read
        data = frame[3 : 3 + frame[2]]

        if function_code == 0x01:
            coils = {
                (server, start + i): bool(data[i // 8] & (1 << (i % 8)))
                for i in range(min(quantity, 8 * len(data)))
            }
            on = [coil for (_, coil), state in coils.items() if state]
            text = f"{head} Response from 0x{start:04X} ({len(on)}/{len(coils)} ON)"
            if verbose:
                text += f" [{', '.join(f'0x{coil:04X}' for coil in on)}]"
            return text, coils

        registers = [
            self.signed((data[i] << 8) | data[i + 1])
            for i in range(0, len(data) - 1, 2)
        ]
        shown = registers if verbose else registers[:5]
        more = "..." if len(shown) < len(registers) else ""
        return (
            f"{head} Response from 0x{start:04X} Regs:[{', '.join(map(str, shown))}{more}]",
            None,
        )


class CoilTracker:
    """Track coil state changes."""

    def __init__(self):
        self.coil_states: dict[Coil, bool] = {}

    def update(self, new_states: dict[Coil, bool]) -> list[tuple[Coil, bool, bool]]:
        """
        Update coil states and return list of changes.
        Returns: list of (coil, old_state, new_state) for changed coils
        """
        changes = []
        for coil, new_state in new_states.items():
            old_state = self.coil_states.get(coil)
            if old_state is not None and old_state != new_state:
                changes.append((coil, old_state, new_state))
            self.coil_states[coil] = new_state
        return changes


def format_hex(data: bytes) -> str:
    """Format bytes as hex string."""
    return " ".join(f"{b:02X}" for b in data)


class BusClock:
    """Each frame's start from the first frame's, and the quiet line before it, from the ESP's times."""

    def __init__(self):
        self.last: int | None = None  # the time the ESP gave the frame before
        self.end = 0.0  # when that frame ended, in µs

    def frame(self, t: int, length: int) -> tuple[float, float | None]:
        """A frame that ended at t: its start in s and the gap before it in ms, None for the first"""
        took = length * BYTE_US
        if self.last is None:
            self.last, self.end = t, took
            return 0.0, None
        # The µs wrap every 71 minutes, and a frame can end before the one above it
        step = (t - self.last + 2**31) % 2**32 - 2**31
        start = self.end + step - took
        gap = start - self.end
        self.last, self.end = t, self.end + step
        return start / 1e6, gap / 1e3


def recv_exact(sock: socket.socket, n: int) -> bytes:
    """Receive exactly n bytes from socket."""
    data = b""
    while len(data) < n:
        chunk = sock.recv(n - len(data))
        if not chunk:
            raise ConnectionError("Connection closed by remote host")
        data += chunk
    return data


def records(read: Callable[[int], bytes]) -> Iterator[tuple[str, int, bytes]]:
    """The bridge's blocks: direction, the ESP's µs when the frame ended, the frame"""
    while True:
        header = read(7)
        if not header:
            return
        if len(header) < 7:
            raise ConnectionError("Capture ends inside a block")
        time = int.from_bytes(header[1:5], "big")
        length = int.from_bytes(header[5:7], "big")
        frame = read(length)
        if len(frame) < length:
            raise ConnectionError("Capture ends inside a block")
        yield chr(header[0]), time, frame


def show(blocks: Iterator[tuple[str, int, bytes]], args: argparse.Namespace):
    """Print the blocks as the options say"""
    frame_parser = ModbusFrameParser()
    # Create coil tracker if needed
    coil_tracker = CoilTracker() if args.coil_changes else None
    clock = BusClock()
    try:
        for direction, time, frame in blocks:
            # Every frame counts for the gaps, those not shown too
            start, gap = clock.frame(time, len(frame))

            # Determine direction
            if direction == "T":
                if args.no_tx:
                    continue
                dir_label = "TX (ESP->UART)"
                color = "\033[94m"  # Blue
            elif direction == "R":
                if args.no_rx:
                    continue
                dir_label = "RX (UART->ESP)"
                color = "\033[92m"  # Green
            else:
                dir_label = f"?? (0x{ord(direction):02X})"
                color = "\033[91m"  # Red

            # When it started on the bus, s from the first frame, and how long the line was quiet before it
            head = f"{color}[{start:12.6f}] {dir_label}"
            if args.raw:
                head += f" ({len(frame)} bytes)"
            if gap is not None:
                head += f"  {gap:+.3f} ms"

            if args.raw:
                print(f"{head}\033[0m")
                print(f"  HEX:   {format_hex(frame)}")
                print()
                continue

            decoded, coil_states = frame_parser.decode_frame(frame, args.verbose)

            if not coil_tracker:
                print(f"{head}\033[0m")
                print(f"  {decoded}")
                if not args.verbose:
                    print(f"  RAW:   {format_hex(frame)}")
                print()
                continue

            # Only frames that change a coil
            changes = coil_tracker.update(coil_states) if coil_states else []
            if changes:
                print(f"{head}\033[0m")
                print(f"  {decoded}")
                print("  \033[93mCOIL CHANGES:\033[0m", end="")
                for (server, coil), old_state, new_state in changes:
                    old_str = "ON" if old_state else "OFF"
                    new_str = "ON" if new_state else "OFF"
                    print(
                        f" [ID:{server:02X} 0x{coil:04X}: {old_str}->{new_str}]", end=""
                    )
                print()
                if not args.verbose:
                    print(f"  RAW:   {format_hex(frame)}")
                print()

    except KeyboardInterrupt:
        print("\n\nStopped by user.")
    except ConnectionError as e:
        print(f"\n\nERROR: {e}")


def main():
    parser = argparse.ArgumentParser(
        description="Monitor UART traffic via TCP bridge with Modbus decoding"
    )
    source = parser.add_mutually_exclusive_group(required=True)
    source.add_argument("--host", help="ESP IP address")
    source.add_argument("--file", help="Read a capture saved with --save instead")
    parser.add_argument(
        "--port", type=int, default=502, help="TCP bridge port (default: 502)"
    )
    parser.add_argument(
        "--no-tx", action="store_true", help="Don't show TX frames (ESP->UART)"
    )
    parser.add_argument(
        "--no-rx", action="store_true", help="Don't show RX frames (UART->ESP)"
    )
    parser.add_argument(
        "--raw", action="store_true", help="Show raw hex instead of decoded Modbus"
    )
    parser.add_argument(
        "--verbose",
        action="store_true",
        help="Show verbose output (all coils, all registers)",
    )
    parser.add_argument(
        "--coil-changes", action="store_true", help="Only show when coils change state"
    )
    parser.add_argument(
        "--save", help="Also save what the ESP sends to this file, for --file"
    )

    args = parser.parse_args()

    if args.file:
        with open(args.file, "rb") as f:
            show(records(f.read), args)
        return

    print(f"Connecting to {args.host}:{args.port}...")
    try:
        sock = socket.create_connection((args.host, args.port), timeout=5.0)
        # Only connecting times out, the bus can be quiet for longer while the CX60 restarts
        sock.settimeout(None)
        print("Connected! Monitoring UART traffic...\n")
    except OSError as e:
        print(f"ERROR: Could not connect: {e}")
        sys.exit(1)

    with sock, open(args.save, "wb") if args.save else nullcontext() as save:

        def read(n: int) -> bytes:
            data = recv_exact(sock, n)
            if save:
                save.write(data)
            return data

        show(records(read), args)
    print("Connection closed.")


if __name__ == "__main__":
    main()
