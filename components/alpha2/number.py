import esphome.codegen as cg
import esphome.config_validation as cv
from esphome.components import number
from esphome.const import CONF_TYPE, UNIT_CUBIC_METER_PER_HOUR, UNIT_METER

from . import Alpha2, CONF_ALPHA2_ID, alpha2_ns

Alpha2PressureSetpointNumber = alpha2_ns.class_("Alpha2PressureSetpointNumber", number.Number, cg.Component)
Alpha2FlowSetpointNumber = alpha2_ns.class_("Alpha2FlowSetpointNumber", number.Number, cg.Component)
Alpha2SpeedSetpointNumber = alpha2_ns.class_("Alpha2SpeedSetpointNumber", number.Number, cg.Component)
Alpha2MinFlowLimitNumber = alpha2_ns.class_("Alpha2MinFlowLimitNumber", number.Number, cg.Component)
Alpha2MaxFlowLimitNumber = alpha2_ns.class_("Alpha2MaxFlowLimitNumber", number.Number, cg.Component)

TYPE_PRESSURE = "pressure_setpoint"
TYPE_FLOW = "flow_setpoint"
TYPE_SPEED = "speed_setpoint"
TYPE_MIN_FLOW_LIMIT = "min_flow_limit"
TYPE_MAX_FLOW_LIMIT = "max_flow_limit"

CONFIG_SCHEMA = cv.typed_schema(
    {
        TYPE_PRESSURE: number.number_schema(
            Alpha2PressureSetpointNumber,
            unit_of_measurement=UNIT_METER,
            icon="mdi:gauge",
        ).extend({cv.GenerateID(CONF_ALPHA2_ID): cv.use_id(Alpha2)}),
        TYPE_FLOW: number.number_schema(
            Alpha2FlowSetpointNumber,
            unit_of_measurement=UNIT_CUBIC_METER_PER_HOUR,
            icon="mdi:gauge",
        ).extend({cv.GenerateID(CONF_ALPHA2_ID): cv.use_id(Alpha2)}),
        # Constant-curve speed setpoint, raw value = rpm.
        TYPE_SPEED: number.number_schema(
            Alpha2SpeedSetpointNumber,
            unit_of_measurement="rpm",
            icon="mdi:speedometer",
        ).extend({cv.GenerateID(CONF_ALPHA2_ID): cv.use_id(Alpha2)}),
        # Flow limitation values; setting one also enables the matching
        # min/max flow limit switch.
        TYPE_MIN_FLOW_LIMIT: number.number_schema(
            Alpha2MinFlowLimitNumber,
            unit_of_measurement=UNIT_CUBIC_METER_PER_HOUR,
            icon="mdi:water-minus",
        ).extend({cv.GenerateID(CONF_ALPHA2_ID): cv.use_id(Alpha2)}),
        TYPE_MAX_FLOW_LIMIT: number.number_schema(
            Alpha2MaxFlowLimitNumber,
            unit_of_measurement=UNIT_CUBIC_METER_PER_HOUR,
            icon="mdi:water-plus",
        ).extend({cv.GenerateID(CONF_ALPHA2_ID): cv.use_id(Alpha2)}),
    },
)


async def to_code(config):
    type_ = config[CONF_TYPE]
    if type_ == TYPE_PRESSURE:
        # Range offered by Grundfos GO: 1.0-4.5 m.
        var = await number.new_number(config, min_value=1.0, max_value=4.5, step=0.1)
    elif type_ == TYPE_FLOW:
        # Range offered by Grundfos GO: 0.3-2.9 m3/h.
        var = await number.new_number(config, min_value=0.3, max_value=2.9, step=0.1)
    elif type_ == TYPE_SPEED:
        # Range offered by Grundfos GO: 2163-5163 rpm.
        var = await number.new_number(config, min_value=2163.0, max_value=5163.0, step=10.0)
    else:
        # min_flow_limit / max_flow_limit: 0.25-3.5 m3/h (XX-60 variant;
        # XX-40 tops out at 2.0, XX-75/90 at 3.5/3.8 per the I/O manual).
        var = await number.new_number(config, min_value=0.25, max_value=3.5, step=0.1)
    await cg.register_component(var, config)
    hub = await cg.get_variable(config[CONF_ALPHA2_ID])
    cg.add(var.set_parent(hub))
    # Also register the entity back on the hub so handle_geni_response_()
    # can push live GET-response readback into it.
    if type_ == TYPE_PRESSURE:
        cg.add(hub.set_pressure_setpoint_number(var))
    elif type_ == TYPE_FLOW:
        cg.add(hub.set_flow_setpoint_number(var))
    elif type_ == TYPE_SPEED:
        cg.add(hub.set_speed_setpoint_number(var))
    elif type_ == TYPE_MIN_FLOW_LIMIT:
        cg.add(hub.set_min_flow_limit_number(var))
    elif type_ == TYPE_MAX_FLOW_LIMIT:
        cg.add(hub.set_max_flow_limit_number(var))
