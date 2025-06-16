import esphome.codegen as cg
import esphome.config_validation as cv
from esphome.components import uart
from esphome.const import CONF_ADDRESS, CONF_ID
from esphome import pins

CONF_TX_ENABLE_PIN        = "tx_enable_pin"
CONF_TX_ENABLE_DIRECT = "tx_enable_direct"

flexit_modbus_server_ns = cg.esphome_ns.namespace("flexit_modbus_server")
FlexitModbusDeviceComponent = flexit_modbus_server_ns.class_("FlexitModbusServer", cg.Component)

DEPENDENCIES = ["uart"]

CONFIG_SCHEMA = (
    cv.Schema(
        {
            cv.GenerateID(): cv.declare_id(FlexitModbusDeviceComponent),
            cv.Required(CONF_ADDRESS): cv.positive_int,
            cv.Optional(CONF_TX_ENABLE_PIN): pins.gpio_output_pin_schema,
            cv.Optional(CONF_TX_ENABLE_DIRECT, True): cv.boolean,
        }
    )
    .extend(uart.UART_DEVICE_SCHEMA)
    .extend(cv.COMPONENT_SCHEMA)
)

MULTI_CONF = True
CODEOWNERS = ["@MSkjel"]

async def to_code(config):
    #cg.add_library("emelianov/modbus-esp8266", "4.1.0")
    cg.add_library(name="ModbusRTUServer", repository="https://github.com/MSkjel/ESP-ModbusRTUServer.git", version=None)
    id = config[CONF_ID]
    uart = await cg.get_variable(config["uart_id"])
    server = cg.new_Pvariable(id)
    cg.add(server.set_uart_parent(uart))
    cg.add(server.set_server_address(config[CONF_ADDRESS]))
    cg.add(server.set_tx_enable_direct(config[CONF_TX_ENABLE_DIRECT]))

    if CONF_TX_ENABLE_PIN in config:
        # Extract the pin from the esphome object
        pin_config = config[CONF_TX_ENABLE_PIN]
        if 'number' in pin_config:
            pin_number = pin_config['number']
            cg.add(server.set_tx_enable_pin(pin_number))

    await cg.register_component(server, config)

    return