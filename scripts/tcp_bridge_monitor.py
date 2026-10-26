"""
TCP Bridge Monitor for Flexit Modbus Server
Connects to the ESP TCP bridge and displays UART traffic with Modbus frame decoding.
"""

import argparse
import socket
import sys
from datetime import datetime

# (server address, coil number)
Coil = tuple[int, int]


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


def recv_exact(sock: socket.socket, n: int) -> bytes:
    """Receive exactly n bytes from socket."""
    data = b""
    while len(data) < n:
        chunk = sock.recv(n - len(data))
        if not chunk:
            raise ConnectionError("Connection closed by remote host")
        data += chunk
    return data


def main():
    parser = argparse.ArgumentParser(
        description="Monitor UART traffic via TCP bridge with Modbus decoding"
    )
    parser.add_argument("--host", required=True, help="ESP IP address")
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

    args = parser.parse_args()

    frame_parser = ModbusFrameParser()
    # Create coil tracker if needed
    coil_tracker = CoilTracker() if args.coil_changes else None

    # Connect to ESP
    print(f"Connecting to {args.host}:{args.port}...")
    try:
        sock = socket.create_connection((args.host, args.port), timeout=5.0)
        print("Connected! Monitoring UART traffic...\n")
    except OSError as e:
        print(f"ERROR: Could not connect: {e}")
        sys.exit(1)

    try:
        while True:
            # Read 3-byte header
            header = recv_exact(sock, 3)
            direction = chr(header[0])
            length = (header[1] << 8) | header[2]

            # Read payload (complete Modbus frame from ESP)
            frame = recv_exact(sock, length)

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

            timestamp = datetime.now().strftime("%H:%M:%S.%f")[:-3]

            if args.raw:
                hex_str = format_hex(frame)
                print(f"{color}[{timestamp}] {dir_label} ({len(frame)} bytes)\033[0m")
                print(f"  HEX:   {hex_str}")
                print()
                continue

            decoded, coil_states = frame_parser.decode_frame(frame, args.verbose)

            if not coil_tracker:
                print(f"{color}[{timestamp}] {dir_label}\033[0m")
                print(f"  {decoded}")
                if not args.verbose:
                    print(f"  RAW:   {format_hex(frame)}")
                print()
                continue

            # Only frames that change a coil
            changes = coil_tracker.update(coil_states) if coil_states else []
            if changes:
                print(f"{color}[{timestamp}] {dir_label}\033[0m")
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
    finally:
        sock.close()
        print("Connection closed.")


if __name__ == "__main__":
    main()
