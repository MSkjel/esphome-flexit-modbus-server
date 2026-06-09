#!/usr/bin/env python3
"""
Publish Flexit CU60 / CI600 sensor data (read via an Elfin EW11 RS485->WiFi
adapter) to MQTT, with Home Assistant auto-discovery.

It reuses the framing/decoding logic from ew11_flexit_decoder.py: connect to the
EW11's transparent TCP port, reframe the gapless CS60 Modbus stream by function
code + CRC, and track the holding registers the CS60 writes (0x10/0x06/0x65).
The latest values are published as a single JSON state topic; HA discovery
configs point each entity at a field of that JSON.

Credentials: pass the broker password via the MQTT_PASSWORD env var (preferred,
keeps it out of shell history and the repo) or --password.

Usage:
    export MQTT_PASSWORD='...'
    .venv/bin/python scripts/ew11_flexit_mqtt.py \
        --ew11-host 172.16.36.5 --ew11-port 8899 \
        --mqtt-host 172.16.36.48 --mqtt-user mqtt_user

    # one-shot connectivity/discovery test, no EW11 needed:
    .venv/bin/python scripts/ew11_flexit_mqtt.py --mqtt-host 172.16.36.48 \
        --mqtt-user mqtt_user --dry-run
"""

import argparse
import json
import os
import socket
import sys
import time

import paho.mqtt.client as mqtt

# Reuse the verified decoder primitives from the sibling module.
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import ew11_flexit_decoder as dec  # noqa: E402

# ---------------------------------------------------------------------------
# Entity definitions. Each maps a holding-register address to an HA entity.
#   kind: temp -> signed/10 °C | pct -> % | mode -> enum string | heater -> ON/OFF
# Unconnected sensors on this unit (0xC4 extract, 0xC6 return water) are omitted
# on purpose -- they read garbage. Add them back if your unit has those sensors.
# ---------------------------------------------------------------------------
ENTITIES = [
    # key,            addr,  name,                      kind,   component,        device_class, unit
    ("setpoint",      0xBE, "Temperature setpoint",     "setpoint", "sensor",     "temperature", "°C"),
    ("inside_air",    0xC3, "Inside air temperature",   "temp", "sensor",         "temperature", "°C"),
    ("outside_air",   0xC5, "Outside air temperature",  "temp", "sensor",         "temperature", "°C"),
    ("cooling",       0xC7, "Cooling",                  "pct",  "sensor",         None,          "%"),
    ("heat_exchanger",0xC8, "Heat exchanger",           "pct",  "sensor",         None,          "%"),
    ("heating",       0xC9, "Heating",                  "pct",  "sensor",         None,          "%"),
    ("supply_fan",    0xCA, "Supply fan",               "pct",  "sensor",         None,          "%"),
    ("mode",          0xBF, "Mode",                     "mode", "sensor",         None,          None),
    ("heater",        0x10E,"Heater",                   "heater","binary_sensor", "running",     None),
    ("filter_alarm",  0x10C,"Filter change alarm",      "alarm","binary_sensor",  "problem",     None),
    # Runtime counters: CS60 stores seconds across a high/low register pair (the
    # `addr` here is the HIGH word); we report hours = (high<<16|low)/3600.
    # Empirically verified (2026-06-09) by stepping the fan through each level:
    # exactly the active level's counter ticks +1/sec, while `runtime` and
    # `filter` tick every second in every mode. The per-level counters are
    # lifetime totals; `runtime` is time since the last reset/power-on (so it can
    # be smaller than a per-level counter -- it is NOT a grand total).
    ("runtime",        0x15C, "Runtime (since reset)",  "hours", "sensor", "duration", "h"),
    ("runtime_filter", 0x15E, "Runtime since filter",   "hours", "sensor", "duration", "h"),
    ("runtime_stop",   0x14C, "Runtime at Stop",        "hours", "sensor", "duration", "h"),
    ("runtime_min",    0x14E, "Runtime at Min",         "hours", "sensor", "duration", "h"),
    ("runtime_normal", 0x150, "Runtime at Normal",      "hours", "sensor", "duration", "h"),
    ("runtime_max",    0x152, "Runtime at High (Max)",  "hours", "sensor", "duration", "h"),
    ("runtime_heater", 0x156, "Runtime heater",         "hours", "sensor", "duration", "h"),
    ("runtime_rotor",  0x154, "Runtime rotor",          "hours", "sensor", "duration", "h"),
]

# Discovery keys we previously published but have since renamed/removed; publish
# an empty config to delete the orphaned HA entity on startup.
REMOVED_KEYS = [("sensor", "runtime_total")]

DEVICE = {
    "identifiers": ["flexit_cs60_ew11"],
    "name": "Flexit",
    "manufacturer": "Flexit",
    "model": "CU60 / CI600 (via EW11)",
}

BASE = "flexit"
STATE_TOPIC = f"{BASE}/state"
AVAIL_TOPIC = f"{BASE}/availability"


def register_to_field(addr: int, kind: str, table: "dec.RegisterTable"):
    """Convert register(s) to the JSON field value for an entity."""
    if kind == "hours":
        # 32-bit seconds across (high=addr, low=addr+1). Missing high word
        # defaults to 0, which is correct until the unit passes ~18.2 h.
        hi = table.values.get(addr)
        lo = table.values.get(addr + 1)
        if hi is None and lo is None:
            return None
        return round((((hi or 0) << 16) | (lo or 0)) / 3600.0, 1)

    raw = table.values.get(addr)
    if raw is None:
        return None
    if kind == "temp":
        return round(dec.signed16(raw) / 10.0, 1)
    if kind == "setpoint":
        # Stored as °C x10 but only settable in whole degrees -> report an int.
        return int(round(dec.signed16(raw) / 10.0))
    if kind == "pct":
        return raw
    if kind == "mode":
        return dec.MODE_STRINGS[raw] if raw < len(dec.MODE_STRINGS) else f"invalid({raw})"
    if kind in ("heater", "alarm"):
        return "ON" if raw else "OFF"
    return raw


def build_state(table: "dec.RegisterTable") -> dict:
    out = {}
    for key, addr, _n, kind, *_ in ENTITIES:
        out[key] = register_to_field(addr, kind, table)
    return out


# Compact unit suffix per entity, for the console status line.
_UNIT = {key: (unit or "") for key, _a, _n, _k, _c, _dc, unit in ENTITIES}


def format_status(state: dict) -> str:
    """One-line human-readable summary of all sensor values for the console."""
    ts = time.strftime("%H:%M:%S")
    parts = []
    for key, _addr, name, *_ in ENTITIES:
        val = state.get(key)
        val = "--" if val is None else val
        parts.append(f"{name}={val}{_UNIT.get(key, '')}")
    return f"[{ts}] " + " | ".join(parts)


def publish_discovery(client: mqtt.Client):
    """Publish HA MQTT discovery configs (retained) for every entity."""
    for key, _addr, name, kind, component, device_class, unit in ENTITIES:
        uid = f"flexit_cs60_{key}"
        cfg = {
            "name": name,
            "unique_id": uid,
            "object_id": f"flexit_{key}",
            "state_topic": STATE_TOPIC,
            "value_template": f"{{{{ value_json.{key} }}}}",
            "availability_topic": AVAIL_TOPIC,
            "device": DEVICE,
        }
        if device_class:
            cfg["device_class"] = device_class
        if unit:
            cfg["unit_of_measurement"] = unit
        if component == "sensor" and unit:
            cfg["state_class"] = "total_increasing" if device_class == "duration" else "measurement"
        if component == "binary_sensor":
            cfg["payload_on"] = "ON"
            cfg["payload_off"] = "OFF"
        topic = f"homeassistant/{component}/flexit_cs60/{key}/config"
        client.publish(topic, json.dumps(cfg), qos=1, retain=True)
    # Remove discovery for any renamed/retired entities.
    for component, key in REMOVED_KEYS:
        client.publish(f"homeassistant/{component}/flexit_cs60/{key}/config", "", qos=1, retain=True)
    print(f"Published {len(ENTITIES)} discovery configs under homeassistant/"
          f" (+{len(REMOVED_KEYS)} removed)")


def make_client(args) -> mqtt.Client:
    # paho-mqtt 2.x requires an explicit callback API version.
    try:
        client = mqtt.Client(mqtt.CallbackAPIVersion.VERSION2, client_id="flexit-ew11-bridge")
    except (AttributeError, TypeError):
        client = mqtt.Client(client_id="flexit-ew11-bridge")  # paho 1.x fallback

    password = args.password or os.environ.get("MQTT_PASSWORD")
    if args.mqtt_user:
        if not password:
            print("ERROR: no MQTT password. Set MQTT_PASSWORD or pass --password.")
            sys.exit(2)
        client.username_pw_set(args.mqtt_user, password)

    client.will_set(AVAIL_TOPIC, "offline", qos=1, retain=True)
    client.connect(args.mqtt_host, args.mqtt_port, keepalive=60)
    client.loop_start()
    client.publish(AVAIL_TOPIC, "online", qos=1, retain=True)
    return client


def run(args):
    client = make_client(args)
    publish_discovery(client)
    print(f"Connected to MQTT {args.mqtt_host}:{args.mqtt_port}")

    if args.dry_run:
        # Publish one synthetic state so HA entities populate, then exit.
        demo = {key: None for key, *_ in ENTITIES}
        client.publish(STATE_TOPIC, json.dumps(demo), qos=1, retain=True)
        print("Dry run: discovery + empty state published. Exiting.")
        time.sleep(1)
        client.loop_stop()
        client.disconnect()
        return

    print(f"Reading EW11 {args.ew11_host}:{args.ew11_port}, publishing every {args.interval}s")
    table = dec.RegisterTable()
    sock = socket.create_connection((args.ew11_host, args.ew11_port), timeout=10.0)
    sock.settimeout(1.0)
    buf = bytearray()
    last_pub = 0.0

    try:
        while True:
            try:
                chunk = sock.recv(4096)
                if not chunk:
                    print("EW11 closed connection; reconnecting...")
                    sock.close()
                    time.sleep(2)
                    sock = socket.create_connection((args.ew11_host, args.ew11_port), timeout=10.0)
                    sock.settimeout(1.0)
                    buf.clear()
                    continue
                buf.extend(chunk)
            except socket.timeout:
                pass

            # Reframe + update register table (same walk as the decoder).
            offset = 0
            while len(buf) - offset >= dec.MIN_FRAME_LENGTH:
                view = bytes(buf[offset:])
                length = dec.expected_frame_length(view)
                if length == 0:
                    offset += 1
                    continue
                if length > len(view):
                    break
                frame = view[:length]
                if not dec.crc_ok(frame):
                    offset += 1
                    continue
                fn = frame[1]
                if fn in (0x06, 0x65):
                    table.set((frame[2] << 8) | frame[3], (frame[4] << 8) | frame[5])
                elif fn == 0x10:
                    start = (frame[2] << 8) | frame[3]
                    count = (frame[4] << 8) | frame[5]
                    bc = frame[6]
                    data = frame[7:7 + bc]
                    for i in range(count):
                        if 2 * i + 1 < len(data):
                            table.set(start + i, (data[2 * i] << 8) | data[2 * i + 1])
                offset += length
            del buf[:offset]
            if len(buf) > 4 * dec.MAX_FRAME_LENGTH:
                del buf[:-dec.MAX_FRAME_LENGTH]

            now = time.monotonic()
            if now - last_pub >= args.interval and table.values:
                state = build_state(table)
                client.publish(STATE_TOPIC, json.dumps(state), qos=0, retain=True)
                last_pub = now
                print(format_status(state), flush=True)
    finally:
        client.publish(AVAIL_TOPIC, "offline", qos=1, retain=True)
        client.loop_stop()
        client.disconnect()
        sock.close()


def main():
    p = argparse.ArgumentParser(description=__doc__,
                                formatter_class=argparse.RawDescriptionHelpFormatter)
    p.add_argument("--ew11-host", default="172.16.36.5")
    p.add_argument("--ew11-port", type=int, default=8899)
    p.add_argument("--mqtt-host", default="172.16.36.48")
    p.add_argument("--mqtt-port", type=int, default=1883)
    p.add_argument("--mqtt-user", default="mqtt_user")
    p.add_argument("--password", default=None, help="MQTT password (else MQTT_PASSWORD env)")
    p.add_argument("--interval", type=float, default=10.0, help="Publish interval seconds")
    p.add_argument("--dry-run", action="store_true", help="Publish discovery + empty state, then exit")
    p.add_argument("--verbose", action="store_true")
    args = p.parse_args()

    try:
        run(args)
    except KeyboardInterrupt:
        print("\nStopped.")
    except (ConnectionError, OSError) as e:
        print(f"ERROR: {e}")
        sys.exit(1)


if __name__ == "__main__":
    main()
