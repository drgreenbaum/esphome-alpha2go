import esphome.codegen as cg
import esphome.config_validation as cv
from esphome.components import switch
from esphome.const import CONF_TYPE

from . import Alpha2, CONF_ALPHA2_ID, alpha2_ns

Alpha2Switch = alpha2_ns.class_("Alpha2Switch", switch.Switch)
Alpha2ExternControlSwitch = alpha2_ns.class_("Alpha2ExternControlSwitch", switch.Switch)
Alpha2NightSetbackSwitch = alpha2_ns.class_("Alpha2NightSetbackSwitch", switch.Switch)
Alpha2MinFlowLimitSwitch = alpha2_ns.class_("Alpha2MinFlowLimitSwitch", switch.Switch)
Alpha2MaxFlowLimitSwitch = alpha2_ns.class_("Alpha2MaxFlowLimitSwitch", switch.Switch)
Alpha2ContinuousVentingSwitch = alpha2_ns.class_("Alpha2ContinuousVentingSwitch", switch.Switch)
Alpha2PanelLockSwitch = alpha2_ns.class_("Alpha2PanelLockSwitch", switch.Switch)
Alpha2PreventDisplaySleepSwitch = alpha2_ns.class_("Alpha2PreventDisplaySleepSwitch", switch.Switch)

TYPE_POWER = "power"
TYPE_EXTERNAL_CONTROL = "external_control"
TYPE_NIGHT_SETBACK = "night_setback"
TYPE_MIN_FLOW_LIMIT = "min_flow_limit"
TYPE_MAX_FLOW_LIMIT = "max_flow_limit"
TYPE_CONTINUOUS_VENTING = "continuous_venting"
TYPE_PANEL_LOCK = "panel_lock"
TYPE_PREVENT_DISPLAY_SLEEP = "prevent_display_sleep"

CONFIG_SCHEMA = cv.typed_schema(
    {
        TYPE_POWER: switch.switch_schema(Alpha2Switch).extend(
            {cv.GenerateID(CONF_ALPHA2_ID): cv.use_id(Alpha2)}
        ),
        # External (PWM) control, register 5c0193 (0x0000=off, 0x7b00=on).
        TYPE_EXTERNAL_CONTROL: switch.switch_schema(Alpha2ExternControlSwitch).extend(
            {cv.GenerateID(CONF_ALPHA2_ID): cv.use_id(Alpha2)}
        ),
        # Night setback, register 5a0008 (1=on, 0=off).
        TYPE_NIGHT_SETBACK: switch.switch_schema(Alpha2NightSetbackSwitch).extend(
            {cv.GenerateID(CONF_ALPHA2_ID): cv.use_id(Alpha2)}
        ),
        # Registers 9b560259 (min) / 9b560258 (max) - enable/disable ONLY;
        # the numeric value has its own `number` entities (min_flow_limit/
        # max_flow_limit types in number.py). See write_min_flow_limit()/
        # write_max_flow_limit() in alpha2.cpp for the "echoes back the
        # last-written value" caveat.
        TYPE_MIN_FLOW_LIMIT: switch.switch_schema(Alpha2MinFlowLimitSwitch).extend(
            {cv.GenerateID(CONF_ALPHA2_ID): cv.use_id(Alpha2)}
        ),
        TYPE_MAX_FLOW_LIMIT: switch.switch_schema(Alpha2MaxFlowLimitSwitch).extend(
            {cv.GenerateID(CONF_ALPHA2_ID): cv.use_id(Alpha2)}
        ),
        # "Continuous air detection and venting", register 580263.
        TYPE_CONTINUOUS_VENTING: switch.switch_schema(Alpha2ContinuousVentingSwitch).extend(
            {cv.GenerateID(CONF_ALPHA2_ID): cv.use_id(Alpha2)}
        ),
        # Panel lock, register 5e0002 (1=on, 0=off).
        TYPE_PANEL_LOCK: switch.switch_schema(Alpha2PanelLockSwitch).extend(
            {cv.GenerateID(CONF_ALPHA2_ID): cv.use_id(Alpha2)}
        ),
        # Prevent display sleep, register 5e0072 (inverted: raw 0 = on).
        TYPE_PREVENT_DISPLAY_SLEEP: switch.switch_schema(Alpha2PreventDisplaySleepSwitch).extend(
            {cv.GenerateID(CONF_ALPHA2_ID): cv.use_id(Alpha2)}
        ),
    },
    default_type=TYPE_POWER,
)


async def to_code(config):
    var = await switch.new_switch(config)
    hub = await cg.get_variable(config[CONF_ALPHA2_ID])
    cg.add(var.set_parent(hub))
    type_ = config[CONF_TYPE]
    # Also register the entity back on the hub so handle_geni_response_()
    # can push live GET-response readback into it.
    if type_ == TYPE_POWER:
        cg.add(hub.set_power_switch(var))
    elif type_ == TYPE_EXTERNAL_CONTROL:
        cg.add(hub.set_extern_control_switch(var))
    elif type_ == TYPE_NIGHT_SETBACK:
        cg.add(hub.set_night_setback_switch(var))
    elif type_ == TYPE_MIN_FLOW_LIMIT:
        cg.add(hub.set_min_flow_limit_switch(var))
    elif type_ == TYPE_MAX_FLOW_LIMIT:
        cg.add(hub.set_max_flow_limit_switch(var))
    elif type_ == TYPE_CONTINUOUS_VENTING:
        cg.add(hub.set_continuous_venting_switch(var))
    elif type_ == TYPE_PANEL_LOCK:
        cg.add(hub.set_panel_lock_switch(var))
    elif type_ == TYPE_PREVENT_DISPLAY_SLEEP:
        cg.add(hub.set_prevent_display_sleep_switch(var))
