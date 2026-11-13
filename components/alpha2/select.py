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

# Default option labels. The C++ side works on option INDEX, so the order
# must match ALPHA2_OPERATING_MODE_OPTIONS / ALPHA2_PANEL_OPTIONS /
# write_curve() in alpha2.cpp; the text can be changed with option_labels.
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

CONF_OPTION_LABELS = "option_labels"

_DEFAULTS = {
    TYPE_OPERATING_MODE: OPERATING_MODE_OPTIONS,
    TYPE_PANEL_SETTING: PANEL_SETTING_OPTIONS,
    TYPE_CURVE: CURVE_OPTIONS,
}


def _labels_for(type_):
    """option_labels: {default option: shown label} - e.g. to translate."""

    def validator(value):
        value = cv.Schema({cv.string: cv.string_strict})(value)
        for key in value:
            if key not in _DEFAULTS[type_]:
                raise cv.Invalid(f"Unknown option '{key}', valid options: {_DEFAULTS[type_]}")
        shown = [value.get(o, o) for o in _DEFAULTS[type_]]
        if len(set(shown)) != len(shown):
            raise cv.Invalid("option_labels must not make two options identical")
        return value

    return validator


def _extra(type_):
    return {
        cv.GenerateID(CONF_ALPHA2_ID): cv.use_id(Alpha2),
        cv.Optional(CONF_OPTION_LABELS, default={}): _labels_for(type_),
    }

CONFIG_SCHEMA = cv.typed_schema(
    {
        # Control mode with free set points (see the number platform).
        TYPE_OPERATING_MODE: select.select_schema(Alpha2OperatingModeSelect).extend(_extra(TYPE_OPERATING_MODE)),
        # The pump's own panel settings I/II/III and AutoAdapt.
        TYPE_PANEL_SETTING: select.select_schema(Alpha2PanelSettingSelect).extend(_extra(TYPE_PANEL_SETTING)),
        # Normal operation / min curve / max curve.
        TYPE_CURVE: select.select_schema(Alpha2CurveSelect).extend(_extra(TYPE_CURVE)),
    },
    default_type=TYPE_OPERATING_MODE,
)


async def to_code(config):
    type_ = config[CONF_TYPE]
    labels = config[CONF_OPTION_LABELS]
    options = [labels.get(o, o) for o in _DEFAULTS[type_]]
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
