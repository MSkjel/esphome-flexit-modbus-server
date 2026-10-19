import logging
import os
import re

import esphome.codegen as cg
import esphome.config_validation as cv
import esphome.final_validate as fv
from esphome.const import CONF_ADDRESS, CONF_ID, CONF_INVERTED, CONF_UART_ID
from esphome.core import CORE

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
FlexitModbusDeviceComponent = flexit_modbus_server_ns.class_(
    "FlexitModbusServer", cg.Component
)

DEPENDENCIES = ["uart"]
AUTO_LOAD = ["socket"]  # the TCP bridge

LIBRARY_REPOSITORY = "https://github.com/MSkjel/ESP-ModbusRTUServer.git"

_LOGGER = logging.getLogger(__name__)


def validate_ref(value):
    value = cv.string_strict(value)
    if not re.fullmatch(r"[A-Za-z0-9._/-]+", value):
        raise cv.Invalid(f"'{value}' is not a branch, tag or commit")
    return value


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
            cv.GenerateID(): cv.declare_id(FlexitModbusDeviceComponent),
            cv.Required(CONF_ADDRESS): cv.positive_int,
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
    _consume_sockets,
)


def _final_validate(config):
    # An ESP8266 keeps flash preferences in 64 words without restore_from_flash. The saved settings and runtime
    # counters take 57, the 26 for WiFi credentials don't fit after them.
    if not CORE.is_esp8266 or not (
        config[CONF_PERSIST_SETTINGS] or config[CONF_PERSIST_RUNTIME_COUNTERS]
    ):
        return
    full_config = fv.full_config.get()
    if full_config.get("esp8266", {}).get(CONF_RESTORE_FROM_FLASH):
        return
    for component in ("captive_portal", "improv_serial"):
        if component in full_config:
            _LOGGER.warning(
                "WiFi credentials saved through %s aren't kept over a reboot: on an ESP8266 the saved "
                "settings and runtime counters fill the flash space for them. Put them in the config, "
                "or set persist_settings and persist_runtime_counters to false.",
                component,
            )


FINAL_VALIDATE_SCHEMA = _final_validate

MULTI_CONF = True
CODEOWNERS = ["@MSkjel"]


async def to_code(config):
    local_lib_exists = False
    component_dir = os.path.dirname(__file__)

    if os.path.exists(
        os.path.join(component_dir, "ModbusRTUServer.h")
    ) or os.path.exists(os.path.join(component_dir, "ModbusRTUServer.cpp")):
        local_lib_exists = True

    if not local_lib_exists:
        possible_paths = [
            os.path.join(component_dir, "..", "..", "..", "ModbusRTUServer"),
            os.path.join(component_dir, "..", "..", "ModbusRTUServer"),
            os.path.join(os.getcwd(), "ModbusRTUServer"),
        ]
        for path in possible_paths:
            if os.path.exists(path):
                local_lib_exists = True
                break

    if not local_lib_exists:
        # ESPHome leaves out the version for git libraries, so the ref goes on the URL
        repository = LIBRARY_REPOSITORY
        if CONF_MODBUS_LIBRARY_REF in config:
            repository += f"#{config[CONF_MODBUS_LIBRARY_REF]}"
        cg.add_library(name="ModbusRTUServer", repository=repository, version=None)

    if config[CONF_TCP_BRIDGE_ENABLED]:
        cg.add_define("USE_FLEXIT_TCP_BRIDGE")

    uart_parent = await cg.get_variable(config[CONF_UART_ID])
    server = cg.new_Pvariable(config[CONF_ID])
    cg.add(server.set_uart_parent(uart_parent))
    cg.add(server.set_server_address(config[CONF_ADDRESS]))
    cg.add(server.set_tx_enable_direct(config[CONF_TX_ENABLE_DIRECT]))
    cg.add(server.set_persist_settings(config[CONF_PERSIST_SETTINGS]))
    cg.add(server.set_persist_runtime_counters(config[CONF_PERSIST_RUNTIME_COUNTERS]))

    pin_config = config.get(CONF_TX_ENABLE_PIN, "none")
    if pin_config != "none":
        if CORE.is_esp32:
            # The UART drives the pin as RTS in RS-485 half-duplex mode and lets go right after the last stop
            # bit, the Modbus library doesn't get it. tx_enable_direct false (active low) becomes an inverted pin.
            pin_config = dict(pin_config)
            pin_config[CONF_INVERTED] = bool(pin_config.get(CONF_INVERTED, False)) != (
                not config[CONF_TX_ENABLE_DIRECT]
            )
            flow_control_pin = await cg.gpio_pin_expression(pin_config)
            cg.add(uart_parent.set_flow_control_pin(flow_control_pin))
        else:
            cg.add(server.set_tx_enable_pin(pin_config["number"]))

    if config[CONF_TCP_BRIDGE_ENABLED]:
        cg.add(server.set_tcp_bridge_enabled(config[CONF_TCP_BRIDGE_ENABLED]))
        cg.add(server.set_tcp_bridge_port(config[CONF_TCP_BRIDGE_PORT]))
        cg.add(server.set_tcp_bridge_max_clients(config[CONF_TCP_BRIDGE_MAX_CLIENTS]))

    await cg.register_component(server, config)
