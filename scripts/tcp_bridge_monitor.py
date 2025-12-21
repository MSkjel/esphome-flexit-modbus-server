"""
TCP Bridge Monitor for Flexit Modbus Server
Connects to the ESP TCP bridge and displays UART traffic with Modbus frame decoding.
"""

import socket
import sys
import argparse
from datetime import datetime
from typing import Dict, Optional


class ModbusFrameParser:
    """Parse and decode Modbus RTU frames."""

    # Modbus function code names
    FUNCTION_CODES = {
        0x01: "Read Coils",
        0x02: "Read Discrete Inputs",
        0x03: "Read Holding Registers",
        0x04: "Read Input Registers",
        0x05: "Write Single Coil",
        0x06: "Write Single Register",
        0x0F: "Write Multiple Coils",
        0x10: "Write Multiple Registers",
        0x65: "Custom Read Register",
    }

    @staticmethod
    def calc_crc(data: bytes) -> int:
        """Calculate Modbus RTU CRC16."""
        crc = 0xFFFF
        for byte in data:
            crc ^= byte
            for _ in range(8):
                if crc & 0x0001:
                    crc = (crc >> 1) ^ 0xA001
                else:
                    crc >>= 1
        return crc

    @staticmethod
    def decode_frame(frame: bytes, verbose: bool = False) -> tuple[str, Optional[Dict[int, bool]]]:
        """
        Decode Modbus frame into human-readable format.
        Returns: (decoded_string, coil_states_dict or None)
        """
        if len(frame) < 4:
            return "Invalid frame", None

        slave_id = frame[0]
        function_code = frame[1]
        fn_name = ModbusFrameParser.FUNCTION_CODES.get(function_code, f"Unknown (0x{function_code:02X})")

        # Custom 0x65 function
        if function_code == 0x65:
            addr = (frame[2] << 8) | frame[3]
            value = (frame[4] << 8) | frame[5]
            value_signed = value if value < 32768 else value - 65536
            if verbose:
                return f"ID:{slave_id:02X} {fn_name} Addr:0x{addr:04X} Val:{value_signed}", None
            return f"ID:{slave_id:02X} {fn_name} Addr:0x{addr:04X} Val:{value_signed}", None

        # Read Holding (0x03) or Input Registers (0x04)
        if function_code in [0x03, 0x04]:
            # Check if this is a request (8 bytes) or response (variable length)
            if len(frame) == 8:
                # Request: Slave + Fn + StartAddr(2) + Quantity(2) + CRC(2)
                start_addr = (frame[2] << 8) | frame[3]
                quantity = (frame[4] << 8) | frame[5]
                return f"ID:{slave_id:02X} {fn_name} Request Addr:0x{start_addr:04X} Qty:{quantity}", None
            elif len(frame) >= 3:
                # Response: Slave + Fn + ByteCount + Data + CRC(2)
                byte_count = frame[2]
                data = frame[3:3+byte_count]
                # Parse as registers (16-bit values)
                registers = []
                limit = byte_count if verbose else min(byte_count, 10)
                for i in range(0, limit, 2):
                    if i+1 < byte_count:
                        reg_val = (data[i] << 8) | data[i+1]
                        reg_val_signed = reg_val if reg_val < 32768 else reg_val - 65536
                        registers.append(f"{reg_val_signed}")

                more = "..." if byte_count > 10 and not verbose else ""
                return f"ID:{slave_id:02X} {fn_name} Response Regs:[{', '.join(registers)}{more}]", None

        # Write Single Register (0x06)
        if function_code == 0x06:
            addr = (frame[2] << 8) | frame[3]
            value = (frame[4] << 8) | frame[5]
            value_signed = value if value < 32768 else value - 65536
            return f"ID:{slave_id:02X} {fn_name} Addr:0x{addr:04X} Val:{value_signed}", None

        # Read Coils (0x01)
        if function_code == 0x01:
            # Check if this is a request (8 bytes) or response (variable length)
            if len(frame) == 8:
                # Request: Slave + Fn + StartAddr(2) + Quantity(2) + CRC(2)
                start_addr = (frame[2] << 8) | frame[3]
                quantity = (frame[4] << 8) | frame[5]
                return f"ID:{slave_id:02X} {fn_name} Request Addr:{start_addr} Qty:{quantity}", None
            elif len(frame) >= 3:
                # Response: Slave + Fn + ByteCount + Data + CRC(2)
                byte_count = frame[2]
                coil_data = frame[3:3+byte_count]

                # Parse coil states
                coil_states = {}
                for byte_idx, byte_val in enumerate(coil_data):
                    for bit_idx in range(8):
                        coil_num = byte_idx * 8 + bit_idx
                        coil_states[coil_num] = bool(byte_val & (1 << bit_idx))

                # Create summary
                num_coils = len(coil_states)
                on_count = sum(coil_states.values())

                if verbose:
                    # Show all coils
                    coil_str = ", ".join([f"{i}:{'ON' if v else 'OFF'}" for i, v in coil_states.items()])
                    return f"ID:{slave_id:02X} {fn_name} Response ({on_count}/{num_coils} ON) [{coil_str}]", coil_states
                else:
                    return f"ID:{slave_id:02X} {fn_name} Response ({on_count}/{num_coils} ON)", coil_states

        # Write Single Coil (0x05)
        if function_code == 0x05:
            addr = (frame[2] << 8) | frame[3]
            value = (frame[4] << 8) | frame[5]
            state = "ON" if value == 0xFF00 else "OFF"
            coil_states = {addr: value == 0xFF00}
            return f"ID:{slave_id:02X} {fn_name} Addr:{addr} State:{state}", coil_states

        # Generic
        return f"ID:{slave_id:02X} {fn_name} Len:{len(frame)}", None


class CoilTracker:
    """Track coil state changes."""

    def __init__(self):
        self.coil_states: Dict[int, bool] = {}

    def update(self, new_states: Dict[int, bool]) -> list[tuple[int, bool, bool]]:
        """
        Update coil states and return list of changes.
        Returns: list of (coil_num, old_state, new_state) for changed coils
        """
        changes = []
        for coil_num, new_state in new_states.items():
            old_state = self.coil_states.get(coil_num)
            if old_state is not None and old_state != new_state:
                changes.append((coil_num, old_state, new_state))
            self.coil_states[coil_num] = new_state
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
    parser.add_argument(
        "--host",
        required=True,
        help="ESP IP address"
    )
    parser.add_argument(
        "--port",
        type=int,
        default=502,
        help="TCP bridge port (default: 502)"
    )
    parser.add_argument(
        "--no-tx",
        action="store_true",
        help="Don't show TX frames (ESP→UART)"
    )
    parser.add_argument(
        "--no-rx",
        action="store_true",
        help="Don't show RX frames (UART→ESP)"
    )
    parser.add_argument(
        "--raw",
        action="store_true",
        help="Show raw hex instead of decoded Modbus"
    )
    parser.add_argument(
        "--verbose",
        action="store_true",
        help="Show verbose output (all coils, all registers)"
    )
    parser.add_argument(
        "--coil-changes",
        action="store_true",
        help="Only show when coils change state"
    )

    args = parser.parse_args()

    # Create coil tracker if needed
    coil_tracker = CoilTracker() if args.coil_changes else None

    # Connect to ESP
    print(f"Connecting to {args.host}:{args.port}...")
    try:
        sock = socket.create_connection((args.host, args.port), timeout=5.0)
        print(f"Connected! Monitoring UART traffic...\n")
    except Exception as e:
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
            if direction == 'T':
                if args.no_tx:
                    continue
                dir_label = "TX (ESP→UART)"
                color = "\033[94m"  # Blue
            elif direction == 'R':
                if args.no_rx:
                    continue
                dir_label = "RX (UART→ESP)"
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
            else:
                decoded, coil_states = ModbusFrameParser.decode_frame(frame, args.verbose)

                # Handle coil change tracking
                if coil_tracker and coil_states:
                    changes = coil_tracker.update(coil_states)
                    if changes:
                        # Print frame with changes highlighted
                        print(f"{color}[{timestamp}] {dir_label}\033[0m")
                        print(f"  {decoded}")
                        print(f"  \033[93mCOIL CHANGES:\033[0m", end="")
                        for coil_num, old_state, new_state in changes:
                            old_str = "ON" if old_state else "OFF"
                            new_str = "ON" if new_state else "OFF"
                            print(f" [{coil_num}: {old_str}→{new_str}]", end="")
                        print()
                        if not args.verbose:
                            print(f"  RAW:   {format_hex(frame)}")
                        print()
                    # else: skip frames with no changes when --coil-changes is enabled
                elif not coil_tracker:
                    # Normal output (no coil tracking)
                    print(f"{color}[{timestamp}] {dir_label}\033[0m")
                    print(f"  {decoded}")
                    if not args.verbose:
                        print(f"  RAW:   {format_hex(frame)}")
                    print()

    except KeyboardInterrupt:
        print("\n\nStopped by user.")
    except ConnectionError as e:
        print(f"\n\nERROR: {e}")
    except Exception as e:
        print(f"\n\nERROR: Unexpected error: {e}")
        import traceback
        traceback.print_exc()
    finally:
        sock.close()
        print("Connection closed.")


if __name__ == "__main__":
    main()
