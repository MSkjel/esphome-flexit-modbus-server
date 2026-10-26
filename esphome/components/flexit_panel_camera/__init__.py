import esphome.codegen as cg
import esphome.config_validation as cv
from esphome.const import CONF_ID
from esphome.core.entity_helpers import setup_entity

# The panel's screensaver picture as a Home Assistant camera, see packages/panel/camera.yaml. The pictures come from
# packages/code/panel/snapshot.h.
DEPENDENCIES = ["esp32"]
AUTO_LOAD = ["camera"]

flexit_panel_camera_ns = cg.esphome_ns.namespace("flexit_panel_camera")
PanelCamera = flexit_panel_camera_ns.class_("PanelCamera", cg.Component, cg.EntityBase)

CONFIG_SCHEMA = cv.ENTITY_BASE_SCHEMA.extend(
    {
        cv.GenerateID(): cv.declare_id(PanelCamera),
    }
).extend(cv.COMPONENT_SCHEMA)


async def to_code(config):
    cg.add_define("USE_CAMERA")
    cg.add_define("USE_FLEXIT_PANEL_CAMERA")
    var = cg.new_Pvariable(config[CONF_ID])
    await setup_entity(var, config, "camera")
    await cg.register_component(var, config)
