# TCP bridge

The TCP bridge lets you watch the Modbus traffic over a TCP connection. This is useful for finding new registers.

## How it works

When enabled, the TCP bridge creates a server that:
- Accepts TCP connections on the configured port (default: 502)
- Mirrors all UART data to connected clients in real time (both TX and RX)
- Sends each frame with its direction and the ESP's time, so you can tell sent and received frames apart and see
  the gaps between them
- Never waits for a client, so it can't hold up the bus. A client that reads too slowly misses data instead

## Frame protocol

Data is sent to TCP clients using a 7-byte header + payload format:
```
[Direction (1 byte)][Time (4 bytes)][Length (2 bytes)][Payload (N bytes)]
```
- **Direction**: `'T'` (0x54) for TX (sent by the ESP), `'R'` (0x52) for RX (received from the bus)
- **Time**: the ESP's `micros()` when the frame ended, 32-bit big-endian. It wraps every 71 minutes, so only the
  differences between blocks count.
- **Length**: 16-bit big-endian payload length

Each block is one frame. Bytes from the bus that aren't part of a valid frame come as an RX block of their own.

How exact the time is:
- **TX**: when the UART had sent the frame's last byte.
- **RX**: the latest the frame can have ended: when the ESP read it, less a byte time (87 µs at 115200 baud) for
  each byte read after it. That's late by as long as the UART held the bytes, up to 2 byte times or its RX
  threshold's worth, and by how long the loop took to get to them.

## Options

| Option | Type | Default | Description |
|--------|------|---------|-------------|
| `tcp_bridge_enabled` | boolean | `false` | Enable/disable TCP bridge |
| `tcp_bridge_port` | integer | `502` | TCP port to listen on |
| `tcp_bridge_max_clients` | integer | `4` | Maximum concurrent clients (1-10) |

## Example

```yaml
flexit_modbus_server:
  - id: !extend server
    tcp_bridge_enabled: true
    tcp_bridge_port: 8502
    tcp_bridge_max_clients: 2
```

## Monitoring tool

A Python script for monitoring and decoding the TCP bridge traffic is included in
[scripts/tcp_bridge_monitor.py](../scripts/tcp_bridge_monitor.py).

**Features:**
- Decodes Modbus RTU frames, writes (0x10) and the CX60's 0x64 included
- Color-coded TX (sent by the ESP) and RX (received) traffic
- When each frame started on the bus, in s from the first one, and how long the line was quiet before it. A
  negative gap means the frame was on the line before the one above it had ended, like bytes heard while the ESP
  was sending.
- Track coil changes per server and address with `--coil-changes`
- Save what the ESP sends with `--save`, and show it again later with `--file`

```bash
python3 scripts/tcp_bridge_monitor.py --host 192.168.1.50 --save capture.bin
python3 scripts/tcp_bridge_monitor.py --file capture.bin --raw
```
