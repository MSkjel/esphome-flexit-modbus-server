import logging
import os
import re
from pathlib import Path

import esphome.codegen as cg
import esphome.config_validation as cv
import esphome.final_validate as fv
from esphome.const import (
    CONF_ADDRESS,
    CONF_ID,
    CONF_INVERTED,
    CONF_UART_ID,
    PLATFORM_ESP32,
    PLATFORM_ESP8266,
)
from esphome.core import CORE
from esphome.helpers import copy_file_if_changed, fnv1_hash

from esphome import pins
from esphome.components import uart

CONF_TX_ENABLE_PIN = "tx_enable_pin"
CONF_TX_ENABLE_DIRECT = "tx_enable_direct"
CONF_PERSIST_SETTINGS = "persist_settings"
CONF_PERSIST_RUNTIME_COUNTERS = "persist_runtime_counters"
CONF_TCP_BRIDGE_ENABLED = "tcp_bridge_enabled"
CONF_MODBUS_LIBRARY_REF = "modbus_library_ref"
CONF_TCP_BRIDGE_PORT = "tcp_bridge_port"
CONF_TCP_BRIDGE_MAX_CLIENTS = "tcp_bridge_max_clients"
CONF_RESTORE_FROM_FLASH = "restore_from_flash"  # esp8266's

flexit_modbus_server_ns = cg.esphome_ns.namespace("flexit_modbus_server")
FlexitModbusServer = flexit_modbus_server_ns.class_("FlexitModbusServer", cg.Component)

DEPENDENCIES = ["uart"]
AUTO_LOAD = ["socket"]  # the TCP bridge

LIBRARY_REPOSITORY = "https://github.com/MSkjel/ESP-ModbusRTUServer.git"

# The bus code the panel firmware shares, which this repo ships for the panel packages
PBUS_DIR = Path(__file__).resolve().parents[3] / "packages" / "code" / "panel"

_LOGGER = logging.getLogger(__name__)


def validate_ref(value):
    value = cv.string_strict(value)
    if not re.fullmatch(r"[A-Za-z0-9._/-]+", value):
        raise cv.Invalid(f"'{value}' is not a branch, tag or commit")
    return value


def _check_pbus(config):
    """The component builds the bus code from the repo's packages, so it needs the repo around it."""
    missing = [name for name in ("pbus.h", "pbus.c") if not (PBUS_DIR / name).is_file()]
    if missing:
        raise cv.Invalid(
            f"{', '.join(missing)} not found in {PBUS_DIR}. The component needs the whole "
            "esphome-flexit-modbus-server repo, not just its component directory: point external_components "
            "at the repo."
        )
    return config


def _consume_sockets(config):
    """Tell the socket component how many sockets the TCP bridge needs."""
    if config[CONF_TCP_BRIDGE_ENABLED]:
        from esphome.components import socket

        socket.consume_sockets(
            config[CONF_TCP_BRIDGE_MAX_CLIENTS], "flexit_modbus_server"
        )(config)
        socket.consume_sockets(1, "flexit_modbus_server", socket.SocketType.TCP_LISTEN)(
            config
        )
    return config


CONFIG_SCHEMA = cv.All(
    cv.Schema(
        {
            cv.GenerateID(): cv.declare_id(FlexitModbusServer),
            # The CX60 only polls 1-3, and something answering at 254 sends it to its bootloader
            cv.Required(CONF_ADDRESS): cv.int_range(min=1, max=3),
            # "none" so the pin can be turned off from a substitution
            cv.Optional(CONF_TX_ENABLE_PIN): cv.Any(
                cv.one_of("none", lower=True), pins.internal_gpio_output_pin_schema
            ),
            cv.Optional(CONF_TX_ENABLE_DIRECT, True): cv.boolean,
            # false = never restored from flash at boot and never saved to it
            cv.Optional(CONF_PERSIST_SETTINGS, True): cv.boolean,
            cv.Optional(CONF_PERSIST_RUNTIME_COUNTERS, True): cv.boolean,
            cv.Optional(CONF_TCP_BRIDGE_ENABLED, False): cv.boolean,
            cv.Optional(CONF_TCP_BRIDGE_PORT, 502): cv.port,
            cv.Optional(CONF_TCP_BRIDGE_MAX_CLIENTS, 4): cv.int_range(min=1, max=10),
            # Branch, tag or commit of ESP-ModbusRTUServer. Not used with a local copy.
            cv.Optional(CONF_MODBUS_LIBRARY_REF): validate_ref,
        }
    )
    .extend(uart.UART_DEVICE_SCHEMA)
    .extend(cv.COMPONENT_SCHEMA),
    cv.only_on([PLATFORM_ESP32, PLATFORM_ESP8266]),
    _check_pbus,
    _consume_sockets,
)


# Without restore_from_flash an ESP8266 has 64 flash preference words, given out in setup order with a CRC word
# per record: ours (checked in flexit_modbus_server.h), then runtime WiFi credentials, then an API key from HA.
_FLASH_WORDS = 64
_SETTINGS_WORDS = 47
_RUNTIME_WORDS = 10
_WIFI_WORDS = 26
_API_KEY_WORDS = 9


def _final_validate_flash(config):
    if not CORE.is_esp8266:
        return config
    full_config = fv.full_config.get()
    if full_config.get("esp8266", {}).get(CONF_RESTORE_FROM_FLASH):
        return config
    left = _FLASH_WORDS
    if config[CONF_PERSIST_SETTINGS]:
        left -= _SETTINGS_WORDS
    if config[CONF_PERSIST_RUNTIME_COUNTERS]:
        left -= _RUNTIME_WORDS
    if left >= _WIFI_WORDS:
        left -= _WIFI_WORDS
    else:
        for component in ("captive_portal", "improv_serial"):
            if component in full_config:
                _LOGGER.warning(
                    "WiFi credentials saved through %s aren't kept over a reboot: on an ESP8266 the saved "
                    "settings fill the flash space for them. Put them in the config, set persist_settings "
                    "to false, or turn on restore_from_flash under esp8266.",
                    component,
                )
    encryption = full_config.get("api", {}).get("encryption")
    if encryption is not None and "key" not in encryption and left < _API_KEY_WORDS:
        _LOGGER.warning(
            "An API encryption key sent by Home Assistant can't be saved: on an ESP8266 the saved settings and "
            "runtime counters fill the flash space for it. Put the key in the config, or set "
            "persist_runtime_counters to false."
        )
    return config


# The CX60's bus runs at 115200 baud, and the server both reads and answers
FINAL_VALIDATE_SCHEMA = cv.All(
    uart.final_validate_device_schema(
        "flexit_modbus_server", require_tx=True, require_rx=True, baud_rate=115200
    ),
    _final_validate_flash,
)

MULTI_CONF = True
CODEOWNERS = ["@MSkjel"]


async def to_code(config):
    local_lib_exists = False
    component_dir = os.path.dirname(__file__)

    # ESPHome copies the component's directory into the build, so a copy here gets built with it
    if os.path.exists(
        os.path.join(component_dir, "ModbusRTUServer.h")
    ) or os.path.exists(os.path.join(component_dir, "ModbusRTUServer.cpp")):
        local_lib_exists = True

    if not local_lib_exists:
        # ESPHome leaves out the version for git libraries, so the ref goes on the URL
        repository = LIBRARY_REPOSITORY
        if CONF_MODBUS_LIBRARY_REF in config:
            repository += f"#{config[CONF_MODBUS_LIBRARY_REF]}"
        cg.add_library(name="ModbusRTUServer", repository=repository, version=None)

    if config[CONF_TCP_BRIDGE_ENABLED]:
        cg.add_define("USE_FLEXIT_TCP_BRIDGE")

    # Where the panel package's includes put it too, so a build with both has one copy
    for name in ("pbus.h", "pbus.c"):
        copy_file_if_changed(PBUS_DIR / name, CORE.relative_src_path("panel", name))

    uart_parent = await cg.get_variable(config[CONF_UART_ID])
    server = cg.new_Pvariable(config[CONF_ID])
    cg.add(server.set_uart_parent(uart_parent))
    cg.add(server.set_server_address(config[CONF_ADDRESS]))
    cg.add(server.set_persist_settings(config[CONF_PERSIST_SETTINGS]))
    cg.add(server.set_persist_runtime_counters(config[CONF_PERSIST_RUNTIME_COUNTERS]))
    cg.add(server.set_preference_hash(fnv1_hash(config[CONF_ID].id)))

    direct = config[CONF_TX_ENABLE_DIRECT]
    pin_config = config.get(CONF_TX_ENABLE_PIN, "none")
    if pin_config != "none":
        if CORE.is_esp32:
            # The UART drives the pin as RTS in RS-485 half-duplex mode and lets go right after the last stop
            # bit, the Modbus library doesn't get it. tx_enable_direct false (active low) becomes an inverted pin.
            pin_config = dict(pin_config)
            pin_config[CONF_INVERTED] = bool(pin_config.get(CONF_INVERTED, False)) != (
                not direct
            )
            flow_control_pin = await cg.gpio_pin_expression(pin_config)
            cg.add(uart_parent.set_flow_control_pin(flow_control_pin))
        else:
            # The Modbus library drives the pin, inverted is active low like tx_enable_direct false
            cg.add(server.set_tx_enable_pin(pin_config["number"]))
            direct = direct != bool(pin_config.get(CONF_INVERTED, False))
    cg.add(server.set_tx_enable_direct(direct))

    if config[CONF_TCP_BRIDGE_ENABLED]:
        cg.add(server.set_tcp_bridge_enabled(config[CONF_TCP_BRIDGE_ENABLED]))
        cg.add(server.set_tcp_bridge_port(config[CONF_TCP_BRIDGE_PORT]))
        cg.add(server.set_tcp_bridge_max_clients(config[CONF_TCP_BRIDGE_MAX_CLIENTS]))

    await cg.register_component(server, config)
