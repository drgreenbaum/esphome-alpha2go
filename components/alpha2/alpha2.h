//original code from https://github.com/esphome/esphome/blob/dev/esphome/components/alpha3/
//I just figured out the new request "command" bytes and response type bytes
#pragma once

#include "esphome/core/component.h"
#include "esphome/core/defines.h"
#include "esphome/components/ble_client/ble_client.h"
#include "esphome/components/esp32_ble_tracker/esp32_ble_tracker.h"
#include "esphome/components/sensor/sensor.h"
#ifdef USE_BINARY_SENSOR
#include "esphome/components/binary_sensor/binary_sensor.h"
#endif
#ifdef USE_TEXT_SENSOR
#include "esphome/components/text_sensor/text_sensor.h"
#endif
#ifdef USE_SWITCH
#include "esphome/components/switch/switch.h"
#endif
#ifdef USE_NUMBER
#include "esphome/components/number/number.h"
#endif
#ifdef USE_SELECT
#include "esphome/components/select/select.h"
#endif

#include <string>

#ifdef USE_ESP32

#include <esp_gattc_api.h>

namespace esphome {
namespace alpha2 {

#ifdef USE_SWITCH
class Alpha2Switch;
class Alpha2ExternControlSwitch;
class Alpha2NightSetbackSwitch;
class Alpha2MinFlowLimitSwitch;
class Alpha2MaxFlowLimitSwitch;
class Alpha2ContinuousVentingSwitch;
class Alpha2PanelLockSwitch;
class Alpha2PreventDisplaySleepSwitch;
#endif
#if defined(USE_NUMBER) && defined(USE_SWITCH)
class Alpha2PressureSetpointNumber;
class Alpha2FlowSetpointNumber;
class Alpha2SpeedSetpointNumber;
class Alpha2MinFlowLimitNumber;
class Alpha2MaxFlowLimitNumber;
#endif

namespace espbt = esphome::esp32_ble_tracker;

static const espbt::ESPBTUUID ALPHA2_GENI_SERVICE_UUID = espbt::ESPBTUUID::from_uint16(0xfe5d);
static const espbt::ESPBTUUID ALPHA2_GENI_CHARACTERISTIC_UUID =
    espbt::ESPBTUUID::from_raw({static_cast<char>(0xa9), 0x7b, static_cast<char>(0xb8), static_cast<char>(0x85), 0x0,
                                0x1a, 0x28, static_cast<char>(0xaa), 0x2a, 0x43, 0x6e, 0x3, static_cast<char>(0xd1),
                                static_cast<char>(0xff), static_cast<char>(0x9c), static_cast<char>(0x85)});
static const int16_t GENI_RESPONSE_HEADER_LENGTH = 13;
static const size_t GENI_RESPONSE_TYPE_LENGTH = 8;

static const uint8_t GENI_RESPONSE_TYPE_FLOW_HEAD[GENI_RESPONSE_TYPE_LENGTH]   = {43, 0, 2, 53, 2, 0, 0, 36};
static const int16_t GENI_RESPONSE_FLOW_OFFSET = 0;
static const int16_t GENI_RESPONSE_HEAD_OFFSET = 4;

static const uint8_t GENI_RESPONSE_TYPE_POWER[GENI_RESPONSE_TYPE_LENGTH] = {48, 0, 1, 0, 3, 0, 0, 41};
static const int16_t GENI_RESPONSE_VOLTAGE_AC_OFFSET = 0;
static const int16_t GENI_RESPONSE_VOLTAGE_DC_OFFSET = 4;
static const int16_t GENI_RESPONSE_CURRENT_OFFSET = 8;
static const int16_t GENI_RESPONSE_POWER_OFFSET = 12;
static const int16_t GENI_RESPONSE_MOTOR_POWER_OFFSET = 16;  // not sure
static const int16_t GENI_RESPONSE_MOTOR_SPEED_OFFSET = 20;

// Operating mode status response, register 56000a - confirmed
// 2026-09-04 by reassembling real GENI notifications (class 0x0a GET reply).
// Type bytes are response[5.12] (0x0e opspec/len byte + the fixed 7-byte
// "2f 01 00 00 07" register-echo prefix seen in every observed instance);
// the mode byte itself sits at data offset 2 (00=Constant pressure,
// 01=Proportional pressure, 02=Constant curve unconfirmed, 08=Constant flow,
// 0d=AutoAdapt -.
static const uint8_t GENI_RESPONSE_TYPE_OPERATING_MODE[GENI_RESPONSE_TYPE_LENGTH] = {0x0e, 0x00, 0x01, 0x2f,
                                                                                0x01, 0x00, 0x00, 0x07};
static const int16_t GENI_RESPONSE_OPERATING_MODE_OFFSET = 2;
// IMPORTANT (2026-09-04): register 560006 (pump run/stop status, see below)
// shares this EXACT same 8-byte type - a data-offset-1 discriminator is
// needed to tell them apart (0x06=operating mode, 0x00/0x01=pump status).
static const int16_t GENI_RESPONSE_OPERATING_MODE_VS_PUMP_STATUS_DISCRIMINATOR_OFFSET = 1;
static const uint8_t GENI_DISCRIMINATOR_OPERATING_MODE = 0x06;

// Pump run/stop status, register 560006 - found 2026-09-04 (this is the
// SAME register originally tried and abandoned early on as a "setpoint"
// hypothesis - it isn't a setpoint, it's the run/stop status). Shares the
// operating mode response type (see discriminator note above); the status byte
// sits at data offset 1 itself (not offset 2 like operating mode's code).
// Confirmed via a clean capture with 4 physical pump on/off toggles
// producing exactly 4 clean value transitions, settling at 0x00 during a
// manual venting cycle (which requires the pump to be running) - so
// **0x00 = running, 0x01 = stopped** (direction inferred from that venting
// correlation, not 100% independently confirmed).
static const int16_t GENI_RESPONSE_PUMP_STATUS_OFFSET = 1;

// Manual venting active + countdown, register 560008 - ALSO shares the same
// type as operating mode/pump-status (see above), discriminated at data offset
// 0: 0x80=venting active (countdown float at data offset 3), 0x01=idle
// (unrelated noisy reading, ignored except as "not active"). Found
// 2026-09-04: confirmed via a real capture where this exact sequence was
// observed - noisy ~16800-17300 readings before/after venting, cleanly
// switching to offset0=0x80 with a monotonically decreasing float (5025.0
// -> 3225.0 over ~19s, IDENTICAL starting value 5025.0 on two separate
// venting-start events) exactly bracketing both manual venting sessions in
// the capture. Unit of the countdown value is not known (not simply
// seconds - decrements too fast for that), but it reliably indicates
// "venting in progress" via the offset-0 discriminator regardless.
static const int16_t GENI_RESPONSE_VENTING_COUNTDOWN_VALUE_OFFSET = 3;
static const uint8_t GENI_DISCRIMINATOR_VENTING_ACTIVE = 0x80;

// Calculated media temperature (calculated medium/fluid temperature), register
// 5d012c - found 2026-09-04 by matching the app's "Show all measurements"
// screen (24°C shown) against a systematic scan of all polled registers.
// Own unique response type, no known collision with other registers.
// Float at data offset 0; two more floats follow (data offset 4/8,
// ~44.8/~34.7 in the sample capture) whose meaning isn't decoded - possibly
// min/max thresholds, not needed for the temperature reading itself.
static const uint8_t GENI_RESPONSE_TYPE_MEDIA_TEMP[GENI_RESPONSE_TYPE_LENGTH] = {0x14, 0x00, 0x02, 0x16,
                                                                                  0x02, 0x00, 0x00, 0x0d};
static const int16_t GENI_RESPONSE_MEDIA_TEMP_OFFSET = 0;

// Antal starter (start count), register 5d0001 - found 2026-09-04. One
// uint16 field (data offset 2) of a larger 14-field statistics block, whose
// other fields aren't decoded. for the
// full field breakdown - includes a still-unidentified short-window runtime
// counter, NOT the pump's true lifetime "Ackumulerad drifttid"). Confirmed
// by restarting the pump once and diffing two captures ~1.5h apart: this
// field went 283->290 (+7), consistent with the amount of start/stop
// testing done in between.
static const uint8_t GENI_RESPONSE_TYPE_STATISTICS[GENI_RESPONSE_TYPE_LENGTH] = {0x23, 0x00, 0x00, 0xf8,
                                                                                  0x02, 0x00, 0x00, 0x1c};
static const int16_t GENI_RESPONSE_START_COUNT_OFFSET = 2;

// External control on/off, register 5c0193 - found
// 2026-09-04. IMPORTANT: this was originally (wrongly) attributed to
// register 5a0008 - corrected the same day after the user cross-checked a
// clean isolated capture against the app's live on/off state. Two-byte
// value: OFF=0x0000, ON=0x7b00 (confirmed via a clean off-then-on capture
// matching the app's current state exactly).
static const uint8_t GENI_RESPONSE_TYPE_EXTERN_CONTROL[GENI_RESPONSE_TYPE_LENGTH] = {0x09, 0x00, 0x04, 0x5e,
                                                                                      0x01, 0x00, 0x00, 0x02};
static const int16_t GENI_RESPONSE_EXTERN_CONTROL_OFFSET = 0;

// Night setback, register 5a0008 - found 2026-09-04.
// IMPORTANT: originally (wrongly) attributed to register 5c0193 - see the
// correction note on GENI_RESPONSE_TYPE_EXTERN_CONTROL above. Single-byte
// value (1=on, 0=off).
static const uint8_t GENI_RESPONSE_TYPE_NIGHT_SETBACK[GENI_RESPONSE_TYPE_LENGTH] = {0x08, 0x00, 0x01, 0x4d,
                                                                                     0x01, 0x00, 0x00, 0x01};

// Venting/air-detection status, register 580263 - found 2026-09-04.
// Single-byte value: 2=continuous air-detection-and-venting off, 3=on -
// BOTH CONFIRMED reliable live on hardware. Value 1 appears momentarily as
// part of the manual-venting START write bundle (see write_venting()) but
// is NOT an ongoing "manual venting active" status - confirmed live (user
// heard the pump actually venting while this register stayed stuck at 2,
// never showing 1). Don't use this sensor for "is manual venting running
// right now" - no register for that is currently known.
static const uint8_t GENI_RESPONSE_TYPE_VENTING_STATUS[GENI_RESPONSE_TYPE_LENGTH] = {0x0e, 0x00, 0x03, 0xca,
                                                                                      0x01, 0x00, 0x00, 0x07};
static const int16_t GENI_RESPONSE_VENTING_STATUS_OFFSET = 0;

// Manual venting "type" byte (2026-09-04) - written to register 580263 as
// part of the START bundle (see write_venting()). 0x01 seen in earlier
// tests (never explicitly labeled at the time, assumed "Pump" by default);
// 0x02 confirmed via a capture the user explicitly described as starting
// with "system+pump on". Hypothesis, not independently re-verified.
static const uint8_t GENI_VENTING_TYPE_PUMP = 0x01;
static const uint8_t GENI_VENTING_TYPE_SYSTEM_PUMP = 0x02;
static const int16_t GENI_RESPONSE_NIGHT_SETBACK_OFFSET = 0;

// Setpoint readback (2026-09-04) - pressure/flow/speed setpoints (registers
// 9b560012/9b560028/9b56000e) all share this SAME 8-byte response type
// signature (their real per-register discriminator is a 9th byte, data
// offset 1, right after this type - NOT part of the type match). Confirmed
// via real GET responses: pressure read back exactly 3.0m, flow 0.9 m3/h,
// speed 5225 rpm, all round/clean values matching live app state.
static const uint8_t GENI_RESPONSE_TYPE_SETPOINT[GENI_RESPONSE_TYPE_LENGTH] = {0x19, 0x00, 0x01, 0x2e,
                                                                                0x01, 0x00, 0x00, 0x12};
static const int16_t GENI_RESPONSE_SETPOINT_DISCRIMINATOR_OFFSET = 1;  // data offset of the 0x04/0x06/0xff byte
static const int16_t GENI_RESPONSE_SETPOINT_VALUE_OFFSET = 2;          // data offset of the float target
static const uint8_t GENI_SETPOINT_DISCRIMINATOR_PRESSURE = 0x04;
static const uint8_t GENI_SETPOINT_DISCRIMINATOR_FLOW = 0x06;
static const uint8_t GENI_SETPOINT_DISCRIMINATOR_SPEED = 0xff;

// Flow-limitation readback (2026-09-04) - min/max flow limit (registers
// 9b560259/9b560258) share this type signature too, discriminated by data
// offset 0 (0x02=min, 0x01=max). Data offset 1 is the enable/disable byte
// (0x01/0x00), so this readback also gives us the switch state for free.
// Confirmed exact match: max read back 2.0 m3/h, min 0.4 m3/h.
static const uint8_t GENI_RESPONSE_TYPE_FLOW_LIMIT[GENI_RESPONSE_TYPE_LENGTH] = {0x19, 0x00, 0x03, 0x7f,
                                                                                  0x01, 0x00, 0x00, 0x12};
static const int16_t GENI_RESPONSE_FLOW_LIMIT_DISCRIMINATOR_OFFSET = 0;  // 0x01=max, 0x02=min
static const int16_t GENI_RESPONSE_FLOW_LIMIT_ENABLE_OFFSET = 1;
static const int16_t GENI_RESPONSE_FLOW_LIMIT_VALUE_OFFSET = 2;
static const uint8_t GENI_FLOW_LIMIT_DISCRIMINATOR_MAX = 0x01;
static const uint8_t GENI_FLOW_LIMIT_DISCRIMINATOR_MIN = 0x02;

// ---------------------------------------------------------------------------
// Device info (ASCII text) fields, found 2026-09-04 - matches the app's
// "Product info" screen exactly. Completely different, much simpler protocol
// shape than the Class-10 GENIbus registers above: GENIbus Class 7, opspec
// 0x01, single field-id byte, response is [0x24][len][0xf8][0xe7][0x07]
// [ascii_data_length][ascii bytes incl. trailing NUL][2-byte CRC] - no type
// signature to match, no discriminator, just a length-prefixed string. Long
// fields (App/BLE software, GSC identification) span 2 BLE fragments, same
// as the Class-10 responses.
// ---------------------------------------------------------------------------
static const uint8_t GENI_CLASS_DEVICE_INFO = 0x07;
static const uint8_t GENI_DEVICE_INFO_FIELD_PRODUCT_TYPE = 0x01;
static const uint8_t GENI_DEVICE_INFO_FIELD_PRODUCT_NO = 0x08;
static const uint8_t GENI_DEVICE_INFO_FIELD_SERIAL_NO = 0x09;
static const uint8_t GENI_DEVICE_INFO_FIELD_PRODUCTION_CODE = 0x0a;
static const uint8_t GENI_DEVICE_INFO_FIELD_GSC_DESCRIPTION = 0x13;
static const uint8_t GENI_DEVICE_INFO_FIELD_GSC_IDENTIFICATION = 0x15;
static const uint8_t GENI_DEVICE_INFO_FIELD_APP_SOFTWARE = 0x32;
static const uint8_t GENI_DEVICE_INFO_FIELD_BLE_SOFTWARE = 0x3a;
static const size_t GENI_DEVICE_INFO_BUFFER_LENGTH = 40;
#ifdef USE_TEXT_SENSOR
// Fetch order for the fields above (2026-09-04) - see device_info_fetch_
// active_ in the Alpha2 class and loop() in alpha2.cpp for why this is now
// driven one-at-a-time from loop() instead of a fixed delay() sequence in
// update(): a real capture showed ALL 8 fields' responses arriving in one
// late batch when fired with fixed delays, since the BLE connection's own
// actual round-trip time varies and can exceed any fixed guess.
static const uint8_t GENI_DEVICE_INFO_FIELD_ORDER[] = {
    GENI_DEVICE_INFO_FIELD_PRODUCT_TYPE,     GENI_DEVICE_INFO_FIELD_PRODUCT_NO,
    GENI_DEVICE_INFO_FIELD_SERIAL_NO,        GENI_DEVICE_INFO_FIELD_PRODUCTION_CODE,
    GENI_DEVICE_INFO_FIELD_GSC_DESCRIPTION,  GENI_DEVICE_INFO_FIELD_GSC_IDENTIFICATION,
    GENI_DEVICE_INFO_FIELD_APP_SOFTWARE,     GENI_DEVICE_INFO_FIELD_BLE_SOFTWARE,
};
static const size_t GENI_DEVICE_INFO_FIELD_COUNT = 8;
#endif

// Panel lock, register 5e0002 - found 2026-09-04. Single-byte
// boolean (0=off, 1=on), confirmed by cross-checking the pump's live app
// state (both off) against the last write in an isolated "panel lock av och
// on" capture (which also ended on 0).
static const uint8_t GENI_RESPONSE_TYPE_PANEL_LOCK[GENI_RESPONSE_TYPE_LENGTH] = {0x0a, 0x00, 0x01, 0x0d,
                                                                                  0x01, 0x00, 0x00, 0x03};
static const int16_t GENI_RESPONSE_PANEL_LOCK_OFFSET = 0;

// Prevent display sleep, register 5e0072 -
// found 2026-09-04. Two-byte value, but only the HIGH byte is the on/off
// flag (0x00xx=off, 0x01xx=on) - the low byte stays a constant 0x0f in
// every capture (meaning unknown, possibly an unrelated fixed
// timeout/threshold field sharing this register).
static const uint8_t GENI_RESPONSE_TYPE_PREVENT_DISPLAY_SLEEP[GENI_RESPONSE_TYPE_LENGTH] = {0x09, 0x00, 0x03, 0x97,
                                                                                              0x01, 0x00, 0x00, 0x02};
static const int16_t GENI_RESPONSE_PREVENT_DISPLAY_SLEEP_OFFSET = 0;

// ---------------------------------------------------------------------------
// Start/stop control - CONFIRMED working on real ALPHA2 GO hardware
// (2026-09-01). GENIbus Class 3 (COMMANDS): single-byte command IDs that
// emulate pressing the pump's own physical remote/start/stop buttons. Frame
// layout and CRC match the already-verified read requests above:
//   [0x27][length][0xE7 dest][0xF8 source][0x03 class][0x80|N opspec][cmd.][crcH][crcL]
//
// Command IDs originated from an unmerged, buggy draft ESPHome PR
// (esphome/esphome#13003). That PR's own frame-building code had a real bug
// (dest/source swapped plus a spurious extra byte) - the frame format below
// is corrected (same math as the read requests, checked against known CRCs).
//
// Only START/STOP/REMOTE from that table are used. Operating mode,
// setpoints and settings are written as GENIbus Class 10 SET frames instead
// (decoded from Android HCI snoop captures of the Grundfos GO app) - see
// write_operating_mode() and friends in alpha2.cpp.
// ---------------------------------------------------------------------------

#ifdef USE_SWITCH
static const uint8_t GENI_CLASS_COMMANDS = 0x03;
static const uint8_t GENI_OPSPEC_SET = 0x80;  // OR'd with the command count

static const uint8_t GENI_CMD_STOP = 5;
static const uint8_t GENI_CMD_START = 6;
static const uint8_t GENI_CMD_REMOTE = 7;
#endif

class Alpha2 : public esphome::ble_client::BLEClientNode, public PollingComponent {
 public:
  void setup() override;
  void loop() override;
  void update() override;
  void gattc_event_handler(esp_gattc_cb_event_t event, esp_gatt_if_t gattc_if,
                           esp_ble_gattc_cb_param_t *param) override;
  void dump_config() override;
  void set_flow_sensor(sensor::Sensor *sensor) { this->flow_sensor_ = sensor; }
  void set_head_sensor(sensor::Sensor *sensor) { this->head_sensor_ = sensor; }
  void set_power_sensor(sensor::Sensor *sensor) { this->power_sensor_ = sensor; }
  void set_current_sensor(sensor::Sensor *sensor) { this->current_sensor_ = sensor; }
  void set_speed_sensor(sensor::Sensor *sensor) { this->speed_sensor_ = sensor; }
  void set_voltage_sensor(sensor::Sensor *sensor) { this->voltage_sensor_ = sensor; }
  void set_operating_mode_sensor(sensor::Sensor *sensor) { this->operating_mode_sensor_ = sensor; }
  void set_extern_control_sensor(sensor::Sensor *sensor) { this->extern_control_sensor_ = sensor; }
  void set_night_setback_sensor(sensor::Sensor *sensor) { this->night_setback_sensor_ = sensor; }
  void set_venting_status_sensor(sensor::Sensor *sensor) { this->venting_status_sensor_ = sensor; }
#ifdef USE_BINARY_SENSOR
  void set_venting_active_sensor(binary_sensor::BinarySensor *sensor) { this->venting_active_sensor_ = sensor; }
#endif
  void set_venting_countdown_sensor(sensor::Sensor *sensor) { this->venting_countdown_sensor_ = sensor; }
  void set_media_temp_sensor(sensor::Sensor *sensor) { this->media_temp_sensor_ = sensor; }
  void set_start_count_sensor(sensor::Sensor *sensor) { this->start_count_sensor_ = sensor; }
  // Alarm/warning codes (2026-11-12): active lists 58000b / 580000.
  void set_warning_code_sensor(sensor::Sensor *sensor) { this->warning_code_sensor_ = sensor; }
  void set_alarm_code_sensor(sensor::Sensor *sensor) { this->alarm_code_sensor_ = sensor; }
#ifdef USE_TEXT_SENSOR
  // Device info (ASCII) text sensors, found 2026-09-04 - see the Class 7
  // protocol comment above GENI_CLASS_DEVICE_INFO.
  void set_product_type_text_sensor(text_sensor::TextSensor *s) { this->product_type_text_sensor_ = s; }
  void set_product_no_text_sensor(text_sensor::TextSensor *s) { this->product_no_text_sensor_ = s; }
  void set_serial_no_text_sensor(text_sensor::TextSensor *s) { this->serial_no_text_sensor_ = s; }
  void set_production_code_text_sensor(text_sensor::TextSensor *s) { this->production_code_text_sensor_ = s; }
  void set_gsc_description_text_sensor(text_sensor::TextSensor *s) { this->gsc_description_text_sensor_ = s; }
  void set_gsc_identification_text_sensor(text_sensor::TextSensor *s) {
    this->gsc_identification_text_sensor_ = s;
  }
  void set_app_software_text_sensor(text_sensor::TextSensor *s) { this->app_software_text_sensor_ = s; }
  void set_ble_software_text_sensor(text_sensor::TextSensor *s) { this->ble_software_text_sensor_ = s; }
  void set_active_warning_text_sensor(text_sensor::TextSensor *s) { this->active_warning_text_sensor_ = s; }
  void set_active_alarm_text_sensor(text_sensor::TextSensor *s) { this->active_alarm_text_sensor_ = s; }
  void set_last_event_text_sensor(text_sensor::TextSensor *s) { this->last_event_text_sensor_ = s; }
#endif
#if defined(USE_SELECT) && defined(USE_SWITCH)
  void set_operating_mode_select(select::Select *sel) { this->operating_mode_select_ = sel; }
#endif

#ifdef USE_SWITCH
  void set_power_switch(Alpha2Switch *sw) { this->power_switch_ = sw; }
  void write_power(bool state);

  // Readback wiring (2026-09-04) - lets handle_geni_response_() push live
  // GET-response values into these entities' displayed state, not just
  // optimistic write-echo.
  void set_extern_control_switch(Alpha2ExternControlSwitch *sw) { this->extern_control_switch_ = sw; }
  void set_night_setback_switch(Alpha2NightSetbackSwitch *sw) { this->night_setback_switch_ = sw; }
  void set_min_flow_limit_switch(Alpha2MinFlowLimitSwitch *sw) { this->min_flow_limit_switch_ = sw; }
  void set_max_flow_limit_switch(Alpha2MaxFlowLimitSwitch *sw) { this->max_flow_limit_switch_ = sw; }
  void set_continuous_venting_switch(Alpha2ContinuousVentingSwitch *sw) { this->continuous_venting_switch_ = sw; }
  void set_panel_lock_switch(Alpha2PanelLockSwitch *sw) { this->panel_lock_switch_ = sw; }
  void set_prevent_display_sleep_switch(Alpha2PreventDisplaySleepSwitch *sw) {
    this->prevent_display_sleep_switch_ = sw;
  }
#if defined(USE_NUMBER)
  void set_pressure_setpoint_number(Alpha2PressureSetpointNumber *n) { this->pressure_setpoint_number_ = n; }
  void set_flow_setpoint_number(Alpha2FlowSetpointNumber *n) { this->flow_setpoint_number_ = n; }
  void set_speed_setpoint_number(Alpha2SpeedSetpointNumber *n) { this->speed_setpoint_number_ = n; }
  void set_min_flow_limit_number(Alpha2MinFlowLimitNumber *n) { this->min_flow_limit_number_ = n; }
  void set_max_flow_limit_number(Alpha2MaxFlowLimitNumber *n) { this->max_flow_limit_number_ = n; }
#endif

  // External control (register 5c0193) and night setback (register 5a0008)
  // on/off writes, found 2026-09-04 -.
  // NOTE: these two registers were swapped 2026-09-04 evening after the
  // original mapping (external control=5a0008, night setback=5c0193) was
  // found wrong - corrected via a live cross-check against the app.
  // External control: 2-byte value, OFF=0x0000/ON=0x7b00, both confirmed.
  // Night setback: simple 1-byte boolean, 1=on/0=off, confirmed.
  void write_extern_control(bool state);
  void write_night_setback(bool state);

  // Flow limitation on/off toggles (2026-09-04, registers 9b560259/
  // 9b560258) - enable/disable ONLY, echo back whatever value was
  // configured at capture time (~1.0/2.0 m3/h) - see the .cpp comments for
  // the "silently resets the limit" caveat.
  void write_min_flow_limit(bool state);
  void write_max_flow_limit(bool state);

  // Flow limitation VALUE-setting writes (2026-09-04, same registers as
  // above) - confirmed with exact 0.3/1.5 m3/h matches against real
  // captures. Forces enable=1.
  void write_min_flow_limit_value(float cubic_meters_per_hour);
  void write_max_flow_limit_value(float cubic_meters_per_hour);

  // "Continuous air detection and venting" toggle, register 90580263 -
  // CONFIRMED 2026-09-04 (0x03=ON, 0x02=OFF, matched a clean off-then-on
  // capture exactly). Note the same register also takes value 0x01 in a
  // different context (bundled with the one-shot manual venting START
  // sequence) - likely a general 3-state "venting mode" register, not a
  // pure boolean.
  void write_continuous_venting(bool state);

  // Panel lock (register 5e0002) and "prevent display sleep"
  // (register 5e0072) on/off writes, found 2026-09-04 -. Panel lock: simple 1-byte boolean. Prevent
  // display sleep: 2-byte value, only the high byte toggles.
  void write_panel_lock(bool state);
  void write_prevent_display_sleep(bool state);

  // Start/cancel manual venting, register 560009 - live-computed CRC
  // (2026-09-04, confirmed START and CANCEL differ in exactly one byte).
  // venting_type only matters when start=true - see write_venting()'s .cpp
  // comment. Default GENI_VENTING_TYPE_PUMP matches earlier (unlabeled)
  // tests; pass GENI_VENTING_TYPE_SYSTEM_PUMP for "System+Pump".
  void write_venting(bool start, uint8_t venting_type = GENI_VENTING_TYPE_PUMP);

  // General-purpose version (2026-09-04): builds a fresh 24-byte operating mode
  // SET frame for an arbitrary code with a live-computed CRC, instead of
  // replaying a fixed verbatim capture. See write_pressure_setpoint for the
  // same CRC-computation pattern, confirmed working on real hardware.
  void write_operating_mode(uint8_t code);

  // Real setpoint control (2026-09-04) - CRC verified to match the app's
  // own algorithm (calculate_crc_ below) against 5 real captured frames, so
  // this builds a fresh frame for an ARBITRARY target instead of replaying
  // a fixed one. Register 9b560012, DataObject 0x56 sub-index 0x12. meters:
  // target head/pressure setpoint in meters (raw = meters / 0.000102).
  void write_pressure_setpoint(float meters);

  // Same technique, register 9b560028 (DataObject 0x56 sub-index 0x28),
  // constant tail (0.6, 1.6, 0.4) instead of pressure's (0.5, 8.0, 0.0).
  // cubic_meters_per_hour: target flow setpoint in m³/h (raw = value/3600,
  // same conversion factor already used for the flow SENSOR elsewhere in
  // this file). Only meaningful while the pump is in Constant flow.
  void write_flow_setpoint(float cubic_meters_per_hour);

  // Found 2026-09-04, register 9b56000e (DataObject 0x56 sub-index 0x0e),
  // constant tail (1.0, 1.0, 1.0). SCALE UNCONFIRMED ON HARDWARE - assumed
  // raw = rpm directly (no conversion). Only meaningful while the pump is
  // in Constant curve.
  void write_speed_setpoint(float rpm);
#endif

 protected:
  sensor::Sensor *flow_sensor_{nullptr};
  sensor::Sensor *head_sensor_{nullptr};
  sensor::Sensor *power_sensor_{nullptr};
  sensor::Sensor *current_sensor_{nullptr};
  sensor::Sensor *speed_sensor_{nullptr};
  sensor::Sensor *voltage_sensor_{nullptr};
  sensor::Sensor *operating_mode_sensor_{nullptr};
  sensor::Sensor *extern_control_sensor_{nullptr};
  sensor::Sensor *night_setback_sensor_{nullptr};
  sensor::Sensor *venting_status_sensor_{nullptr};
#ifdef USE_BINARY_SENSOR
  binary_sensor::BinarySensor *venting_active_sensor_{nullptr};
#endif
  sensor::Sensor *venting_countdown_sensor_{nullptr};
  sensor::Sensor *media_temp_sensor_{nullptr};
  sensor::Sensor *start_count_sensor_{nullptr};
  // Alarm/warning handling (2026-11-12), see handle_event_response_().
  // Active-event lists: register 58000b (warnings) and 580000 (alarms; the
  // alarm/warning split is inferred - 58000b held the live warning 25
  // while 580000 stayed empty). Both answer with the SAME response type,
  // so only one of them is requested per update() and pending_event_list_
  // remembers which one is in flight.
  sensor::Sensor *warning_code_sensor_{nullptr};
  sensor::Sensor *alarm_code_sensor_{nullptr};
  bool handle_event_response_(const uint8_t *response, uint16_t length);
  void publish_event_list_(uint8_t list, const uint8_t *data, uint8_t data_len);
  uint32_t event_poll_counter_{0};
  uint8_t pending_event_list_{0};  // 1 = warnings (58000b), 2 = alarms (580000)
  uint16_t log_newest_seq_{0};      // newest event-log sequence number (5827 header)
  uint16_t log_fetch_seq_{0};       // entry still to fetch, 0 = none
  bool event_done_{false};
  uint8_t event_data_[32];
#ifdef USE_TEXT_SENSOR
  text_sensor::TextSensor *active_warning_text_sensor_{nullptr};
  text_sensor::TextSensor *active_alarm_text_sensor_{nullptr};
  text_sensor::TextSensor *last_event_text_sensor_{nullptr};
  text_sensor::TextSensor *product_type_text_sensor_{nullptr};
  text_sensor::TextSensor *product_no_text_sensor_{nullptr};
  text_sensor::TextSensor *serial_no_text_sensor_{nullptr};
  text_sensor::TextSensor *production_code_text_sensor_{nullptr};
  text_sensor::TextSensor *gsc_description_text_sensor_{nullptr};
  text_sensor::TextSensor *gsc_identification_text_sensor_{nullptr};
  text_sensor::TextSensor *app_software_text_sensor_{nullptr};
  text_sensor::TextSensor *ble_software_text_sensor_{nullptr};
  // Device info (Class 7) request/accumulation state - separate from the
  // Class 10 response_offset_/response_length_/response_type_ state above.
  bool awaiting_device_info_{false};
  uint8_t pending_device_info_field_{0};
  int16_t device_info_offset_{0};
  int16_t device_info_length_{0};
  uint8_t device_info_buffer_[GENI_DEVICE_INFO_BUFFER_LENGTH];
  // Only fetch device info every Nth update() cycle (2026-09-04) - these are
  // static values, don't need refreshing every 15s. See update() in
  // alpha2.cpp for the interval.
  uint32_t device_info_poll_counter_{0};
  // One-field-at-a-time state machine, driven from loop() (2026-09-04,
  // replacing a fixed delay()-per-request sequence in update()). A real
  // capture showed ALL 8 fields' responses arriving in one late batch when
  // fired with fixed 25-60ms delays - the actual BLE round-trip time varies
  // and isn't reliably boundable by a guessed constant, so this instead
  // waits for either a confirmed response (awaiting_device_info_ cleared)
  // or a timeout before sending the next field's request. See loop() in
  // alpha2.cpp.
  bool device_info_fetch_active_{false};
  uint8_t device_info_next_field_index_{0};
  uint32_t device_info_request_time_{0};
#endif
#if defined(USE_SELECT) && defined(USE_SWITCH)
  select::Select *operating_mode_select_{nullptr};
#endif
#ifdef USE_SWITCH
  Alpha2Switch *power_switch_{nullptr};
  Alpha2ExternControlSwitch *extern_control_switch_{nullptr};
  Alpha2NightSetbackSwitch *night_setback_switch_{nullptr};
  Alpha2MinFlowLimitSwitch *min_flow_limit_switch_{nullptr};
  Alpha2MaxFlowLimitSwitch *max_flow_limit_switch_{nullptr};
  Alpha2ContinuousVentingSwitch *continuous_venting_switch_{nullptr};
  Alpha2PanelLockSwitch *panel_lock_switch_{nullptr};
  Alpha2PreventDisplaySleepSwitch *prevent_display_sleep_switch_{nullptr};
#ifdef USE_NUMBER
  Alpha2PressureSetpointNumber *pressure_setpoint_number_{nullptr};
  Alpha2FlowSetpointNumber *flow_setpoint_number_{nullptr};
  Alpha2SpeedSetpointNumber *speed_setpoint_number_{nullptr};
  Alpha2MinFlowLimitNumber *min_flow_limit_number_{nullptr};
  Alpha2MaxFlowLimitNumber *max_flow_limit_number_{nullptr};
#endif
  // Set from the BLE GATT callback (ESP_GATTC_REG_FOR_NOTIFY_EVT), consumed
  // from loop() instead of calling reapply_last_command_() directly from
  // that callback. The Bluedroid GATT callback runs on the Bluetooth host
  // stack's own task with a small, fixed stack size - doing more BLE writes
  // and nested calls there (on top of what update() already does) risked
  // stack overflow/command-queue overload. Deferring to the main loop keeps
  // the callback itself minimal.
  bool pending_reapply_{false};
#endif
  // Explicit defaults: these are also (re)set on connect/disconnect, but an
  // uninitialized read here (e.g. a stray notify before the first connect)
  // is undefined behavior otherwise.
  uint16_t geni_handle_{0};
  int16_t response_length_{0};
  int16_t response_offset_{0};
  uint8_t response_type_[GENI_RESPONSE_TYPE_LENGTH];
  uint8_t buffer_[4];
  // Shared cross-fragment-safe 4-byte float decode, used by both
  // extract_publish_sensor_value_ and extract_publish_number_value_ below
  // (identical logic, only the target entity type differs) - factored out
  // 2026-09-04 to avoid duplicating it. Returns true (and fills *out_value)
  // once the full value has arrived; false if nothing to publish yet
  // (value not in this fragment, or still incomplete).
  bool extract_float_value_(const uint8_t *response, int16_t length, int16_t response_offset, int16_t value_offset,
                            float *out_value);
  void extract_publish_sensor_value_(const uint8_t *response, int16_t length, int16_t response_offset,
                                     int16_t value_offset, sensor::Sensor *sensor, float factor);
  void extract_publish_enum_sensor_value_(const uint8_t *response, int16_t length, int16_t response_offset,
                                          int16_t value_offset, sensor::Sensor *sensor);
  void extract_publish_uint16_sensor_value_(const uint8_t *response, int16_t length, int16_t response_offset,
                                            int16_t value_offset, sensor::Sensor *sensor);
#if defined(USE_NUMBER) && defined(USE_SWITCH)
  // Same float-extraction logic as extract_publish_sensor_value_ above, but
  // targeting a number::Number (all our Alpha2*SetpointNumber/*LimitNumber
  // subclasses share that base, so one method covers all of them). Used for
  // GET-response readback (2026-09-04) so the number sliders show the
  // pump's actual current value on boot/reconnect, not just optimistic
  // write-echo. `decimals` rounds the published value (2026-09-04) - unlike
  // sensor::Sensor's accuracy_decimals, number::Number has no separate
  // display-rounding hint, so the raw float32-converted-from-the-pump value
  // (things like 2.00016 instead of 2.0) would otherwise show as-is.
  void extract_publish_number_value_(const uint8_t *response, int16_t length, int16_t response_offset,
                                     int16_t value_offset, number::Number *entity, float factor, int decimals);
#endif
#ifdef USE_SWITCH
  // Single-byte boolean readback for switch::Switch entities (2026-09-04) -
  // same purpose as extract_publish_number_value_ above.
  void extract_publish_switch_value_(const uint8_t *response, int16_t length, int16_t response_offset,
                                     int16_t value_offset, switch_::Switch *entity);
#endif
  void handle_geni_response_(const uint8_t *response, uint16_t length);
#ifdef USE_TEXT_SENSOR
  void handle_device_info_response_(const uint8_t *response, uint16_t length);
  void request_device_info_(uint8_t field_id);
#endif
  void send_request_(uint8_t *request, size_t len);
  bool is_current_response_type_(const uint8_t *response_type);
#ifdef USE_SWITCH
  // We can't read the pump's actual current on/off state back (no known
  // GENI register for it). As a practical substitute, re-send the switch's
  // last known state as soon as the BLE connection (re)establishes - relies
  // on the switch's own restore_mode (configured in YAML) to have the right
  // value after a fresh boot. Runs on every reconnect, not just cold boot,
  // since a dropped-and-recovered BLE link leaves us equally unsure of the
  // pump's actual state.
  void reapply_last_command_();
  uint16_t calculate_crc_(const uint8_t *data, size_t len);
  void send_commands_(const uint8_t *command_ids, size_t num_commands);
  // Shared tail for every write_*() below (2026-09-04): computes the live
  // CRC over frame[1 . total_len-3], writes it into the frame's last 2
  // bytes, then sends the frame - split into two BLE writes if it's more
  // than `first_chunk_len` bytes (matching the app's own MTU fragmentation),
  // or as one write if first_chunk_len == total_len. Factored out to avoid
  // repeating this same 4-line CRC+split pattern in ~15 write_*() functions.
  void finalize_and_send_(uint8_t *frame, size_t total_len, size_t first_chunk_len);
#endif
};

// Reports back whatever state we last wrote (optimistic), since we don't
// decode the pump's GENI acknowledgement for these commands.
#ifdef USE_SWITCH
class Alpha2Switch : public switch_::Switch {
 public:
  void set_parent(Alpha2 *parent) { parent_ = parent; }

 protected:
  void write_state(bool state) override;
  Alpha2 *parent_{nullptr};
};

// External control and night setback on/off switches (2026-09-04) - same
// optimistic pattern as Alpha2Switch above (no GENI readback tied to these,
// just report back whatever we last wrote). See write_extern_control()/
// write_night_setback() in alpha2.cpp for the frame details and confidence
// notes (external control confirmed, night setback's ON value is a guess).
class Alpha2ExternControlSwitch : public switch_::Switch {
 public:
  void set_parent(Alpha2 *parent) { parent_ = parent; }

 protected:
  void write_state(bool state) override {
    this->parent_->write_extern_control(state);
    this->publish_state(state);
  }
  Alpha2 *parent_{nullptr};
};

class Alpha2NightSetbackSwitch : public switch_::Switch {
 public:
  void set_parent(Alpha2 *parent) { parent_ = parent; }

 protected:
  void write_state(bool state) override {
    this->parent_->write_night_setback(state);
    this->publish_state(state);
  }
  Alpha2 *parent_{nullptr};
};

// Flow-limitation on/off switches (2026-09-04) - see write_min_flow_limit()/
// write_max_flow_limit() for the "echoes back an unchangeable value" caveat.
class Alpha2MinFlowLimitSwitch : public switch_::Switch {
 public:
  void set_parent(Alpha2 *parent) { parent_ = parent; }

 protected:
  void write_state(bool state) override {
    this->parent_->write_min_flow_limit(state);
    this->publish_state(state);
  }
  Alpha2 *parent_{nullptr};
};

class Alpha2MaxFlowLimitSwitch : public switch_::Switch {
 public:
  void set_parent(Alpha2 *parent) { parent_ = parent; }

 protected:
  void write_state(bool state) override {
    this->parent_->write_max_flow_limit(state);
    this->publish_state(state);
  }
  Alpha2 *parent_{nullptr};
};

class Alpha2ContinuousVentingSwitch : public switch_::Switch {
 public:
  void set_parent(Alpha2 *parent) { parent_ = parent; }

 protected:
  void write_state(bool state) override {
    this->parent_->write_continuous_venting(state);
    this->publish_state(state);
  }
  Alpha2 *parent_{nullptr};
};

// Panel lock and "prevent display sleep" switches (2026-09-04) - GET
// readback wired (see handle_geni_response_()), same pattern as
// Alpha2NightSetbackSwitch. See write_panel_lock()/write_prevent_display_
// sleep() in alpha2.cpp for the frame details.
class Alpha2PanelLockSwitch : public switch_::Switch {
 public:
  void set_parent(Alpha2 *parent) { parent_ = parent; }

 protected:
  void write_state(bool state) override {
    this->parent_->write_panel_lock(state);
    this->publish_state(state);
  }
  Alpha2 *parent_{nullptr};
};

class Alpha2PreventDisplaySleepSwitch : public switch_::Switch {
 public:
  void set_parent(Alpha2 *parent) { parent_ = parent; }

 protected:
  void write_state(bool state) override {
    this->parent_->write_prevent_display_sleep(state);
    this->publish_state(state);
  }
  Alpha2 *parent_{nullptr};
};
#endif

// Operating mode select (2026-09-04) - replaces the earlier per-mode TEST
// buttons. Options map to codes via alpha2_operating_mode_code_for_label_()/
// alpha2_operating_mode_label_for_code_() in alpha2.cpp, shared with the
// automatic state readback in handle_geni_response_() so the select stays
// in sync with the pump's actual reported mode, not just optimistic.
#if defined(USE_SELECT) && defined(USE_SWITCH)
class Alpha2OperatingModeSelect : public select::Select, public Component {
 public:
  void set_parent(Alpha2 *parent) { parent_ = parent; }

 protected:
  void control(const std::string &value) override;
  Alpha2 *parent_{nullptr};
};
#endif

// number entity for the pressure/head setpoint (2026-09-04) - see
// write_pressure_setpoint() above. Requires USE_SWITCH to be enabled too,
// since it depends on send_commands_/calculate_crc_ which live in that
// block - fine for this single-device component (switch: is always
// configured here), not meant to be generally decoupled.
#if defined(USE_NUMBER) && defined(USE_SWITCH)
class Alpha2PressureSetpointNumber : public number::Number, public Component {
 public:
  void set_parent(Alpha2 *parent) { parent_ = parent; }

 protected:
  void control(float value) override {
    this->parent_->write_pressure_setpoint(value);
    this->publish_state(value);
  }
  Alpha2 *parent_{nullptr};
};

class Alpha2FlowSetpointNumber : public number::Number, public Component {
 public:
  void set_parent(Alpha2 *parent) { parent_ = parent; }

 protected:
  void control(float value) override {
    this->parent_->write_flow_setpoint(value);
    this->publish_state(value);
  }
  Alpha2 *parent_{nullptr};
};

class Alpha2SpeedSetpointNumber : public number::Number, public Component {
 public:
  void set_parent(Alpha2 *parent) { parent_ = parent; }

 protected:
  void control(float value) override {
    this->parent_->write_speed_setpoint(value);
    this->publish_state(value);
  }
  Alpha2 *parent_{nullptr};
};

class Alpha2MinFlowLimitNumber : public number::Number, public Component {
 public:
  void set_parent(Alpha2 *parent) { parent_ = parent; }

 protected:
  void control(float value) override {
    this->parent_->write_min_flow_limit_value(value);
    this->publish_state(value);
  }
  Alpha2 *parent_{nullptr};
};

class Alpha2MaxFlowLimitNumber : public number::Number, public Component {
 public:
  void set_parent(Alpha2 *parent) { parent_ = parent; }

 protected:
  void control(float value) override {
    this->parent_->write_max_flow_limit_value(value);
    this->publish_state(value);
  }
  Alpha2 *parent_{nullptr};
};
#endif

}  // namespace alpha2
}  // namespace esphome

#endif
