import esphome.codegen as cg
from esphome.components import ble_client, sensor
import esphome.config_validation as cv
from esphome.const import (
    CONF_CURRENT,
    CONF_FLOW,
    CONF_HEAD,
    CONF_ID,
    CONF_POWER,
    CONF_SPEED,
    CONF_VOLTAGE,
    UNIT_AMPERE,
    UNIT_CELSIUS,
    UNIT_CUBIC_METER_PER_HOUR,
    UNIT_METER,
    UNIT_REVOLUTIONS_PER_MINUTE,
    UNIT_VOLT,
    UNIT_WATT,
)

from . import Alpha2

CONFIG_SCHEMA = (
    cv.Schema(
        {
            cv.GenerateID(): cv.declare_id(Alpha2),
            cv.Optional(CONF_FLOW): sensor.sensor_schema(
                unit_of_measurement=UNIT_CUBIC_METER_PER_HOUR,
                accuracy_decimals=2,
            ),
            cv.Optional(CONF_HEAD): sensor.sensor_schema(
                unit_of_measurement=UNIT_METER,
                accuracy_decimals=1,
            ),
            cv.Optional(CONF_POWER): sensor.sensor_schema(
                unit_of_measurement=UNIT_WATT,
                accuracy_decimals=2,
            ),
            cv.Optional(CONF_CURRENT): sensor.sensor_schema(
                unit_of_measurement=UNIT_AMPERE,
                accuracy_decimals=2,
            ),
            cv.Optional(CONF_SPEED): sensor.sensor_schema(
                unit_of_measurement=UNIT_REVOLUTIONS_PER_MINUTE,
                accuracy_decimals=0,
            ),
            cv.Optional(CONF_VOLTAGE): sensor.sensor_schema(
                unit_of_measurement=UNIT_VOLT,
                accuracy_decimals=2,
            ),
            # Raw operating-mode code (register 56000a): 0=constant pressure,
            # 1=proportional pressure, 2=constant curve, 8=constant flow,
            # 13=proportional pressure + AutoAdapt, 14=constant pressure +
            # AutoAdapt. Prefer the `select` platform for a readable value.
            cv.Optional("operating_mode"): sensor.sensor_schema(
                accuracy_decimals=0,
            ),
            # External (PWM) control enabled, register 5c0193. Raw uint16:
            # 0x0000=off, 0x7b00=on.
            cv.Optional("external_control"): sensor.sensor_schema(
                accuracy_decimals=0,
            ),
            # Night setback enabled, register 5a0008. Raw 0/1.
            cv.Optional("night_setback"): sensor.sensor_schema(
                accuracy_decimals=0,
            ),
            # Venting status, register 580263. Raw code: 1=manual venting
            # active, 2=continuous venting off, 3=continuous venting on.
            cv.Optional("venting_status"): sensor.sensor_schema(
                accuracy_decimals=0,
            ),
            # Manual venting countdown, register 560008. Raw value, unit
            # unknown; only meaningful while venting is active.
            cv.Optional("venting_countdown"): sensor.sensor_schema(
                accuracy_decimals=1,
            ),
            # Calculated media temperature, register 5d012c - matches the
            # app's "Show all measurements" screen.
            cv.Optional("media_temperature"): sensor.sensor_schema(
                unit_of_measurement=UNIT_CELSIUS,
                device_class="temperature",
                state_class="measurement",
                accuracy_decimals=1,
            ),
            # Number of pump starts, register 5d0001.
            cv.Optional("start_count"): sensor.sensor_schema(
                state_class="total_increasing",
                accuracy_decimals=0,
            ),
            # Active warning / alarm code, 0 = none. Registers 58000b /
            # 580000 (see README for the code list).
            cv.Optional("warning_code"): sensor.sensor_schema(
                accuracy_decimals=0,
                icon="mdi:alert-outline",
            ),
            cv.Optional("alarm_code"): sensor.sensor_schema(
                accuracy_decimals=0,
                icon="mdi:alert-octagon",
            ),
        }
    )
    .extend(ble_client.BLE_CLIENT_SCHEMA)
    .extend(cv.polling_component_schema("15s"))
)


async def to_code(config):
    var = cg.new_Pvariable(config[CONF_ID])
    await cg.register_component(var, config)
    await ble_client.register_ble_node(var, config)

    if flow_config := config.get(CONF_FLOW):
        sens = await sensor.new_sensor(flow_config)
        cg.add(var.set_flow_sensor(sens))

    if head_config := config.get(CONF_HEAD):
        sens = await sensor.new_sensor(head_config)
        cg.add(var.set_head_sensor(sens))

    if power_config := config.get(CONF_POWER):
        sens = await sensor.new_sensor(power_config)
        cg.add(var.set_power_sensor(sens))

    if current_config := config.get(CONF_CURRENT):
        sens = await sensor.new_sensor(current_config)
        cg.add(var.set_current_sensor(sens))

    if speed_config := config.get(CONF_SPEED):
        sens = await sensor.new_sensor(speed_config)
        cg.add(var.set_speed_sensor(sens))

    if voltage_config := config.get(CONF_VOLTAGE):
        sens = await sensor.new_sensor(voltage_config)
        cg.add(var.set_voltage_sensor(sens))

    if operating_mode_config := config.get("operating_mode"):
        sens = await sensor.new_sensor(operating_mode_config)
        cg.add(var.set_operating_mode_sensor(sens))

    if external_control_config := config.get("external_control"):
        sens = await sensor.new_sensor(external_control_config)
        cg.add(var.set_extern_control_sensor(sens))

    if night_setback_config := config.get("night_setback"):
        sens = await sensor.new_sensor(night_setback_config)
        cg.add(var.set_night_setback_sensor(sens))

    if venting_status_config := config.get("venting_status"):
        sens = await sensor.new_sensor(venting_status_config)
        cg.add(var.set_venting_status_sensor(sens))

    if venting_countdown_config := config.get("venting_countdown"):
        sens = await sensor.new_sensor(venting_countdown_config)
        cg.add(var.set_venting_countdown_sensor(sens))

    if media_temperature_config := config.get("media_temperature"):
        sens = await sensor.new_sensor(media_temperature_config)
        cg.add(var.set_media_temp_sensor(sens))

    if start_count_config := config.get("start_count"):
        sens = await sensor.new_sensor(start_count_config)
        cg.add(var.set_start_count_sensor(sens))

    if warning_code_config := config.get("warning_code"):
        sens = await sensor.new_sensor(warning_code_config)
        cg.add(var.set_warning_code_sensor(sens))

    if alarm_code_config := config.get("alarm_code"):
        sens = await sensor.new_sensor(alarm_code_config)
        cg.add(var.set_alarm_code_sensor(sens))
