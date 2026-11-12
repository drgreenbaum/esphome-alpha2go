import esphome.codegen as cg
import esphome.config_validation as cv
from esphome.components import select
from esphome.const import CONF_TYPE

from . import Alpha2, CONF_ALPHA2_ID, alpha2_ns

Alpha2OperatingModeSelect = alpha2_ns.class_("Alpha2OperatingModeSelect", select.Select, cg.Component)
Alpha2PanelSettingSelect = alpha2_ns.class_("Alpha2PanelSettingSelect", select.Select, cg.Component)
Alpha2CurveSelect = alpha2_ns.class_("Alpha2CurveSelect", select.Select, cg.Component)

TYPE_OPERATING_MODE = "operating_mode"
TYPE_PANEL_SETTING = "panel_setting"
TYPE_CURVE = "curve"

# Option labels - must match the tables in alpha2.cpp
# (ALPHA2_OPERATING_MODE_OPTIONS / ALPHA2_PANEL_OPTIONS / write_curve()).
OPERATING_MODE_OPTIONS = [
    "Constant pressure",
    "Proportional pressure",
    "Constant curve",
    "Constant flow",
    "Proportional pressure + AutoAdapt",
    "Constant pressure + AutoAdapt",
]
PANEL_SETTING_OPTIONS = [
    "Constant curve I",
    "Constant curve II",
    "Constant curve III",
    "Constant pressure I",
    "Constant pressure II",
    "Constant pressure III",
    "Proportional pressure I",
    "Proportional pressure II",
    "Proportional pressure III",
    "Constant pressure + AutoAdapt",
    "Proportional pressure + AutoAdapt",
    "Advanced",
]
CURVE_OPTIONS = ["Normal", "Min curve", "Max curve"]

_EXTRA = {cv.GenerateID(CONF_ALPHA2_ID): cv.use_id(Alpha2)}

CONFIG_SCHEMA = cv.typed_schema(
    {
        # Control mode with free set points (see the number platform).
        TYPE_OPERATING_MODE: select.select_schema(Alpha2OperatingModeSelect).extend(_EXTRA),
        # The pump's own panel settings I/II/III and AutoAdapt.
        TYPE_PANEL_SETTING: select.select_schema(Alpha2PanelSettingSelect).extend(_EXTRA),
        # Normal operation / min curve / max curve.
        TYPE_CURVE: select.select_schema(Alpha2CurveSelect).extend(_EXTRA),
    },
    default_type=TYPE_OPERATING_MODE,
)


async def to_code(config):
    type_ = config[CONF_TYPE]
    options = {
        TYPE_OPERATING_MODE: OPERATING_MODE_OPTIONS,
        TYPE_PANEL_SETTING: PANEL_SETTING_OPTIONS,
        TYPE_CURVE: CURVE_OPTIONS,
    }[type_]
    var = await select.new_select(config, options=options)
    await cg.register_component(var, config)
    hub = await cg.get_variable(config[CONF_ALPHA2_ID])
    cg.add(var.set_parent(hub))
    if type_ == TYPE_OPERATING_MODE:
        cg.add(hub.set_operating_mode_select(var))
    elif type_ == TYPE_PANEL_SETTING:
        cg.add(hub.set_panel_setting_select(var))
    else:
        cg.add(hub.set_curve_select(var))
