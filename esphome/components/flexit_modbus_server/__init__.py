import esphome.codegen as cg
import esphome.config_validation as cv
from esphome.components import uart
from esphome.const import CONF_ADDRESS, CONF_ID
from esphome import pins
import os

CONF_TX_ENABLE_PIN = "tx_enable_pin"
CONF_TX_ENABLE_DIRECT = "tx_enable_direct"
CONF_TCP_BRIDGE_ENABLED = "tcp_bridge_enabled"
CONF_TCP_BRIDGE_PORT = "tcp_bridge_port"
CONF_TCP_BRIDGE_MAX_CLIENTS = "tcp_bridge_max_clients"

flexit_modbus_server_ns = cg.esphome_ns.namespace("flexit_modbus_server")
FlexitModbusDeviceComponent = flexit_modbus_server_ns.class_("FlexitModbusServer", cg.Component)

DEPENDENCIES = ["uart", "wifi"]

CONFIG_SCHEMA = (
    cv.Schema(
        {
            cv.GenerateID(): cv.declare_id(FlexitModbusDeviceComponent),
            cv.Required(CONF_ADDRESS): cv.positive_int,
            cv.Optional(CONF_TX_ENABLE_PIN): pins.gpio_output_pin_schema,
            cv.Optional(CONF_TX_ENABLE_DIRECT, True): cv.boolean,
            cv.Optional(CONF_TCP_BRIDGE_ENABLED, False): cv.boolean,
            cv.Optional(CONF_TCP_BRIDGE_PORT, 502): cv.port,
            cv.Optional(CONF_TCP_BRIDGE_MAX_CLIENTS, 4): cv.int_range(min=1, max=10),
        }
    )
    .extend(uart.UART_DEVICE_SCHEMA)
    .extend(cv.COMPONENT_SCHEMA)
)

MULTI_CONF = True
CODEOWNERS = ["@MSkjel"]

async def to_code(config):
    local_lib_exists = False
    component_dir = os.path.dirname(__file__)

    if (os.path.exists(os.path.join(component_dir, "ModbusRTUServer.h")) or
        os.path.exists(os.path.join(component_dir, "ModbusRTUServer.cpp"))):
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
        cg.add_library(
            name="ModbusRTUServer",
            repository="https://github.com/MSkjel/ESP-ModbusRTUServer.git",
            version=None
        )

    id = config[CONF_ID]
    uart = await cg.get_variable(config["uart_id"])
    server = cg.new_Pvariable(id)
    cg.add(server.set_uart_parent(uart))
    cg.add(server.set_server_address(config[CONF_ADDRESS]))
    cg.add(server.set_tx_enable_direct(config[CONF_TX_ENABLE_DIRECT]))

    if CONF_TX_ENABLE_PIN in config:
        pin_config = config[CONF_TX_ENABLE_PIN]
        if 'number' in pin_config:
            pin_number = pin_config['number']
            cg.add(server.set_tx_enable_pin(pin_number))

    cg.add(server.set_tcp_bridge_enabled(config[CONF_TCP_BRIDGE_ENABLED]))
    cg.add(server.set_tcp_bridge_port(config[CONF_TCP_BRIDGE_PORT]))
    cg.add(server.set_tcp_bridge_max_clients(config[CONF_TCP_BRIDGE_MAX_CLIENTS]))

    await cg.register_component(server, config)

    return