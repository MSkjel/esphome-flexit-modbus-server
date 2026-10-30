import esphome.codegen as cg
from esphome.const import DEVICE_CLASS_FIRMWARE

from esphome.components import update

# The panel's firmware as an update in Home Assistant, see packages/panel/update.yaml
DEPENDENCIES = ["esp32"]

flexit_modbus_server_ns = cg.esphome_ns.namespace("flexit_modbus_server")
PanelUpdate = flexit_modbus_server_ns.class_("PanelUpdate", update.UpdateEntity)

CONFIG_SCHEMA = update.update_schema(PanelUpdate, device_class=DEVICE_CLASS_FIRMWARE)


async def to_code(config):
    await update.new_update(config)
