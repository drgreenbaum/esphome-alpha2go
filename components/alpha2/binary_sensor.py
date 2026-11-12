import esphome.codegen as cg
from esphome.components import binary_sensor
import esphome.config_validation as cv

from . import Alpha2, CONF_ALPHA2_ID

CONFIG_SCHEMA = cv.Schema(
    {
        cv.GenerateID(CONF_ALPHA2_ID): cv.use_id(Alpha2),
        # Manual venting in progress, register 560008.
        cv.Optional("venting_active"): binary_sensor.binary_sensor_schema(),
    }
)


async def to_code(config):
    hub = await cg.get_variable(config[CONF_ALPHA2_ID])

    if venting_active_config := config.get("venting_active"):
        sens = await binary_sensor.new_binary_sensor(venting_active_config)
        cg.add(hub.set_venting_active_sensor(sens))
