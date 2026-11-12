import esphome.codegen as cg
import esphome.config_validation as cv
from esphome.components import select

from . import Alpha2, CONF_ALPHA2_ID, alpha2_ns

Alpha2OperatingModeSelect = alpha2_ns.class_("Alpha2OperatingModeSelect", select.Select, cg.Component)

# Option labels - must match ALPHA2_OPERATING_MODE_OPTIONS in alpha2.cpp,
# which maps them to the GENIbus codes written to register 56000a.
OPERATING_MODE_OPTIONS = [
    "Constant pressure",
    "Proportional pressure",
    "Constant curve",
    "Constant flow",
    "Proportional pressure + AutoAdapt",
    "Constant pressure + AutoAdapt",
]

CONFIG_SCHEMA = select.select_schema(Alpha2OperatingModeSelect).extend(
    {
        cv.GenerateID(CONF_ALPHA2_ID): cv.use_id(Alpha2),
    }
)


async def to_code(config):
    var = await select.new_select(config, options=OPERATING_MODE_OPTIONS)
    await cg.register_component(var, config)
    hub = await cg.get_variable(config[CONF_ALPHA2_ID])
    cg.add(var.set_parent(hub))
    cg.add(hub.set_operating_mode_select(var))
