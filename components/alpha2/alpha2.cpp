//original code from https://github.com/esphome/esphome/blob/dev/esphome/components/alpha3/
//I just figured out the new request "command" bytes and response type bytes

#include "alpha2.h"
#include "esphome/core/log.h"
#include "esphome/core/application.h"
#include <cstring>
#include <lwip/sockets.h>  //gives ntohl

#ifdef USE_ESP32

namespace esphome {
namespace alpha2 {

static const char *const TAG = "alpha2";

void Alpha2::dump_config() {
  ESP_LOGCONFIG(TAG, "ALPHA2");
  LOG_SENSOR(" ", "Flow", this->flow_sensor_);
  LOG_SENSOR(" ", "Head", this->head_sensor_);
  LOG_SENSOR(" ", "Power", this->power_sensor_);
  LOG_SENSOR(" ", "Current", this->current_sensor_);
  LOG_SENSOR(" ", "Speed", this->speed_sensor_);
  LOG_SENSOR(" ", "Voltage", this->voltage_sensor_);
  LOG_SENSOR(" ", "Operating mode", this->operating_mode_sensor_);
  LOG_SENSOR(" ", "Extern kontroll", this->extern_control_sensor_);
  LOG_SENSOR(" ", "Night setback", this->night_setback_sensor_);
  LOG_SENSOR(" ", "Avluftningsstatus", this->venting_status_sensor_);
#ifdef USE_BINARY_SENSOR
  LOG_BINARY_SENSOR(" ", "Venting active", this->venting_active_sensor_);
#endif
  LOG_SENSOR(" ", "Venting countdown", this->venting_countdown_sensor_);
  LOG_SENSOR(" ", "Medietemperatur", this->media_temp_sensor_);
  LOG_SENSOR(" ", "Antal starter", this->start_count_sensor_);
#ifdef USE_SWITCH
  LOG_SWITCH(" ", "Power switch", this->power_switch_);
#endif
}

void Alpha2::setup() {}

void Alpha2::loop() {
#ifdef USE_SWITCH
  if (this->pending_reapply_) {
    this->pending_reapply_ = false;
    this->reapply_last_command_();
  }
#endif
#ifdef USE_TEXT_SENSOR
  // Device info one-field-at-a-time state machine (2026-09-04) - see the
  // device_info_fetch_active_ comment in alpha2.h for why this replaced a
  // fixed delay()-per-request sequence in update().
  if (this->device_info_fetch_active_) {
    uint32_t now = millis();
    // 800ms (2026-09-04, was 400) - a real log still showed occasional
    // mismatches with 400ms, most likely the connection still settling
    // right after boot/reconnect (this fetch's first window fires on the
    // very first update() cycle). Giving up too early on a field whose
    // response is just running late causes exactly that stray-late-arrival
    // mismatch, so err generous - this only runs once every 20 cycles, so
    // a slower timeout here costs nothing in practice.
    bool timed_out = this->awaiting_device_info_ && (now - this->device_info_request_time_ > 800);
    if (timed_out) {
      // Give up on this one field - it'll be retried on the next fetch
      // window (every 20th update() cycle). Not indicative of a fault, see
      // the comment above.
      this->awaiting_device_info_ = false;
    }
    if (!this->awaiting_device_info_) {
      if (this->device_info_next_field_index_ >= GENI_DEVICE_INFO_FIELD_COUNT) {
        this->device_info_fetch_active_ = false;
      } else {
        uint8_t field = GENI_DEVICE_INFO_FIELD_ORDER[this->device_info_next_field_index_++];
        this->device_info_request_time_ = now;
        this->request_device_info_(field);
      }
    }
  }
#endif
}

bool Alpha2::extract_float_value_(const uint8_t *response, int16_t length, int16_t response_offset,
                                  int16_t value_offset, float *out_value) {
  // we need to handle cases where a value is split over two packets
  const int16_t value_length = 4;  // 32bit float
  // offset inside current response packet
  int16_t rel_offset = value_offset - response_offset;
  if (rel_offset <= -value_length)
    return false;  // aready passed the value completly
  if (rel_offset >= length)
    return false;  // value not in this packet

  auto start_offset = std::max((int16_t) 0, rel_offset);
  auto end_offset = std::min((int16_t) (rel_offset + value_length), length);
  auto copy_length = end_offset - start_offset;
  auto buffer_offset = std::max((int16_t) (-rel_offset), (int16_t) 0);
  std::memcpy(this->buffer_ + buffer_offset, response + start_offset, copy_length);

  if (rel_offset + value_length > length)
    return false;  // don't have the whole value yet

  void *buffer = this->buffer_;                          // to prevent warnings when casting the pointer
  *((int32_t *) buffer) = ntohl(*((int32_t *) buffer));  // values are big endian
  *out_value = *((float *) buffer);
  return true;
}

void Alpha2::extract_publish_sensor_value_(const uint8_t *response, int16_t length, int16_t response_offset,
                                           int16_t value_offset, sensor::Sensor *sensor, float factor) {
  if (sensor == nullptr)
    return;
  float fvalue;
  if (this->extract_float_value_(response, length, response_offset, value_offset, &fvalue))
    sensor->publish_state(fvalue * factor);
}

void Alpha2::extract_publish_enum_sensor_value_(const uint8_t *response, int16_t length, int16_t response_offset,
                                                int16_t value_offset, sensor::Sensor *sensor) {
  if (sensor == nullptr)
    return;
  // Single-byte value (unlike extract_publish_sensor_value_'s 4-byte float) -
  // used for the operating mode code, which is one raw byte, not a
  // measurement. Same split-across-packets handling isn't needed for a
  // single byte, but the same offset math is kept for consistency.
  auto rel_offset = value_offset - response_offset;
  if (rel_offset < 0 || rel_offset >= length)
    return;
  sensor->publish_state(response[rel_offset]);
}

void Alpha2::extract_publish_uint16_sensor_value_(const uint8_t *response, int16_t length, int16_t response_offset,
                                                   int16_t value_offset, sensor::Sensor *sensor) {
  if (sensor == nullptr)
    return;
  // Two-byte big-endian value (night setback register 5c0193) - no
  // cross-packet handling, these responses are short enough to always
  // arrive whole in one notification.
  auto rel_offset = value_offset - response_offset;
  if (rel_offset < 0 || rel_offset + 1 >= length)
    return;
  uint16_t value = (static_cast<uint16_t>(response[rel_offset]) << 8) | response[rel_offset + 1];
  sensor->publish_state(value);
}

#if defined(USE_NUMBER) && defined(USE_SWITCH)
void Alpha2::extract_publish_number_value_(const uint8_t *response, int16_t length, int16_t response_offset,
                                           int16_t value_offset, number::Number *entity, float factor,
                                           int decimals) {
  if (entity == nullptr)
    return;
  float fvalue;
  if (this->extract_float_value_(response, length, response_offset, value_offset, &fvalue)) {
    float scale = powf(10.0F, decimals);
    entity->publish_state(roundf(fvalue * factor * scale) / scale);
  }
}
#endif

#ifdef USE_SWITCH
void Alpha2::extract_publish_switch_value_(const uint8_t *response, int16_t length, int16_t response_offset,
                                           int16_t value_offset, switch_::Switch *entity) {
  if (entity == nullptr)
    return;
  auto rel_offset = value_offset - response_offset;
  if (rel_offset < 0 || rel_offset >= length)
    return;
  entity->publish_state(response[rel_offset] != 0);
}
#endif

#if defined(USE_SELECT) && defined(USE_SWITCH)
// Operating mode code<->label mapping, shared between the select entity's
// control() (label -> code, to write) and handle_geni_response_ (code ->
// label, to keep the select in sync with what the pump reports).
struct OperatingModeOption {
  const char *label;
  uint8_t code;
};
static const OperatingModeOption ALPHA2_OPERATING_MODE_OPTIONS[] = {
    {"Constant pressure", 0x00},
    {"Proportional pressure", 0x01},
    {"Constant curve", 0x02},
    {"Constant flow", 0x08},
    {"Proportional pressure + AutoAdapt", 0x0d},
    {"Constant pressure + AutoAdapt", 0x0e},
};

static const char *alpha2_operating_mode_label_for_code_(uint8_t code) {
  for (const auto &opt : ALPHA2_OPERATING_MODE_OPTIONS) {
    if (opt.code == code)
      return opt.label;
  }
  return nullptr;
}

static bool alpha2_operating_mode_code_for_label_(const std::string &label, uint8_t *code) {
  for (const auto &opt : ALPHA2_OPERATING_MODE_OPTIONS) {
    if (label == opt.label) {
      *code = opt.code;
      return true;
    }
  }
  return false;
}

void Alpha2OperatingModeSelect::control(const std::string &value) {
  uint8_t code;
  if (!alpha2_operating_mode_code_for_label_(value, &code)) {
    ESP_LOGW(TAG, "Unknown operating mode option: %s", value.c_str());
    return;
  }
  this->parent_->write_operating_mode(code);
  this->publish_state(value);
}
#endif

bool Alpha2::is_current_response_type_(const uint8_t *response_type) {
  return !std::memcmp(this->response_type_, response_type, GENI_RESPONSE_TYPE_LENGTH);
}


// ---------------------------------------------------------------------------
// Status, presets and extra values (2026-11-12). Object layouts follow
// Birkemosen/esphome-alpha2go's docs/ble-traffic.md (GPL-3.0) and were
// checked against this project's own HCI captures.
// ---------------------------------------------------------------------------
static float alpha2_be_float_(const uint8_t *p) {
  uint32_t bits = (uint32_t) p[0] << 24 | (uint32_t) p[1] << 16 | (uint32_t) p[2] << 8 | p[3];
  float f;
  std::memcpy(&f, &bits, sizeof(f));
  return f;
}
static float alpha2_celsius_(float v) { return v > 200.0F ? v - 273.15F : v; }
static const float ALPHA2_PA_TO_M = 0.000102F;  // 1 m head = 9804 Pa

#ifdef USE_TEXT_SENSOR
static const char *alpha2_operation_mode_text_(uint8_t v) {
  switch (v) {
    case 0: return "Auto";
    case 1: return "Stop";
    case 2: return "Min curve";
    case 3: return "Max curve";
    case 4: return "User defined";
    case 5: return "Forced start";
    case 6: return "No command";
    case 7: return "Hand";
    case 8: return "Standby";
    case 9: return "Stop without delay";
    case 10: return "Emergency run";
    default: return "Unknown";
  }
}
static const char *alpha2_control_mode_text_(uint8_t v) {
  switch (v) {
    case 0: return "Constant pressure";
    case 1: return "Proportional pressure";
    case 2: return "Constant curve";
    case 5: return "AutoAdapt";
    case 8: return "Constant flow";
    case 13: return "Proportional pressure + AutoAdapt";
    case 14: return "Constant pressure + AutoAdapt";
    case 15: return "AutoAdapt radiator and underfloor";
    case 254: return "None";
    default: return "Unknown";
  }
}
static const char *alpha2_control_source_text_(uint8_t v) {
  switch (v) {
    case 1: return "Local (panel or app)";
    case 2: return "Bus";
    case 3: return "Link";
    case 4: return "External IO";
    case 5: return "Off button";
    case 8: return "Event action";
    case 104: return "AutoNight";
    case 111: return "Air venting";
    case 123: return "PWM";
    case 127: return "Temporary operation control";
    default: return "Unknown";
  }
}
#endif

static bool alpha2_is_autoadapt_(uint8_t control_mode) {
  return control_mode == 5 || control_mode == 13 || control_mode == 14 || control_mode == 15;
}

// Panel setting options; index 0..8 = presets 86/351..359.
static const char *const ALPHA2_PANEL_OPTIONS[] = {
    "Constant curve I",        "Constant curve II",        "Constant curve III",
    "Constant pressure I",     "Constant pressure II",     "Constant pressure III",
    "Proportional pressure I", "Proportional pressure II", "Proportional pressure III",
    "Constant pressure + AutoAdapt", "Proportional pressure + AutoAdapt", "Advanced",
};
static const uint8_t ALPHA2_PANEL_OPTION_COUNT = 12;

// Controller tails of the 86/x setpoint objects, as the app writes them.
static const uint8_t ALPHA2_TAIL_CP[12] = {0x3f, 0x19, 0x99, 0x9a, 0x3f, 0x99, 0x99, 0x9a, 0x00, 0x00, 0x00, 0x00};
static const uint8_t ALPHA2_TAIL_PP[12] = {0x3f, 0x00, 0x00, 0x00, 0x41, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00};
static const uint8_t ALPHA2_TAIL_CC[12] = {0x3f, 0x80, 0x00, 0x00, 0x3f, 0x80, 0x00, 0x00, 0x3f, 0x80, 0x00, 0x00};
static const uint16_t ALPHA2_PRESET_CRC[9] = {0x3db7, 0xfa0b, 0xea2a, 0xda49, 0xca68,
                                              0xba8f, 0xaaae, 0x9acd, 0x8aec};

void Alpha2::handle_complete_response_(uint8_t data_len) {
  const uint8_t *t = this->response_type_;
  const uint8_t *d = this->resp_data_;
  if (this->is_current_response_type_(GENI_RESPONSE_TYPE_FLOW_HEAD) && data_len >= 36) {
    // 93/290: [flow m3/s][diff pressure Pa][?][?][media T][diff T][flow m3/h][head m][Kv]
    if (this->flow_coefficient_kv_sensor_ != nullptr)
      this->flow_coefficient_kv_sensor_->publish_state(alpha2_be_float_(d + 32));
    if (this->differential_temperature_sensor_ != nullptr)
      this->differential_temperature_sensor_->publish_state(alpha2_be_float_(d + 20));
  } else if (this->is_current_response_type_(GENI_RESPONSE_TYPE_POWER) && data_len >= 32) {
    // 87/69: [grid V][DC link V][current][power][?][speed][converter T][inverter T]
    if (this->dc_link_voltage_sensor_ != nullptr)
      this->dc_link_voltage_sensor_->publish_state(alpha2_be_float_(d + 4));
    if (this->converter_temperature_sensor_ != nullptr)
      this->converter_temperature_sensor_->publish_state(alpha2_celsius_(alpha2_be_float_(d + 24)));
    if (this->inverter_temperature_sensor_ != nullptr)
      this->inverter_temperature_sensor_->publish_state(alpha2_celsius_(alpha2_be_float_(d + 28)));
  } else if (this->is_current_response_type_(GENI_RESPONSE_TYPE_OPERATING_MODE) && data_len >= 7) {
    // Operation request objects: [source][operation mode][control mode][set point float].
    uint8_t source = d[0];
    if (source == 0x00 && d[1] == GENI_DISCRIMINATOR_OPERATING_MODE) {
      // 86/10 local control mode
      this->local_control_mode_ = d[2];
      this->update_panel_setting_state_();
    } else if (source != 0x00 && source != GENI_DISCRIMINATOR_VENTING_ACTIVE) {
      // 86/8 realized request - what the pump runs right now
      uint8_t op_mode = d[1];
      uint8_t control_mode = d[2];
      float sp = alpha2_be_float_(d + 3);
      ESP_LOGD(TAG, "[%s] REALIZED REQUEST source %u op %u control %u sp %.4g", this->parent_->address_str(), source,
               op_mode, control_mode, sp);
#ifdef USE_TEXT_SENSOR
      if (this->control_source_text_sensor_ != nullptr)
        this->control_source_text_sensor_->publish_state(alpha2_control_source_text_(source));
      if (this->operation_mode_text_sensor_ != nullptr)
        this->operation_mode_text_sensor_->publish_state(alpha2_operation_mode_text_(op_mode));
      if (this->control_mode_text_sensor_ != nullptr)
        this->control_mode_text_sensor_->publish_state(alpha2_control_mode_text_(control_mode));
#endif
#ifdef USE_BINARY_SENSOR
      if (this->autoadapt_sensor_ != nullptr)
        this->autoadapt_sensor_->publish_state(alpha2_is_autoadapt_(control_mode));
#endif
      bool pressure_mode = control_mode == 0 || control_mode == 1 || alpha2_is_autoadapt_(control_mode);
      if (this->setpoint_head_sensor_ != nullptr)
        this->setpoint_head_sensor_->publish_state(pressure_mode ? sp * ALPHA2_PA_TO_M : NAN);
      if (this->setpoint_speed_sensor_ != nullptr)
        this->setpoint_speed_sensor_->publish_state(control_mode == 2 ? sp : NAN);
      if (this->setpoint_flow_sensor_ != nullptr)
        this->setpoint_flow_sensor_->publish_state(control_mode == 8 ? sp * 3600.0F : NAN);
#if defined(USE_SELECT) && defined(USE_SWITCH)
      if (this->curve_select_ != nullptr)
        this->curve_select_->publish_state(op_mode == 2 ? "Min curve" : (op_mode == 3 ? "Max curve" : "Normal"));
#endif
    }
  } else if (this->is_current_response_type_(GENI_RESPONSE_TYPE_SETPOINT) && data_len >= 6) {
    float raw = alpha2_be_float_(d + 2);
    if (d[1] == GENI_SETPOINT_DISCRIMINATOR_PRESSURE) {
      if (this->pending_pressure_ == 1) {
        this->cp_setpoint_raw_ = raw;
#if defined(USE_NUMBER) && defined(USE_SWITCH)
        if (this->const_pressure_setpoint_number_ != nullptr)
          this->const_pressure_setpoint_number_->publish_state(roundf(raw * ALPHA2_PA_TO_M * 10.0F) / 10.0F);
#endif
      } else if (this->pending_pressure_ == 2) {
        this->pp_setpoint_raw_ = raw;
#if defined(USE_NUMBER) && defined(USE_SWITCH)
        if (this->pressure_setpoint_number_ != nullptr)
          this->pressure_setpoint_number_->publish_state(roundf(raw * ALPHA2_PA_TO_M * 10.0F) / 10.0F);
#endif
      }
      this->pending_pressure_ = 0;
      this->update_panel_setting_state_();
    } else if (d[1] == GENI_SETPOINT_DISCRIMINATOR_SPEED) {
      this->cc_setpoint_raw_ = raw;
      this->update_panel_setting_state_();
    }
  } else if (t[1] == 0x00 && t[2] == 0x03 && t[3] == 0x65 && t[4] == 0x01 && data_len >= 5) {
    // 86/351..359 panel presets: [control mode][set point float]
    if (this->pending_preset_ >= 0 && this->pending_preset_ < 9) {
      this->preset_mode_[this->pending_preset_] = d[0];
      this->preset_raw_[this->pending_preset_] = alpha2_be_float_(d + 1);
      this->preset_valid_ |= 1 << this->pending_preset_;
      ESP_LOGD(TAG, "[%s] PRESET %d: mode %u value %.1f", this->parent_->address_str(), this->pending_preset_, d[0],
               this->preset_raw_[this->pending_preset_]);
      this->update_panel_setting_state_();
    }
    this->pending_preset_ = -1;
  }
}

void Alpha2::update_panel_setting_state_() {
#if defined(USE_SELECT) && defined(USE_SWITCH)
  if (this->panel_setting_select_ == nullptr || this->local_control_mode_ == 0xff)
    return;
  const char *label = "Advanced";
  uint8_t mode = this->local_control_mode_;
  if (mode == 14) {
    label = "Constant pressure + AutoAdapt";
  } else if (mode == 13) {
    label = "Proportional pressure + AutoAdapt";
  } else if (mode == 0 || mode == 1 || mode == 2) {
    float current = mode == 0 ? this->cp_setpoint_raw_ : (mode == 1 ? this->pp_setpoint_raw_ : this->cc_setpoint_raw_);
    if (std::isnan(current))
      return;  // wait until the matching setpoint has been read
    for (uint8_t i = 0; i < 9; i++) {
      if (!(this->preset_valid_ & (1 << i)) || this->preset_mode_[i] != mode)
        continue;
      if (fabsf(current - this->preset_raw_[i]) <= fabsf(this->preset_raw_[i]) * 0.005F) {
        label = ALPHA2_PANEL_OPTIONS[i];
        break;
      }
    }
  }
  if (label != this->last_panel_label_) {
    this->last_panel_label_ = label;
    this->panel_setting_select_->publish_state(label);
  }
#endif
}

bool Alpha2::need_pressure_poll_() const {
#if defined(USE_NUMBER) && defined(USE_SWITCH)
  if (this->pressure_setpoint_number_ != nullptr || this->const_pressure_setpoint_number_ != nullptr)
    return true;
#endif
  return this->panel_select_configured_();
}

bool Alpha2::panel_select_configured_() const {
#if defined(USE_SELECT) && defined(USE_SWITCH)
  return this->panel_setting_select_ != nullptr;
#else
  return false;
#endif
}

bool Alpha2::need_status_poll_() const {
  bool need = this->setpoint_head_sensor_ != nullptr || this->setpoint_speed_sensor_ != nullptr ||
              this->setpoint_flow_sensor_ != nullptr;
#ifdef USE_TEXT_SENSOR
  need = need || this->control_source_text_sensor_ != nullptr || this->operation_mode_text_sensor_ != nullptr ||
         this->control_mode_text_sensor_ != nullptr;
#endif
#ifdef USE_BINARY_SENSOR
  need = need || this->autoadapt_sensor_ != nullptr;
#endif
#if defined(USE_SELECT) && defined(USE_SWITCH)
  need = need || this->curve_select_ != nullptr;
#endif
  return need;
}

// ---------------------------------------------------------------------------
// Alarms, warnings and event log (2026-11-12). Decoded from an HCI capture
// of Grundfos GO's "Events" screen with a live code 25 (wrong PWM config)
// and a log of 25/35 entries whose operating-hour timestamps matched the
// app screenshots exactly. Codes/texts from the ALPHA2 GO I/O manual
// (93074263, chapter 11).
// ---------------------------------------------------------------------------
static const char *alpha2_event_text_(uint16_t code) {
  switch (code) {
    case 4: return "Overvoltage";
    case 25: return "Incorrect PWM configuration";
    case 29: return "Forced pumping (alarm)";
    case 35: return "Air in pump head";
    case 40: return "Undervoltage";
    case 43: return "Forced pumping (warning)";
    case 51: return "Blocked pump";
    case 57: return "Dry running";
    case 72: case 76: case 85: return "Internal fault";
    case 132: return "Pump configuration corrupted or missing";
    default: return "Unknown code";
  }
}

static std::string alpha2_format_event_(uint16_t code) {
  char buf[64];
  snprintf(buf, sizeof(buf), "%u – %s", code, alpha2_event_text_(code));
  return std::string(buf);
}

// Event log request frames: register 5827, index 0xd7 = header,
// 0xd8.0xeb = 20-entry ring buffer. CRCs precomputed (CRC-16-CCITT as in
// calculate_crc_(), verified against captured app requests).
static const uint8_t GENI_EVENT_LOG_HEADER_INDEX = 0xd7;
static const uint8_t GENI_EVENT_LOG_FIRST_INDEX = 0xd8;
static const uint8_t GENI_EVENT_LOG_SIZE = 20;
static const uint16_t GENI_EVENT_LOG_ENTRY_CRC[GENI_EVENT_LOG_SIZE] = {
    0x6b99, 0x7bb8, 0x4bdb, 0x5bfa, 0x2b1d, 0x3b3c, 0x0b5f, 0x1b7e, 0xdcc2, 0xcce3,
    0xfc80, 0xeca1, 0x9c46, 0x8c67, 0xbc04, 0xac25, 0x5dca, 0x4deb, 0x7d88, 0x6da9};

void Alpha2::publish_event_list_(uint8_t list, const uint8_t *data, uint8_t data_len) {
  // Layout seen: [count u16][code u16][0000]. With count=0 only [0000].
  // Multiple simultaneous events not yet observed - parsed as consecutive
  // u16 codes after the count, zeros skipped.
  uint16_t count = data_len >= 2 ? (data[0] << 8 | data[1]) : 0;
  uint16_t first = 0;
  std::string text;
  for (uint8_t i = 2; i + 1 < data_len && count > 0; i += 2) {
    uint16_t code = data[i] << 8 | data[i + 1];
    if (code == 0)
      continue;
    if (first == 0)
      first = code;
    if (!text.empty())
      text += ", ";
    text += alpha2_format_event_(code);
    count--;
  }
  ESP_LOGD(TAG, "[%s] %s list: first code %u", this->parent_->address_str(), list == 1 ? "WARNING" : "ALARM", first);
  sensor::Sensor *code_sensor = list == 1 ? this->warning_code_sensor_ : this->alarm_code_sensor_;
  if (code_sensor != nullptr)
    code_sensor->publish_state(first);
#ifdef USE_TEXT_SENSOR
  text_sensor::TextSensor *text_sensor = list == 1 ? this->active_warning_text_sensor_ : this->active_alarm_text_sensor_;
  if (text_sensor != nullptr)
    text_sensor->publish_state(text.empty() ? std::string("None") : text);
#endif
}

bool Alpha2::handle_event_response_(const uint8_t *response, uint16_t length) {
  const uint8_t *t = this->response_type_;
  uint8_t kind;
  if (t[1] == 0x00 && t[2] == 0x02 && t[3] == 0x3a && t[4] == 0x01) {
    kind = 1;  // active event list (58000b or 580000)
  } else if (t[1] == 0x00 && t[2] == 0x00 && t[3] == 0xf3 && t[4] == 0x01) {
    kind = 2;  // event log header (5827 d7)
  } else if (t[1] == 0x00 && t[2] == 0x00 && t[3] == 0xf4 && t[4] == 0x02) {
    kind = 3;  // event log entry (5827 d8.eb)
  } else {
    return false;
  }
  (void) response;
  uint8_t data_len = std::min<uint8_t>(t[7], sizeof(this->resp_data_));
  if (this->response_offset_ + (int16_t) length < (int16_t) data_len ||
      this->response_offset_ >= (int16_t) data_len)
    return true;  // wait for the remaining fragment(s) / trailing CRC fragment
  const uint8_t *d = this->resp_data_;

  if (kind == 1) {
    if (this->pending_event_list_ != 0)
      this->publish_event_list_(this->pending_event_list_, d, data_len);
    this->pending_event_list_ = 0;
  } else if (kind == 2 && data_len >= 2) {
    uint16_t newest = d[0] << 8 | d[1];
    ESP_LOGD(TAG, "[%s] EVENT LOG header: newest seq %u", this->parent_->address_str(), newest);
    if (newest != 0 && newest != this->log_newest_seq_) {
      this->log_newest_seq_ = newest;
      this->log_fetch_seq_ = newest;
    }
  } else if (kind == 3 && data_len >= 11) {
    // [00][seq][00][code][0000][type 03/04][operating seconds u32 BE][0000].
    // The app lists the type-04 records; 03 records precede each 04 by a
    // few s.min (meaning not confirmed).
    uint16_t seq = d[1];
    uint16_t code = d[3];
    uint8_t type = d[6];
    uint32_t secs = (uint32_t) d[7] << 24 | (uint32_t) d[8] << 16 | (uint32_t) d[9] << 8 | d[10];
    ESP_LOGD(TAG, "[%s] EVENT LOG entry seq %u code %u type %u t=%lu s", this->parent_->address_str(), seq, code, type,
             (unsigned long) secs);
    if (seq == (this->log_fetch_seq_ & 0xff)) {
      this->log_fetch_seq_ = 0;
#ifdef USE_TEXT_SENSOR
      if (this->last_event_text_sensor_ != nullptr) {
        char buf[48];
        snprintf(buf, sizeof(buf), " (operating time %lu d %lu h %lu min)", (unsigned long) (secs / 86400),
                 (unsigned long) ((secs % 86400) / 3600), (unsigned long) ((secs % 3600) / 60));
        this->last_event_text_sensor_->publish_state(alpha2_format_event_(code) + buf);
      }
#endif
    }
  }
  return true;
}

void Alpha2::handle_geni_response_(const uint8_t *response, uint16_t length) {
  if (this->response_offset_ >= this->response_length_) {
    ESP_LOGD(TAG, "[%s] GENI response begin", this->parent_->address_str());
    if (length < GENI_RESPONSE_HEADER_LENGTH) {
      char hex[3 * 20 + 1] = {0};
      for (uint16_t i = 0; i < length && i < 20; i++) {
        snprintf(hex + i * 3, 4, "%02x ", response[i]);
      }
      // DEBUG not WARN (2026-09-04) - this fires as an expected, self-
      // healing side effect whenever a Class 7 (device info) fragment races
      // with the next Class 10 request and lands here instead - see the
      // device info polling comment in update(). Not indicative of a real
      // fault on its own.
      ESP_LOGD(TAG, "[%s] response too short (%d bytes): %s", this->parent_->address_str(), length, hex);
      return;
    }
    if (response[0] != 36 || response[2] != 248 || response[3] != 231 || response[4] != 10) {
      ESP_LOGD(TAG, "[%s] response bytes %d %d %d %d %d don't match GENI HEADER", this->parent_->address_str(),
               response[0], response[1], response[2], response[3], response[4]);
      return;
    }
    this->response_length_ = response[1] - GENI_RESPONSE_HEADER_LENGTH + 2;  // maybe 2 byte checksum
    this->response_offset_ = -GENI_RESPONSE_HEADER_LENGTH;
    std::memcpy(this->response_type_, response + 5, GENI_RESPONSE_TYPE_LENGTH);
    this->resp_done_ = false;
  }

  for (uint16_t i = 0; i < length; i++) {
    int16_t data_offset = this->response_offset_ + i;
    if (data_offset >= 0 && data_offset < (int16_t) sizeof(this->resp_data_))
      this->resp_data_[data_offset] = response[i];
  }
  uint8_t resp_len = std::min<uint8_t>(this->response_type_[7], sizeof(this->resp_data_));
  bool resp_complete = !this->resp_done_ && this->response_offset_ + (int16_t) length >= (int16_t) resp_len;
  if (resp_complete)
    this->resp_done_ = true;

  if (this->handle_event_response_(response, length)) {
    if (resp_complete)
      this->handle_complete_response_(resp_len);
    this->response_offset_ += length;
    return;
  }

  auto extract_publish_sensor_value = [response, length, this](int16_t value_offset, sensor::Sensor *sensor,
                                                               float factor) {
    this->extract_publish_sensor_value_(response, length, this->response_offset_, value_offset, sensor, factor);
  };

  if (this->is_current_response_type_(GENI_RESPONSE_TYPE_FLOW_HEAD)) {
    ESP_LOGD(TAG, "[%s] FLOW HEAD Response", this->parent_->address_str());
    extract_publish_sensor_value(GENI_RESPONSE_FLOW_OFFSET, this->flow_sensor_, 3600.0F);
    extract_publish_sensor_value(GENI_RESPONSE_HEAD_OFFSET, this->head_sensor_, 1.0F);
  } else if (this->is_current_response_type_(GENI_RESPONSE_TYPE_POWER)) {
    ESP_LOGD(TAG, "[%s] POWER Response", this->parent_->address_str());
    extract_publish_sensor_value(GENI_RESPONSE_POWER_OFFSET, this->power_sensor_, 1.0F);
    extract_publish_sensor_value(GENI_RESPONSE_CURRENT_OFFSET, this->current_sensor_, 1.0F);
    extract_publish_sensor_value(GENI_RESPONSE_MOTOR_SPEED_OFFSET, this->speed_sensor_, 1.0F);
    extract_publish_sensor_value(GENI_RESPONSE_VOLTAGE_AC_OFFSET, this->voltage_sensor_, 1.0F);
  } else if (this->is_current_response_type_(GENI_RESPONSE_TYPE_OPERATING_MODE)) {
    // Operating mode (56000a), pump run/stop status (560006), AND manual-venting
    // active/countdown (560008) all share this exact type - discriminate
    // via data offset 0 first (2026-09-04): 0x00=operating mode-or-pump-status
    // (sub-discriminate via offset 1, see below), 0x80=venting active
    // (countdown at offset 3), else(observed 0x01)=venting idle/unrelated
    // noise reading. See alpha2.h comments on these offset constants.
    auto offset0_rel = 0 - this->response_offset_;
    uint8_t offset0_value = (offset0_rel >= 0 && offset0_rel < length) ? response[offset0_rel] : 0xFF;
    if (offset0_value == GENI_DISCRIMINATOR_VENTING_ACTIVE) {
      ESP_LOGD(TAG, "[%s] VENTING ACTIVE/COUNTDOWN Response", this->parent_->address_str());
#ifdef USE_BINARY_SENSOR
      if (this->venting_active_sensor_ != nullptr)
        this->venting_active_sensor_->publish_state(true);
#endif
      extract_publish_sensor_value(GENI_RESPONSE_VENTING_COUNTDOWN_VALUE_OFFSET, this->venting_countdown_sensor_,
                                   1.0F);
    } else if (offset0_value == 0x00) {
      // Operating mode (56000a) and pump run/stop status (560006) share offset0
      // == 0x00 - discriminate further via data offset 1 (0x06=operating mode,
      // else=pump status).
      auto discriminator_rel_offset =
          GENI_RESPONSE_OPERATING_MODE_VS_PUMP_STATUS_DISCRIMINATOR_OFFSET - this->response_offset_;
      if (discriminator_rel_offset >= 0 && discriminator_rel_offset < length &&
          response[discriminator_rel_offset] == GENI_DISCRIMINATOR_OPERATING_MODE) {
        ESP_LOGD(TAG, "[%s] OPERATING MODE Response", this->parent_->address_str());
        this->extract_publish_enum_sensor_value_(response, length, this->response_offset_,
                                                 GENI_RESPONSE_OPERATING_MODE_OFFSET, this->operating_mode_sensor_);
#if defined(USE_SELECT) && defined(USE_SWITCH)
        if (this->operating_mode_select_ != nullptr) {
          auto rel_offset = GENI_RESPONSE_OPERATING_MODE_OFFSET - this->response_offset_;
          if (rel_offset >= 0 && rel_offset < length) {
            const char *label = alpha2_operating_mode_label_for_code_(response[rel_offset]);
            if (label != nullptr)
              this->operating_mode_select_->publish_state(label);
          }
        }
#endif
      } else if (discriminator_rel_offset >= 0 && discriminator_rel_offset < length) {
        ESP_LOGD(TAG, "[%s] PUMP STATUS Response", this->parent_->address_str());
#ifdef USE_SWITCH
        if (this->power_switch_ != nullptr) {
          // 0x00=running, 0x01=stopped (see alpha2.h comment on
          // GENI_RESPONSE_PUMP_STATUS_OFFSET for confidence caveat).
          this->power_switch_->publish_state(response[discriminator_rel_offset] == 0x00);
        }
#endif
      }
    } else {
      // offset0 == 0x01 (observed) - "venting idle", a noisy unrelated
      // reading that only matters as the negation of the active case above.
#ifdef USE_BINARY_SENSOR
      if (this->venting_active_sensor_ != nullptr)
        this->venting_active_sensor_->publish_state(false);
#endif
    }
  } else if (this->is_current_response_type_(GENI_RESPONSE_TYPE_EXTERN_CONTROL)) {
    // 2-byte value (register 5c0193) - swapped 2026-09-04, see comment on
    // GENI_RESPONSE_TYPE_EXTERN_CONTROL in alpha2.h.
    ESP_LOGD(TAG, "[%s] EXTERN KONTROLL Response", this->parent_->address_str());
    this->extract_publish_uint16_sensor_value_(response, length, this->response_offset_,
                                               GENI_RESPONSE_EXTERN_CONTROL_OFFSET, this->extern_control_sensor_);
#ifdef USE_SWITCH
    if (this->extern_control_switch_ != nullptr) {
      auto rel_offset = GENI_RESPONSE_EXTERN_CONTROL_OFFSET - this->response_offset_;
      if (rel_offset >= 0 && rel_offset + 1 < length) {
        uint16_t value = (static_cast<uint16_t>(response[rel_offset]) << 8) | response[rel_offset + 1];
        this->extern_control_switch_->publish_state(value != 0);
      }
    }
#endif
  } else if (this->is_current_response_type_(GENI_RESPONSE_TYPE_NIGHT_SETBACK)) {
    // 1-byte value (register 5a0008) - swapped 2026-09-04, see comment on
    // GENI_RESPONSE_TYPE_NIGHT_SETBACK in alpha2.h.
    ESP_LOGD(TAG, "[%s] NIGHT SETBACK Response", this->parent_->address_str());
    this->extract_publish_enum_sensor_value_(response, length, this->response_offset_,
                                             GENI_RESPONSE_NIGHT_SETBACK_OFFSET, this->night_setback_sensor_);
#ifdef USE_SWITCH
    this->extract_publish_switch_value_(response, length, this->response_offset_, GENI_RESPONSE_NIGHT_SETBACK_OFFSET,
                                        this->night_setback_switch_);
#endif
  } else if (this->is_current_response_type_(GENI_RESPONSE_TYPE_PANEL_LOCK)) {
    // 1-byte value (register 5e0002), found 2026-09-04.
    ESP_LOGD(TAG, "[%s] PANEL LOCK Response", this->parent_->address_str());
#ifdef USE_SWITCH
    this->extract_publish_switch_value_(response, length, this->response_offset_, GENI_RESPONSE_PANEL_LOCK_OFFSET,
                                        this->panel_lock_switch_);
#endif
  } else if (this->is_current_response_type_(GENI_RESPONSE_TYPE_PREVENT_DISPLAY_SLEEP)) {
    // High byte of a 2-byte value (register 5e0072), found 2026-09-04.
    // INVERTED (2026-09-04, confirmed on real hardware): raw 0 = ON,
    // raw 1 = OFF - opposite of every other on/off register found so far.
    // Can't use the generic extract_publish_switch_value_ helper (always
    // treats nonzero as ON), so this is inlined with the comparison
    // flipped.
    ESP_LOGD(TAG, "[%s] PREVENT DISPLAY SLEEP Response", this->parent_->address_str());
#ifdef USE_SWITCH
    if (this->prevent_display_sleep_switch_ != nullptr) {
      auto rel_offset = GENI_RESPONSE_PREVENT_DISPLAY_SLEEP_OFFSET - this->response_offset_;
      if (rel_offset >= 0 && rel_offset < length)
        this->prevent_display_sleep_switch_->publish_state(response[rel_offset] == 0);
    }
#endif
  } else if (this->is_current_response_type_(GENI_RESPONSE_TYPE_MEDIA_TEMP)) {
    ESP_LOGD(TAG, "[%s] MEDIA TEMP Response", this->parent_->address_str());
    extract_publish_sensor_value(GENI_RESPONSE_MEDIA_TEMP_OFFSET, this->media_temp_sensor_, 1.0F);
  } else if (this->is_current_response_type_(GENI_RESPONSE_TYPE_STATISTICS)) {
    // Register 5d0001, found 2026-09-04 - only "Antal starter" (data offset
    // 2) is decoded; the rest of this 14-field statistics block is unused.
    ESP_LOGD(TAG, "[%s] STATISTICS Response", this->parent_->address_str());
    this->extract_publish_uint16_sensor_value_(response, length, this->response_offset_,
                                               GENI_RESPONSE_START_COUNT_OFFSET, this->start_count_sensor_);
  } else if (this->is_current_response_type_(GENI_RESPONSE_TYPE_VENTING_STATUS)) {
    ESP_LOGD(TAG, "[%s] VENTING STATUS Response", this->parent_->address_str());
#ifdef USE_SWITCH
    if (this->continuous_venting_switch_ != nullptr) {
      // Only 0x02(off)/0x03(on) are meaningful for THIS switch - 0x01 means
      // a one-shot manual venting cycle is active, unrelated to the
      // continuous-mode toggle, so leave the switch state alone in that case.
      auto rel_offset = GENI_RESPONSE_VENTING_STATUS_OFFSET - this->response_offset_;
      if (rel_offset >= 0 && rel_offset < length) {
        uint8_t value = response[rel_offset];
        if (value == 0x03)
          this->continuous_venting_switch_->publish_state(true);
        else if (value == 0x02)
          this->continuous_venting_switch_->publish_state(false);
      }
    }
#endif
    this->extract_publish_enum_sensor_value_(response, length, this->response_offset_,
                                             GENI_RESPONSE_VENTING_STATUS_OFFSET, this->venting_status_sensor_);
  } else if (this->is_current_response_type_(GENI_RESPONSE_TYPE_SETPOINT)) {
    // Pressure/flow/speed setpoint readback (2026-09-04) - shared type,
    // real register identified by the discriminator byte. See
    // GENI_RESPONSE_TYPE_SETPOINT comment in alpha2.h.
    ESP_LOGD(TAG, "[%s] SETPOINT Response", this->parent_->address_str());
#if defined(USE_NUMBER) && defined(USE_SWITCH)
    auto rel_offset = GENI_RESPONSE_SETPOINT_DISCRIMINATOR_OFFSET - this->response_offset_;
    if (rel_offset >= 0 && rel_offset < length) {
      uint8_t discriminator = response[rel_offset];
      if (discriminator == GENI_SETPOINT_DISCRIMINATOR_PRESSURE) {
        // CP/PP - handled in handle_complete_response_() (pending_pressure_).
      } else if (discriminator == GENI_SETPOINT_DISCRIMINATOR_FLOW) {
        this->extract_publish_number_value_(response, length, this->response_offset_,
                                            GENI_RESPONSE_SETPOINT_VALUE_OFFSET, this->flow_setpoint_number_,
                                            3600.0F, 1);
      } else if (discriminator == GENI_SETPOINT_DISCRIMINATOR_SPEED) {
        this->extract_publish_number_value_(response, length, this->response_offset_,
                                            GENI_RESPONSE_SETPOINT_VALUE_OFFSET, this->speed_setpoint_number_, 1.0F,
                                            0);
      }
    }
#endif
  } else if (this->is_current_response_type_(GENI_RESPONSE_TYPE_FLOW_LIMIT)) {
    // Min/max flow limit readback (2026-09-04) - shared type, real register
    // identified by the discriminator byte; also carries the enable/disable
    // state for the corresponding switch. See GENI_RESPONSE_TYPE_FLOW_LIMIT
    // comment in alpha2.h.
    ESP_LOGD(TAG, "[%s] FLOW LIMIT Response", this->parent_->address_str());
#if defined(USE_NUMBER) && defined(USE_SWITCH)
    auto rel_offset = GENI_RESPONSE_FLOW_LIMIT_DISCRIMINATOR_OFFSET - this->response_offset_;
    if (rel_offset >= 0 && rel_offset < length) {
      uint8_t discriminator = response[rel_offset];
      number::Number *number_entity = nullptr;
      switch_::Switch *switch_entity = nullptr;
      if (discriminator == GENI_FLOW_LIMIT_DISCRIMINATOR_MIN) {
        number_entity = this->min_flow_limit_number_;
        switch_entity = this->min_flow_limit_switch_;
      } else if (discriminator == GENI_FLOW_LIMIT_DISCRIMINATOR_MAX) {
        number_entity = this->max_flow_limit_number_;
        switch_entity = this->max_flow_limit_switch_;
      }
      if (number_entity != nullptr) {
        this->extract_publish_number_value_(response, length, this->response_offset_,
                                            GENI_RESPONSE_FLOW_LIMIT_VALUE_OFFSET, number_entity, 3600.0F, 1);
      }
      this->extract_publish_switch_value_(response, length, this->response_offset_,
                                          GENI_RESPONSE_FLOW_LIMIT_ENABLE_OFFSET, switch_entity);
    }
#endif
  } else if (resp_complete) {
    // Types only handled on the complete payload (presets etc.) - see
    // handle_complete_response_().
  } else {
    ESP_LOGW(TAG, "unkown GENI response Type %d %d %d %d %d %d %d %d", this->response_type_[0], this->response_type_[1],
             this->response_type_[2], this->response_type_[3], this->response_type_[4], this->response_type_[5],
             this->response_type_[6], this->response_type_[7]);
  }

  if (resp_complete)
    this->handle_complete_response_(resp_len);
  this->response_offset_ += length;
}

#ifdef USE_TEXT_SENSOR
void Alpha2::handle_device_info_response_(const uint8_t *response, uint16_t length) {
  if (this->device_info_offset_ >= this->device_info_length_) {
    if (length < 6 || response[0] != 36 || response[2] != 248 || response[3] != 231 ||
        response[4] != GENI_CLASS_DEVICE_INFO) {
      // DEBUG not WARN (2026-09-04) - same benign race as the mismatch
      // checks in handle_geni_response_, just the other direction (a
      // Class 10 fragment landing here instead).
      ESP_LOGD(TAG, "[%s] device info response header mismatch", this->parent_->address_str());
      this->awaiting_device_info_ = false;
      return;
    }
    this->device_info_length_ =
        std::min<uint8_t>(response[5], static_cast<uint8_t>(GENI_DEVICE_INFO_BUFFER_LENGTH - 1));
    this->device_info_offset_ = -6;
    std::memset(this->device_info_buffer_, 0, GENI_DEVICE_INFO_BUFFER_LENGTH);
  }

  // Same cross-fragment copy math as extract_publish_sensor_value_, just
  // generalized to a variable-length string instead of a fixed 4-byte float.
  const int16_t value_length = this->device_info_length_;
  int16_t rel_offset = 0 - this->device_info_offset_;
  if (rel_offset > -value_length && rel_offset < length) {
    auto start_offset = std::max((int16_t) 0, rel_offset);
    auto end_offset = std::min((int16_t) (rel_offset + value_length), (int16_t) length);
    auto copy_length = end_offset - start_offset;
    auto buffer_offset = std::max((int16_t) (-rel_offset), (int16_t) 0);
    if (copy_length > 0 && buffer_offset + copy_length < (int16_t) GENI_DEVICE_INFO_BUFFER_LENGTH)
      std::memcpy(this->device_info_buffer_ + buffer_offset, response + start_offset, copy_length);
  }

  this->device_info_offset_ += length;

  if (this->device_info_offset_ >= this->device_info_length_) {
    text_sensor::TextSensor *target = nullptr;
    switch (this->pending_device_info_field_) {
      case GENI_DEVICE_INFO_FIELD_PRODUCT_TYPE:
        target = this->product_type_text_sensor_;
        break;
      case GENI_DEVICE_INFO_FIELD_PRODUCT_NO:
        target = this->product_no_text_sensor_;
        break;
      case GENI_DEVICE_INFO_FIELD_SERIAL_NO:
        target = this->serial_no_text_sensor_;
        break;
      case GENI_DEVICE_INFO_FIELD_PRODUCTION_CODE:
        target = this->production_code_text_sensor_;
        break;
      case GENI_DEVICE_INFO_FIELD_GSC_DESCRIPTION:
        target = this->gsc_description_text_sensor_;
        break;
      case GENI_DEVICE_INFO_FIELD_GSC_IDENTIFICATION:
        target = this->gsc_identification_text_sensor_;
        break;
      case GENI_DEVICE_INFO_FIELD_APP_SOFTWARE:
        target = this->app_software_text_sensor_;
        break;
      case GENI_DEVICE_INFO_FIELD_BLE_SOFTWARE:
        target = this->ble_software_text_sensor_;
        break;
      default:
        break;
    }
    if (target != nullptr)
      target->publish_state(std::string(reinterpret_cast<char *>(this->device_info_buffer_)));
    this->awaiting_device_info_ = false;
  }
}

void Alpha2::request_device_info_(uint8_t field_id) {
  uint8_t frame[9] = {0x27, 0x05, 0xe7, 0xf8, GENI_CLASS_DEVICE_INFO, 0x01, field_id, 0x00, 0x00};
  this->pending_device_info_field_ = field_id;
  this->device_info_offset_ = 0;
  this->device_info_length_ = 0;
  this->awaiting_device_info_ = true;
  this->finalize_and_send_(frame, sizeof(frame), sizeof(frame));
}
#endif

void Alpha2::gattc_event_handler(esp_gattc_cb_event_t event, esp_gatt_if_t gattc_if, esp_ble_gattc_cb_param_t *param) {
  switch (event) {
    case ESP_GATTC_OPEN_EVT: {
      if (param->open.status == ESP_GATT_OK) {
        this->response_offset_ = 0;
        this->response_length_ = 0;
        ESP_LOGI(TAG, "[%s] connection open", this->parent_->address_str());
      }
      break;
    }
    case ESP_GATTC_CONNECT_EVT: {
      if (std::memcmp(param->connect.remote_bda, this->parent_->get_remote_bda(), 6) != 0)
        return;
      auto ret = esp_ble_set_encryption(param->connect.remote_bda, ESP_BLE_SEC_ENCRYPT);
      if (ret) {
        ESP_LOGW(TAG, "esp_ble_set_encryption failed, status=%x", ret);
      }
      break;
    }
    case ESP_GATTC_DISCONNECT_EVT: {
      this->node_state = espbt::ClientState::IDLE;
      if (this->flow_sensor_ != nullptr)
        this->flow_sensor_->publish_state(NAN);
      if (this->head_sensor_ != nullptr)
        this->head_sensor_->publish_state(NAN);
      if (this->power_sensor_ != nullptr)
        this->power_sensor_->publish_state(NAN);
      if (this->current_sensor_ != nullptr)
        this->current_sensor_->publish_state(NAN);
      if (this->speed_sensor_ != nullptr)
        this->speed_sensor_->publish_state(NAN);
      if (this->voltage_sensor_ != nullptr)
        this->voltage_sensor_->publish_state(NAN);
      if (this->operating_mode_sensor_ != nullptr)
        this->operating_mode_sensor_->publish_state(NAN);
      if (this->extern_control_sensor_ != nullptr)
        this->extern_control_sensor_->publish_state(NAN);
      if (this->night_setback_sensor_ != nullptr)
        this->night_setback_sensor_->publish_state(NAN);
      if (this->venting_status_sensor_ != nullptr)
        this->venting_status_sensor_->publish_state(NAN);
      // venting_active_sensor_ (binary_sensor) has no NAN/"unknown" state -
      // left showing its last known value on disconnect, same as switches.
      if (this->venting_countdown_sensor_ != nullptr)
        this->venting_countdown_sensor_->publish_state(NAN);
      if (this->media_temp_sensor_ != nullptr)
        this->media_temp_sensor_->publish_state(NAN);
      if (this->start_count_sensor_ != nullptr)
        this->start_count_sensor_->publish_state(NAN);
      if (this->warning_code_sensor_ != nullptr)
        this->warning_code_sensor_->publish_state(NAN);
      if (this->alarm_code_sensor_ != nullptr)
        this->alarm_code_sensor_->publish_state(NAN);
      this->pending_event_list_ = 0;
      // Device info text sensors have no NAN/"unknown" state - left showing
      // their last known value on disconnect, same as switches.
#ifdef USE_TEXT_SENSOR
      this->awaiting_device_info_ = false;
      this->device_info_fetch_active_ = false;
#endif
      break;
    }
    case ESP_GATTC_SEARCH_CMPL_EVT: {
      auto *chr = this->parent_->get_characteristic(ALPHA2_GENI_SERVICE_UUID, ALPHA2_GENI_CHARACTERISTIC_UUID);
      if (chr == nullptr) {
        ESP_LOGE(TAG, "[%s] No GENI service found at device, not an Alpha2.?", this->parent_->address_str());
        break;
      }
      auto status = esp_ble_gattc_register_for_notify(this->parent_->get_gattc_if(), this->parent_->get_remote_bda(),
                                                      chr->handle);
      if (status) {
        ESP_LOGW(TAG, "esp_ble_gattc_register_for_notify failed, status=%d", status);
      }
      this->geni_handle_ = chr->handle;
      break;
    }
    case ESP_GATTC_REG_FOR_NOTIFY_EVT: {
      this->node_state = espbt::ClientState::ESTABLISHED;
      this->update();
#ifdef USE_SWITCH
      // Auto-reapply-on-reconnect temporarily disabled (2026-09-02): this
      // device has been reconnecting frequently under heavy load tonight,
      // and every reconnect was firing an extra BLE GENI write here right
      // as the connection re-establishes - suspected of contributing to
      // crash-reconnect loops rather than helping. The manual switch itself
      // (Alpha2Switch::write_state) is untouched and still works normally.
      // Re-enable by uncommenting the line below once the device is stable
      // again - or once you have real GENI capture data to verify this
      // command sequence directly.
      // this->pending_reapply_ = true;
#endif
      break;
    }
    case ESP_GATTC_NOTIFY_EVT: {
      if (param->notify.handle == this->geni_handle_) {
#ifdef USE_TEXT_SENSOR
        if (this->awaiting_device_info_) {
          this->handle_device_info_response_(param->notify.value, param->notify.value_len);
        } else {
          this->handle_geni_response_(param->notify.value, param->notify.value_len);
        }
#else
        this->handle_geni_response_(param->notify.value, param->notify.value_len);
#endif
      }
      break;
    }
    default:
      break;
  }
}

uint16_t Alpha2::calculate_crc_(const uint8_t *data, size_t len) {
  // CRC-16-CCITT, polynomial 0x1021, initial value 0xFFFF, final XOR 0xFFFF.
  // Verified against the flow_head/power read requests above.
  uint16_t crc = 0xFFFF;
  for (size_t i = 0; i < len; i++) {
    crc ^= static_cast<uint16_t>(data[i]) << 8;
    for (int j = 0; j < 8; j++) {
      crc = (crc & 0x8000) ? static_cast<uint16_t>((crc << 1) ^ 0x1021) : static_cast<uint16_t>(crc << 1);
    }
  }
  return crc ^ 0xFFFF;
}

void Alpha2::finalize_and_send_(uint8_t *frame, size_t total_len, size_t first_chunk_len) {
  uint16_t crc = this->calculate_crc_(frame + 1, total_len - 3);
  frame[total_len - 2] = crc >> 8;
  frame[total_len - 1] = crc & 0xFF;
  this->send_request_(frame, first_chunk_len);
  if (first_chunk_len < total_len)
    this->send_request_(frame + first_chunk_len, total_len - first_chunk_len);
}

void Alpha2::send_request_(uint8_t *request, size_t len) {
  auto status =
      esp_ble_gattc_write_char(this->parent_->get_gattc_if(), this->parent_->get_conn_id(), this->geni_handle_, len,
                               request, ESP_GATT_WRITE_TYPE_NO_RSP, ESP_GATT_AUTH_REQ_NONE);
  if (status)
    ESP_LOGW(TAG, "[%s] esp_ble_gattc_write_char failed, status=%d", this->parent_->address_str(), status);
}

void Alpha2::update() {
  if (this->node_state != espbt::ClientState::ESTABLISHED) {
    ESP_LOGW(TAG, "[%s] Cannot poll, not connected", this->parent_->address_str());
    return;
  }

  if (this->flow_sensor_ != nullptr || this->head_sensor_ != nullptr) {
    uint8_t geni_request_flow_head[] =   {39, 7, 231, 248, 10, 3, 93, 1, 34, 98, 124};
    this->send_request_(geni_request_flow_head, sizeof(geni_request_flow_head));
    delay(25);  // need to wait between requests
  }
  if (this->power_sensor_ != nullptr || this->current_sensor_ != nullptr || this->speed_sensor_ != nullptr ||
      this->voltage_sensor_ != nullptr) {
    uint8_t geni_request_power[] = {39, 7, 231, 248, 10, 3, 87, 0, 69, 138, 205};
    this->send_request_(geni_request_power, sizeof(geni_request_power));
    delay(25);  // need to wait between requests
  }
  // Poll guards below check BOTH the raw sensor AND the corresponding
  // switch/select (2026-09-04) - polling must keep running for readback
  // even if the raw diagnostic sensor is removed from YAML, since they
  // share the same GET request/response.
  bool poll_operating_mode = this->operating_mode_sensor_ != nullptr;
  bool poll_extern_control = this->extern_control_sensor_ != nullptr;
  bool poll_night_setback = this->night_setback_sensor_ != nullptr;
  bool poll_venting_status = this->venting_status_sensor_ != nullptr;
#if defined(USE_SELECT) && defined(USE_SWITCH)
  poll_operating_mode = poll_operating_mode || (this->operating_mode_select_ != nullptr);
  poll_operating_mode = poll_operating_mode || this->panel_select_configured_();
#endif
#ifdef USE_SWITCH
  poll_extern_control = poll_extern_control || (this->extern_control_switch_ != nullptr);
  poll_night_setback = poll_night_setback || (this->night_setback_switch_ != nullptr);
  poll_venting_status = poll_venting_status || (this->continuous_venting_switch_ != nullptr);
#endif
  if (poll_operating_mode) {
    uint8_t geni_request_operating_mode[] = {39, 7, 231, 248, 10, 3, 86, 0, 10, 4, 214};
    this->send_request_(geni_request_operating_mode, sizeof(geni_request_operating_mode));
    delay(25);  // need to wait between requests
  }
#ifdef USE_SWITCH
  if (this->power_switch_ != nullptr) {
    // Register 560006, pump run/stop status (2026-09-04). Verbatim real GET
    // request capture. Response shares operating mode's type - see
    // GENI_RESPONSE_PUMP_STATUS_OFFSET comment in alpha2.h.
    uint8_t geni_request_pump_status[] = {39, 7, 231, 248, 10, 3, 86, 0, 6, 197, 90};
    this->send_request_(geni_request_pump_status, sizeof(geni_request_pump_status));
    delay(25);  // need to wait between requests
  }
#endif
  // 86/8 doubles as the realized request (status entities, curve select).
  bool poll_venting_countdown = this->venting_countdown_sensor_ != nullptr || this->need_status_poll_();
#ifdef USE_BINARY_SENSOR
  poll_venting_countdown = poll_venting_countdown || (this->venting_active_sensor_ != nullptr);
#endif
  if (poll_venting_countdown) {
    // Register 560008, manual venting active/countdown (2026-09-04).
    // Verbatim real GET request capture. Response shares operating mode's type -
    // see GENI_DISCRIMINATOR_VENTING_ACTIVE comment in alpha2.h.
    uint8_t geni_request_venting_countdown[] = {39, 7, 231, 248, 10, 3, 86, 0, 8, 36, 148};
    this->send_request_(geni_request_venting_countdown, sizeof(geni_request_venting_countdown));
    delay(25);  // need to wait between requests
  }
  if (this->media_temp_sensor_ != nullptr) {
    // Register 5d012c, media temperature (2026-09-04). Verbatim real GET
    // request capture.
    uint8_t geni_request_media_temp[] = {39, 7, 231, 248, 10, 3, 93, 1, 44, 131, 178};
    this->send_request_(geni_request_media_temp, sizeof(geni_request_media_temp));
    delay(25);  // need to wait between requests
  }
  if (this->start_count_sensor_ != nullptr) {
    // Register 5d0001, start count (2026-09-04). Verbatim real GET
    // request capture.
    uint8_t geni_request_start_count[] = {39, 7, 231, 248, 10, 3, 93, 0, 1, 69, 76};
    this->send_request_(geni_request_start_count, sizeof(geni_request_start_count));
    delay(25);  // need to wait between requests
  }
  if (poll_extern_control) {
    // Register 5c0193, verbatim real GET request capture (2026-09-04).
    // Swapped 2026-09-04 evening - this used to poll 5a0008, see the
    // correction note on GENI_RESPONSE_TYPE_EXTERN_CONTROL in alpha2.h.
    uint8_t geni_request_extern_control[] = {39, 7, 231, 248, 10, 3, 92, 1, 147, 226, 182};
    this->send_request_(geni_request_extern_control, sizeof(geni_request_extern_control));
    delay(25);  // need to wait between requests
  }
  if (poll_night_setback) {
    // Register 5a0008, verbatim real GET request capture (2026-09-04).
    // Swapped 2026-09-04 evening - this used to poll 5c0193, see the
    // correction note on GENI_RESPONSE_TYPE_NIGHT_SETBACK in alpha2.h.
    uint8_t geni_request_night_setback[] = {39, 7, 231, 248, 10, 3, 90, 0, 8, 81, 245};
    this->send_request_(geni_request_night_setback, sizeof(geni_request_night_setback));
    delay(25);  // need to wait between requests
  }
  if (poll_venting_status) {
    // Register 580263, verbatim real GET request capture (2026-09-04).
    uint8_t geni_request_venting_status[] = {39, 7, 231, 248, 10, 3, 88, 2, 99, 132, 58};
    this->send_request_(geni_request_venting_status, sizeof(geni_request_venting_status));
    delay(25);  // need to wait between requests
  }
#ifdef USE_SWITCH
  if (this->panel_lock_switch_ != nullptr) {
    // Register 5e0002, panel lock (2026-09-04). Verbatim real GET request
    // capture.
    uint8_t geni_request_panel_lock[] = {39, 7, 231, 248, 10, 3, 94, 0, 2, 44, 127};
    this->send_request_(geni_request_panel_lock, sizeof(geni_request_panel_lock));
    delay(25);  // need to wait between requests
  }
  if (this->prevent_display_sleep_switch_ != nullptr) {
    // Register 5e0072, prevent display sleep (2026-09-04). Verbatim
    // real GET request capture.
    uint8_t geni_request_prevent_display_sleep[] = {39, 7, 231, 248, 10, 3, 94, 0, 114, 82, 232};
    this->send_request_(geni_request_prevent_display_sleep, sizeof(geni_request_prevent_display_sleep));
    delay(25);  // need to wait between requests
  }
#endif
#if defined(USE_NUMBER) && defined(USE_SWITCH)
  // Setpoint/flow-limit readback polling (2026-09-04) - verbatim real GET
  // request captures. Only polled if the corresponding number entity is
  // configured, so this costs nothing for a component built without them.
  // CP (86/16) and PP (86/18) setpoints are requested alternately: both
  // answer with the same type and feedback id (see pending_pressure_).
  if (this->need_pressure_poll_()) {
    bool want_cp = this->const_pressure_setpoint_number_ != nullptr || this->panel_select_configured_();
    bool want_pp = this->pressure_setpoint_number_ != nullptr || this->panel_select_configured_();
    bool cp_turn = want_cp && (!want_pp || (this->event_poll_counter_ % 2 == 0));
    if (cp_turn) {
      uint8_t geni_request_cp_setpoint[] = {0x27, 0x07, 0xe7, 0xf8, 0x0a, 0x03, 0x56, 0x00, 0x10, 0xb7, 0xad};
      this->pending_pressure_ = 1;
      this->send_request_(geni_request_cp_setpoint, sizeof(geni_request_cp_setpoint));
    } else {
      uint8_t geni_request_pressure_setpoint[] = {39, 7, 231, 248, 10, 3, 86, 0, 18, 151, 239};
      this->pending_pressure_ = 2;
      this->send_request_(geni_request_pressure_setpoint, sizeof(geni_request_pressure_setpoint));
    }
    delay(25);  // need to wait between requests
  }
  if (this->flow_setpoint_number_ != nullptr) {
    uint8_t geni_request_flow_setpoint[] = {39, 7, 231, 248, 10, 3, 86, 0, 40, 0, 246};
    this->send_request_(geni_request_flow_setpoint, sizeof(geni_request_flow_setpoint));
    delay(25);  // need to wait between requests
  }
  if (this->speed_setpoint_number_ != nullptr || this->panel_select_configured_()) {
    uint8_t geni_request_speed_setpoint[] = {39, 7, 231, 248, 10, 3, 86, 0, 14, 68, 82};
    this->send_request_(geni_request_speed_setpoint, sizeof(geni_request_speed_setpoint));
    delay(25);  // need to wait between requests
  }
  if (this->min_flow_limit_number_ != nullptr) {
    uint8_t geni_request_min_flow_limit[] = {39, 7, 231, 248, 10, 3, 86, 2, 89, 8, 34};
    this->send_request_(geni_request_min_flow_limit, sizeof(geni_request_min_flow_limit));
    delay(25);  // need to wait between requests
  }
  if (this->max_flow_limit_number_ != nullptr) {
    uint8_t geni_request_max_flow_limit[] = {39, 7, 231, 248, 10, 3, 86, 2, 88, 24, 3};
    this->send_request_(geni_request_max_flow_limit, sizeof(geni_request_max_flow_limit));
    delay(25);  // need to wait between requests
  }
#endif
  // Alarms/warnings (2026-11-12): one active-list request per cycle,
  // alternating, because 58000b and 580000 share a response type.
  bool poll_warnings = this->warning_code_sensor_ != nullptr;
  bool poll_alarms = this->alarm_code_sensor_ != nullptr;
  bool poll_event_log = false;
#ifdef USE_TEXT_SENSOR
  poll_warnings = poll_warnings || this->active_warning_text_sensor_ != nullptr;
  poll_alarms = poll_alarms || this->active_alarm_text_sensor_ != nullptr;
  poll_event_log = this->last_event_text_sensor_ != nullptr;
#endif
  uint32_t cycle = this->event_poll_counter_++;
  bool warnings_turn = poll_warnings && (!poll_alarms || cycle % 2 == 0);
  if (warnings_turn) {
    uint8_t geni_request_warnings[] = {0x27, 0x07, 0xe7, 0xf8, 0x0a, 0x03, 0x58, 0x00, 0x0b, 0x0f, 0xf6};
    this->pending_event_list_ = 1;
    this->send_request_(geni_request_warnings, sizeof(geni_request_warnings));
    delay(25);  // need to wait between requests
  } else if (poll_alarms) {
    uint8_t geni_request_alarms[] = {0x27, 0x07, 0xe7, 0xf8, 0x0a, 0x03, 0x58, 0x00, 0x00, 0xbe, 0x9d};
    this->pending_event_list_ = 2;
    this->send_request_(geni_request_alarms, sizeof(geni_request_alarms));
    delay(25);  // need to wait between requests
  }
#if defined(USE_SELECT) && defined(USE_SWITCH)
  if (this->panel_setting_select_ != nullptr) {
    uint8_t idx = cycle % 9;
    uint8_t geni_request_preset[] = {0x27, 0x07, 0xe7, 0xf8, 0x0a, 0x03, 0x56, 0x01,
                                     static_cast<uint8_t>(0x5f + idx),
                                     static_cast<uint8_t>(ALPHA2_PRESET_CRC[idx] >> 8),
                                     static_cast<uint8_t>(ALPHA2_PRESET_CRC[idx] & 0xff)};
    this->pending_preset_ = idx;
    this->send_request_(geni_request_preset, sizeof(geni_request_preset));
    delay(25);  // need to wait between requests
  }
#endif
  if (poll_event_log && this->log_fetch_seq_ != 0) {
    uint8_t slot = (this->log_fetch_seq_ - 1) % GENI_EVENT_LOG_SIZE;
    uint16_t crc = GENI_EVENT_LOG_ENTRY_CRC[slot];
    uint8_t geni_request_log_entry[] = {0x27, 0x07, 0xe7, 0xf8, 0x0a, 0x03, 0x58, 0x27,
                                        static_cast<uint8_t>(GENI_EVENT_LOG_FIRST_INDEX + slot),
                                        static_cast<uint8_t>(crc >> 8), static_cast<uint8_t>(crc & 0xff)};
    this->send_request_(geni_request_log_entry, sizeof(geni_request_log_entry));
    delay(25);  // need to wait between requests
  } else if (poll_event_log && cycle % 4 == 1) {
    uint8_t geni_request_log_header[] = {0x27, 0x07, 0xe7, 0xf8, 0x0a, 0x03,
                                         0x58, 0x27, GENI_EVENT_LOG_HEADER_INDEX, 0x9a, 0x76};
    this->send_request_(geni_request_log_header, sizeof(geni_request_log_header));
    delay(25);  // need to wait between requests
  }
#ifdef USE_TEXT_SENSOR
  // Device info (ASCII) text sensors (2026-09-04). These are static values -
  // originally fetched once per connection (buggy), then every update()
  // cycle with fixed delay()s between requests (still buggy - a real
  // capture showed ALL 8 responses arriving in one late batch regardless of
  // the delay used, since the actual BLE round-trip time isn't reliably
  // boundable by a guessed constant). Now just arms the one-at-a-time state
  // machine in loop() (see device_info_fetch_active_ in alpha2.h), which
  // waits for each field's actual response (or a timeout) before requesting
  // the next - self-pacing instead of guessing. Throttled to once every 20
  // update() cycles (~5 minutes at the default 15s interval) since these
  // values never change.
  if (this->device_info_poll_counter_++ % 20 == 0) {
    // Small settling delay (2026-09-04) before arming - real hardware
    // showed the FIRST field (Produkttyp) specifically kept failing while
    // every other field worked fine. Structural, not random: this fetch
    // window is armed right at the tail of the ~20 Class-10 requests above,
    // and loop() fires Produkttyp's request on the very next tick - if one
    // of those Class-10 responses is still in flight at that instant, it
    // lands while we're awaiting Produkttyp specifically (only the FIRST
    // field is vulnerable to this - by the time later fields fire, the
    // Class-10 backlog has long cleared). This gives the connection a
    // moment to flush before starting.
    delay(100);
    this->device_info_fetch_active_ = true;
    this->device_info_next_field_index_ = 0;
  }
#endif
}

// ---------------------------------------------------------------------------
// Start/stop control - confirmed working on real ALPHA2 GO hardware as of
// 2026-09-01. See the comment block in alpha2.h for the full protocol notes.
// ---------------------------------------------------------------------------

#ifdef USE_SWITCH
void Alpha2::reapply_last_command_() {
  if (this->power_switch_ != nullptr) {
    ESP_LOGI(TAG, "[%s] Re-applying last known power state after (re)connect: %s", this->parent_->address_str(),
             this->power_switch_->state ? "ON" : "OFF");
    this->write_power(this->power_switch_->state);
  }
}

void Alpha2::send_commands_(const uint8_t *command_ids, size_t num_commands) {
  if (this->node_state != espbt::ClientState::ESTABLISHED) {
    ESP_LOGW(TAG, "[%s] Cannot send command, not connected", this->parent_->address_str());
    return;
  }
  if (num_commands == 0 || num_commands > 8) {
    ESP_LOGW(TAG, "[%s] Invalid number of commands: %zu", this->parent_->address_str(), num_commands);
    return;
  }

  // Frame: [0x27 start][length][0xE7 dest][0xF8 source][0x03 class][0x80|N opspec][cmd.][crcH][crcL]
  // Same dest/source order and CRC scope as the proven read requests above.
  const size_t apdu_len = 2 + num_commands;  // class + opspec + command ids
  uint8_t packet[16];
  packet[0] = 0x27;
  packet[1] = static_cast<uint8_t>(2 + apdu_len);
  packet[2] = 0xE7;
  packet[3] = 0xF8;
  packet[4] = GENI_CLASS_COMMANDS;
  packet[5] = GENI_OPSPEC_SET | static_cast<uint8_t>(num_commands);
  std::memcpy(packet + 6, command_ids, num_commands);

  const size_t crc_input_len = 1 + 2 + apdu_len;  // length byte + dest + source + apdu
  const size_t total_len = 1 + crc_input_len + 2;  // == 8 + num_commands

  ESP_LOGI(TAG, "[%s] Sending GENI command(s)", this->parent_->address_str());
  this->finalize_and_send_(packet, total_len, total_len);
}

void Alpha2::write_power(bool state) {
  const uint8_t commands[] = {GENI_CMD_REMOTE, static_cast<uint8_t>(state ? GENI_CMD_START : GENI_CMD_STOP)};
  this->send_commands_(commands, 2);
}

void Alpha2Switch::write_state(bool state) {
  if (this->parent_ != nullptr)
    this->parent_->write_power(state);
  this->publish_state(state);
}

void Alpha2::write_extern_control(bool state) {
  // Register 5c0193, found 2026-09-04 -.
  // SWAPPED 2026-09-04 evening: originally attributed to 5a0008, corrected
  // after cross-checking a clean isolated capture against the app's live
  // on/off state (app showed "External control" ON, matching this register's
  // last write in that capture being the ON-ish value). Unfragmented 19-byte
  // frame (opspec 0x8b = SET|11 bytes data). OFF=0x0000, ON=0x7b00 - both
  // confirmed via a clean off-then-on capture matching the app state.
  uint8_t frame[19] = {
      0x27, 0x0f, 0xe7, 0xf8, 0x0a, 0x8b, 0x5c, 0x01, 0x93, 0x04, 0x5e, 0x01, 0x00, 0x00, 0x02,
      static_cast<uint8_t>(state ? 0x7b : 0x00), 0x00,
      0x00, 0x00,  // CRC, filled below
  };
  ESP_LOGI(TAG, "[%s] Writing extern kontroll: %s", this->parent_->address_str(), state ? "ON" : "OFF");
  this->finalize_and_send_(frame, sizeof(frame), sizeof(frame));
}

void Alpha2::write_night_setback(bool state) {
  // Register 5a0008, found 2026-09-04 -.
  // SWAPPED 2026-09-04 evening: originally attributed to 5c0193, see the
  // correction note on write_extern_control above. Unfragmented 18-byte
  // frame (opspec 0x8a = SET|10 bytes data), simple 1-byte boolean (1=on,
  // 0=off).
  uint8_t frame[18] = {
      0x27, 0x0e, 0xe7, 0xf8, 0x0a, 0x8a, 0x5a, 0x00, 0x08, 0x01, 0x4d, 0x01, 0x00, 0x00, 0x01,
      static_cast<uint8_t>(state ? 0x01 : 0x00),
      0x00, 0x00,  // CRC, filled below
  };
  ESP_LOGI(TAG, "[%s] Writing night setback: %s", this->parent_->address_str(), state ? "ON" : "OFF");
  this->finalize_and_send_(frame, sizeof(frame), sizeof(frame));
}

void Alpha2::write_min_flow_limit(bool state) {
  // Register 9b560259, found 2026-09-04 -.
  // Enable/disable toggle only (byte offset 16). The float target below
  // (raw 0.0002778 = 1.0 m3/h) is just whatever was configured at capture
  // time, echoed verbatim on every toggle - toggling this does NOT change
  // the actual limit value (use write_min_flow_limit_value() for that,
  // found later the same day), but it DOES silently reset the value back
  // to whatever's hardcoded here if the real limit has since changed. Fine
  // for a simple enable/disable switch as long as that's understood.
  uint8_t frame[35] = {
      0x27, 0x1f, 0xe7, 0xf8, 0x0a, 0x9b, 0x56, 0x02, 0x59, 0x03, 0x7f, 0x01, 0x00, 0x00, 0x12, 0x02,
      static_cast<uint8_t>(state ? 0x01 : 0x00),
      0x39, 0x91, 0xa5, 0xaf,
      0x3f, 0x00, 0x00, 0x00, 0x40, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
      0x00, 0x00,  // CRC, filled below
  };
  ESP_LOGI(TAG, "[%s] Writing min flow limit: %s (value unchanged, ~1.0 m3/h)", this->parent_->address_str(),
           state ? "ON" : "OFF");
  this->finalize_and_send_(frame, sizeof(frame), 20);
}

void Alpha2::write_max_flow_limit(bool state) {
  // Register 9b560258, found 2026-09-04 -.
  // Same caveat as write_min_flow_limit above - toggle-only, echoes the
  // captured value (raw 0.0005556 = 2.0 m3/h) verbatim every time.
  uint8_t frame[35] = {
      0x27, 0x1f, 0xe7, 0xf8, 0x0a, 0x9b, 0x56, 0x02, 0x58, 0x03, 0x7f, 0x01, 0x00, 0x00, 0x12, 0x01,
      static_cast<uint8_t>(state ? 0x01 : 0x00),
      0x3a, 0x11, 0xa5, 0xaf,
      0x3f, 0x00, 0x00, 0x00, 0x40, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
      0x00, 0x00,  // CRC, filled below
  };
  ESP_LOGI(TAG, "[%s] Writing max flow limit: %s (value unchanged, ~2.0 m3/h)", this->parent_->address_str(),
           state ? "ON" : "OFF");
  this->finalize_and_send_(frame, sizeof(frame), 20);
}

void Alpha2::write_continuous_venting(bool state) {
  // Register 90580263 - "Continuous air detection and venting" toggle.
  // CONFIRMED 2026-09-04: byte offset 15 = 0x03 for ON, 0x02 for OFF,
  // matched a clean off-then-on capture exactly. The same register also
  // takes value 0x01 when a one-shot manual venting cycle is started (see
  // write_venting()) - a 3-state "venting mode" register, not a pure
  // boolean, but 0x02/0x03 are the correct pair for this toggle
  // specifically.
  uint8_t frame[24] = {
      0x27, 0x14, 0xe7, 0xf8, 0x0a, 0x90, 0x58, 0x02, 0x63, 0x03, 0xca, 0x01, 0x00, 0x00, 0x07,
      static_cast<uint8_t>(state ? 0x03 : 0x02),
      0x39, 0x25, 0x62, 0xcb, 0x00, 0x78,
      0x00, 0x00,  // CRC, filled below
  };
  ESP_LOGI(TAG, "[%s] Writing continuous venting: %s", this->parent_->address_str(), state ? "ON" : "OFF");
  this->finalize_and_send_(frame, sizeof(frame), 20);
}

void Alpha2::write_panel_lock(bool state) {
  // Register 5e0002, found 2026-09-04 - single flag byte at frame offset 15
  // (0=off, 1=on). Confirmed by cross-checking a clean "panel lock off then on"
  // capture (ending back on 0x00) against the app showing panel lock OFF
  // live.
  uint8_t frame[20] = {
      0x27, 0x10, 0xe7, 0xf8, 0x0a, 0x8c, 0x5e, 0x00, 0x02, 0x01, 0x0d, 0x01, 0x00, 0x00, 0x03,
      static_cast<uint8_t>(state ? 0x01 : 0x00),
      0x00, 0x00,
      0x00, 0x00,  // CRC, filled below
  };
  ESP_LOGI(TAG, "[%s] Writing panel lock: %s", this->parent_->address_str(), state ? "ON" : "OFF");
  this->finalize_and_send_(frame, sizeof(frame), sizeof(frame));
}

void Alpha2::write_prevent_display_sleep(bool state) {
  // Register 5e0072, found 2026-09-04 - 2-byte value, only the high byte
  // (frame offset 15) toggles. INVERTED (confirmed on real hardware,
  // 2026-09-04): raw 0 = ON, raw 1 = OFF - opposite of every other on/off
  // register found so far. The low byte (offset 16) stays a constant 0x0f
  // in every capture, meaning unknown.
  uint8_t frame[19] = {
      0x27, 0x0f, 0xe7, 0xf8, 0x0a, 0x8b, 0x5e, 0x00, 0x72, 0x03, 0x97, 0x01, 0x00, 0x00, 0x02,
      static_cast<uint8_t>(state ? 0x00 : 0x01),
      0x0f,
      0x00, 0x00,  // CRC, filled below
  };
  ESP_LOGI(TAG, "[%s] Writing prevent display sleep: %s", this->parent_->address_str(),
           state ? "ON" : "OFF");
  this->finalize_and_send_(frame, sizeof(frame), sizeof(frame));
}

void Alpha2::write_min_flow_limit_value(float cubic_meters_per_hour) {
  // Register 9b560259, value-setting write found 2026-09-04 (later same day
  // as the enable/disable toggle above) - CONFIRMED with an exact 0.3 m3/h
  // match against a real capture. Same frame shape as write_min_flow_limit
  // but with a live-computed float target instead of the fixed echo, and
  // enable forced to 1 (setting a value implies wanting it active).
  uint8_t frame[35] = {
      0x27, 0x1f, 0xe7, 0xf8, 0x0a, 0x9b, 0x56, 0x02, 0x59, 0x03, 0x7f, 0x01, 0x00, 0x00, 0x12, 0x02, 0x01,
      // bytes 17-20: target, filled below
      0x00, 0x00, 0x00, 0x00,
      // bytes 21-32: constant tail (0.5, 2.0, 0.0)
      0x3f, 0x00, 0x00, 0x00, 0x40, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
      0x00, 0x00,  // CRC, filled below
  };
  float raw = cubic_meters_per_hour / 3600.0F;
  uint32_t bits;
  std::memcpy(&bits, &raw, sizeof(bits));
  bits = htonl(bits);
  std::memcpy(frame + 17, &bits, sizeof(bits));
  ESP_LOGI(TAG, "[%s] Writing min flow limit value: %.2f m3/h (raw=%.6f)", this->parent_->address_str(),
           cubic_meters_per_hour, raw);
  this->finalize_and_send_(frame, sizeof(frame), 20);
}

void Alpha2::write_max_flow_limit_value(float cubic_meters_per_hour) {
  // Register 9b560258, value-setting write found 2026-09-04 - CONFIRMED
  // with an exact 1.5 m3/h match against a real capture. Same pattern as
  // write_min_flow_limit_value above.
  uint8_t frame[35] = {
      0x27, 0x1f, 0xe7, 0xf8, 0x0a, 0x9b, 0x56, 0x02, 0x58, 0x03, 0x7f, 0x01, 0x00, 0x00, 0x12, 0x01, 0x01,
      // bytes 17-20: target, filled below
      0x00, 0x00, 0x00, 0x00,
      // bytes 21-32: constant tail (0.5, 2.0, 0.0)
      0x3f, 0x00, 0x00, 0x00, 0x40, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
      0x00, 0x00,  // CRC, filled below
  };
  float raw = cubic_meters_per_hour / 3600.0F;
  uint32_t bits;
  std::memcpy(&bits, &raw, sizeof(bits));
  bits = htonl(bits);
  std::memcpy(frame + 17, &bits, sizeof(bits));
  ESP_LOGI(TAG, "[%s] Writing max flow limit value: %.2f m3/h (raw=%.6f)", this->parent_->address_str(),
           cubic_meters_per_hour, raw);
  this->finalize_and_send_(frame, sizeof(frame), 20);
}

void Alpha2::write_venting(bool start, uint8_t venting_type) {
  // The app sends THREE writes together (within ~40-75ms) to actually
  // trigger a venting state change, not just the 560009 SET write alone:
  //   START:  580264 (unknown payload) -> 560009 (0x80=start) -> 580263 (=venting_type)
  //   CANCEL: 580264 (unknown payload) -> 560065 (unknown payload) -> 560009 (0x00=cancel)
  // The 580264/560065 payloads aren't understood well enough to compute
  // live, so those two are verbatim replays (real captured bytes) around
  // the live-computed 560009 (and, for start, 580263) writes.
  // venting_type only matters when start=true - see the write_venting()
  // header comment for what's confirmed
  // about it (0x01/0x02, "Pump"/"System+Pump" hypothesis).
  if (start) {
    static const uint8_t frame_580264_start[] = {0x27, 0x12, 0xe7, 0xf8, 0x0a, 0x8e, 0x58, 0x02, 0x64, 0x03, 0xcb,
                                                  0x01, 0x00, 0x00, 0x05, 0x01, 0x00, 0x00, 0x00, 0x00, 0x35, 0x6b};
    this->send_request_(const_cast<uint8_t *>(frame_580264_start), 20);
    this->send_request_(const_cast<uint8_t *>(frame_580264_start) + 20, 2);
  } else {
    static const uint8_t frame_580264_cancel[] = {0x27, 0x12, 0xe7, 0xf8, 0x0a, 0x8e, 0x58, 0x02, 0x64, 0x03, 0xcb,
                                                   0x01, 0x00, 0x00, 0x05, 0x00, 0x02, 0x00, 0x05, 0x04, 0xcd, 0x23};
    this->send_request_(const_cast<uint8_t *>(frame_580264_cancel), 20);
    this->send_request_(const_cast<uint8_t *>(frame_580264_cancel) + 20, 2);

    static const uint8_t frame_560065[] = {0x27, 0x11, 0xe7, 0xf8, 0x0a, 0x8d, 0x56, 0x00, 0x65, 0x01, 0x4a,
                                            0x01, 0x00, 0x00, 0x04, 0x00, 0x00, 0x00, 0x00, 0x6b, 0xca};
    this->send_request_(const_cast<uint8_t *>(frame_560065), 20);
    this->send_request_(const_cast<uint8_t *>(frame_560065) + 20, 1);
  }

  uint8_t frame[24] = {
      0x27, 0x14, 0xe7, 0xf8, 0x0a, 0x90, 0x56, 0x00, 0x09, 0x01, 0x2f, 0x01, 0x00, 0x00, 0x07,
      static_cast<uint8_t>(start ? 0x80 : 0x00),
      0x00, 0x02, 0x45, 0xbb, 0x80, 0x00,
      0x00, 0x00,  // CRC, filled below
  };
  ESP_LOGI(TAG, "[%s] %s venting (register 560009, split 20+4)", this->parent_->address_str(),
           start ? "Starting" : "Cancelling");
  this->finalize_and_send_(frame, sizeof(frame), 20);

  if (start) {
    // Venting type (2026-09-04): found this byte varies - 0x01 seen in
    // earlier tests (never explicitly labeled, assumed "Pump" by default),
    // 0x02 confirmed via a capture the user explicitly described as
    // "system+pump on" -. Live-computed
    // CRC verified byte-for-byte against BOTH real captured variants.
    uint8_t frame_580263[24] = {
        0x27, 0x14, 0xe7, 0xf8, 0x0a, 0x90, 0x58, 0x02, 0x63, 0x03, 0xca, 0x01, 0x00, 0x00, 0x07,
        venting_type,
        0x39, 0x25, 0x62, 0xcb, 0x00, 0x0a,
        0x00, 0x00,  // CRC, filled below
    };
    this->finalize_and_send_(frame_580263, sizeof(frame_580263), 20);
  }
}

void Alpha2::write_operating_mode(uint8_t code) {
  // Builds a fresh 24-byte Class-10 SET frame (opspec 0x90, DataObject
  // 56000a) for an arbitrary operating mode code, with a live-computed CRC
  // (calculate_crc_, confirmed correct against real captured frames - see
  // write_pressure_setpoint/write_flow_setpoint, both proven to work with
  // arbitrary computed values on real hardware). Used by the operating mode
  // select entity. Confirmed working for all known codes, including both
  // AutoAdapt sub-modes (0x0d Proportional pressure+AutoAdapt, 0x0e Konstant
  // tryck+AutoAdapt).
  uint8_t frame[24] = {
      0x27, 0x14, 0xe7, 0xf8, 0x0a, 0x90, 0x56, 0x00, 0x0a, 0x01, 0x2f,
      0x01, 0x00, 0x00, 0x07, 0x00, 0x06, code, 0x7f, 0xff, 0xff, 0xff,
      0x00, 0x00,  // CRC, filled below
  };
  ESP_LOGI(TAG, "[%s] Writing operating mode code=0x%02x (computed CRC, split 20+4)", this->parent_->address_str(),
           code);
  this->finalize_and_send_(frame, sizeof(frame), 20);
}

void Alpha2::write_setpoint_raw_(uint8_t sub_id, uint8_t feedback_id, float raw, const uint8_t *tail12,
                                 const char *what) {
  // Class-10 SET of a type-302 setpoint object (86/sub_id), 27 data bytes:
  //   [56 00 sub 01 2e 01 00 00 12 00 feedback][set point float32 BE][3 controller floats]
  // Sent split 20 + 15 like the app does.
  uint8_t frame[35] = {0x27, 0x1f, 0xe7, 0xf8, 0x0a, 0x9b, 0x56, 0x00, sub_id, 0x01, 0x2e, 0x01, 0x00,
                       0x00, 0x12, 0x00, feedback_id};
  uint32_t bits;
  std::memcpy(&bits, &raw, sizeof(bits));
  bits = htonl(bits);
  std::memcpy(frame + 17, &bits, sizeof(bits));
  std::memcpy(frame + 21, tail12, 12);
  ESP_LOGI(TAG, "[%s] Writing %s (86/%u): raw=%.1f", this->parent_->address_str(), what, sub_id, raw);
  this->finalize_and_send_(frame, sizeof(frame), 20);
}

void Alpha2::write_pressure_setpoint(float meters) {
  // Proportional pressure Hset, 86/18. Verified on hardware.
  this->pp_setpoint_raw_ = meters / ALPHA2_PA_TO_M;
  this->write_setpoint_raw_(0x12, 0x04, this->pp_setpoint_raw_, ALPHA2_TAIL_PP, "proportional pressure setpoint");
}

void Alpha2::write_const_pressure_setpoint(float meters) {
  // Constant pressure set point, 86/16 (frame layout from Grundfos GO
  // captures, see Birkemosen/esphome-alpha2go docs/ble-traffic.md).
  this->cp_setpoint_raw_ = meters / ALPHA2_PA_TO_M;
  this->write_setpoint_raw_(0x10, 0x04, this->cp_setpoint_raw_, ALPHA2_TAIL_CP, "constant pressure setpoint");
}

void Alpha2::write_flow_setpoint(float cubic_meters_per_hour) {
  // Frame shape confirmed 2026-09-04.:
  // Class-10 SET, DataObject 0x56 sub-index 0x28, 27 bytes of data:
  //   [56 00 28 01 2e 01 00 00 12 00 06][target float32 BE][0.6][1.6][0.4]
  uint8_t frame[35] = {
      0x27, 0x1f, 0xe7, 0xf8, 0x0a, 0x9b, 0x56, 0x00, 0x28, 0x01, 0x2e, 0x01, 0x00,
      0x00, 0x12, 0x00, 0x06,
      // bytes 17-20: target, filled below
      0x00, 0x00, 0x00, 0x00,
      // bytes 21-32: constant tail (0.6, 1.6, 0.4)
      0x3f, 0x19, 0x99, 0x9a, 0x3f, 0xcc, 0xcc, 0xcd, 0x3e, 0xcc, 0xcc, 0xcd,
      // bytes 33-34: CRC, filled below
      0x00, 0x00,
  };
  float raw = cubic_meters_per_hour / 3600.0F;
  uint32_t bits;
  std::memcpy(&bits, &raw, sizeof(bits));
  bits = htonl(bits);
  std::memcpy(frame + 17, &bits, sizeof(bits));

  ESP_LOGI(TAG, "[%s] Writing flow setpoint: %.2f m3/h (raw=%.6f)", this->parent_->address_str(),
           cubic_meters_per_hour, raw);
  this->finalize_and_send_(frame, sizeof(frame), 20);
}

void Alpha2::write_speed_setpoint(float rpm) {
  // Frame shape found 2026-09-04. in a
  // Constant curve-mode capture: DataObject 0x56 sub-index 0x0e, same 27-byte
  // Class-10 SET template as pressure/flow, but with a 0xff prefix byte
  // (vs 0x04/0x06) and a constant tail of (1.0, 1.0, 1.0) instead of a
  // physical range. Scale UNCONFIRMED - two captured raw values (2502.8,
  // 3161.1) were assumed to be raw RPM directly (no conversion), based on
  // the user's own estimate that normal operation is "around 2500 rpm" of
  // a ~5200 rpm max. Needs on-hardware confirmation against the app's
  // displayed varvtal/H% before trusting this blindly.
  uint8_t frame[35] = {
      0x27, 0x1f, 0xe7, 0xf8, 0x0a, 0x9b, 0x56, 0x00, 0x0e, 0x01, 0x2e, 0x01, 0x00,
      0x00, 0x12, 0x00, 0xff,
      // bytes 17-20: target, filled below
      0x00, 0x00, 0x00, 0x00,
      // bytes 21-32: constant tail (1.0, 1.0, 1.0)
      0x3f, 0x80, 0x00, 0x00, 0x3f, 0x80, 0x00, 0x00, 0x3f, 0x80, 0x00, 0x00,
      // bytes 33-34: CRC, filled below
      0x00, 0x00,
  };
  float raw = rpm;
  uint32_t bits;
  std::memcpy(&bits, &raw, sizeof(bits));
  bits = htonl(bits);
  std::memcpy(frame + 17, &bits, sizeof(bits));

  ESP_LOGI(TAG, "[%s] Writing speed setpoint: %.1f rpm (raw=%.1f)", this->parent_->address_str(), rpm, raw);
  this->finalize_and_send_(frame, sizeof(frame), 20);
}

#endif  // USE_SWITCH

#if defined(USE_SELECT) && defined(USE_SWITCH)
void Alpha2::write_panel_setting(const std::string &label) {
  int idx = -1;
  for (uint8_t i = 0; i < ALPHA2_PANEL_OPTION_COUNT; i++) {
    if (label == ALPHA2_PANEL_OPTIONS[i]) {
      idx = i;
      break;
    }
  }
  if (idx < 0 || idx == 11) {  // unknown / "Advanced": nothing to write
    this->update_panel_setting_state_();
    return;
  }
  if (idx == 9) {
    this->write_operating_mode(14);  // constant pressure + AutoAdapt
  } else if (idx == 10) {
    this->write_operating_mode(13);  // proportional pressure + AutoAdapt
  } else {
    if (!(this->preset_valid_ & (1 << idx))) {
      ESP_LOGW(TAG, "[%s] Panel setting '%s' not read from the pump yet, try again in a minute",
               this->parent_->address_str(), label.c_str());
      this->update_panel_setting_state_();
      return;
    }
    // Same sequence as Grundfos GO: control mode first, then the set point.
    uint8_t mode = this->preset_mode_[idx];
    float raw = this->preset_raw_[idx];
    this->write_operating_mode(mode);
    delay(60);
    if (mode == 0) {
      this->cp_setpoint_raw_ = raw;
      this->write_setpoint_raw_(0x10, 0x04, raw, ALPHA2_TAIL_CP, label.c_str());
    } else if (mode == 1) {
      this->pp_setpoint_raw_ = raw;
      this->write_setpoint_raw_(0x12, 0x04, raw, ALPHA2_TAIL_PP, label.c_str());
    } else if (mode == 2) {
      this->cc_setpoint_raw_ = raw;
      this->write_setpoint_raw_(0x0e, 0xff, raw, ALPHA2_TAIL_CC, label.c_str());
    }
  }
  this->last_panel_label_ = nullptr;  // re-publish from the next readback
  this->panel_setting_select_->publish_state(label);
}

void Alpha2::write_curve(const std::string &label) {
  // Class 3 commands: START (6) = normal operation, MIN (25), MAX (26).
  uint8_t cmd = label == "Min curve" ? 25 : (label == "Max curve" ? 26 : 6);
  ESP_LOGI(TAG, "[%s] Curve: %s (command %u)", this->parent_->address_str(), label.c_str(), cmd);
  this->send_commands_(&cmd, 1);
  this->curve_select_->publish_state(label);
}
#endif

}  // namespace alpha2
}  // namespace esphome

#endif
