# ESPHome component for the Grundfos ALPHA2 GO

Read and control a **Grundfos ALPHA2 GO** circulator pump over Bluetooth Low Energy from an ESP32 running [ESPHome](https://esphome.io): measurements, operating mode, setpoints, settings, venting, and active alarms/warnings plus the pump's event log.

> **Unofficial and without warranty.** Not made, endorsed or supported by Grundfos. Everything here was worked out by observing the BLE traffic of the Grundfos GO app, not from protocol documentation, and has been tested on one ALPHA2 GO (XX-60) only. Writing to the pump changes how your heating system runs. Use at your own risk – see sections 15 and 16 of the [GPL-3.0](LICENSE).

## Features

| What | Read | Write | Entity |
| --- | :---: | :---: | --- |
| Flow, head, speed, power, voltage, current | ✓ | – | `sensor` |
| Calculated media temperature, number of starts, Kv, DC link voltage, differential/converter/inverter temperature¹ | ✓ | – | `sensor` |
| Status: control source, operation mode, control mode, AutoAdapt, set point running right now | ✓ | – | `text_sensor`, `binary_sensor`, `sensor` |
| **Active warning / active alarm** (code and text) | ✓ | – | `sensor` (code), `text_sensor` (text) |
| **Last event-log entry** with operating time | ✓ | – | `text_sensor` |
| Operating mode: constant pressure, proportional pressure, constant curve, constant flow, AutoAdapt variants | ✓ | ✓ | `select` |
| Panel settings I/II/III (constant curve, constant pressure, proportional pressure) and AutoAdapt | ✓ | ✓ | `select` |
| Normal / min curve / max curve | ✓ | ✓ | `select` |
| Constant pressure, proportional pressure, flow and speed setpoints | ✓ | ✓ | `number` |
| Min/max flow limitation (enable + value) | ✓ | ✓ | `switch` + `number` |
| External (PWM) control, night setback, panel lock, prevent display sleep, continuous air detection and venting | ✓ | ✓ | `switch` |
| Pump start/stop | – | ✓ | `switch` |
| Manual venting (pump / system + pump), cancel | ✓ | ✓ | `binary_sensor` + `write_venting()` |
| Product info (type, product no., serial, firmware, …) | ✓ | – | `text_sensor` |

¹ The temperatures are reported as NaN (entity stays `unknown`) by some variants, e.g. the XX-60.

A "duty point" (flow + head) in Grundfos GO is not a separate setting: the app converts it into an ordinary constant- or proportional-pressure set point, which you can set directly here.

## Requirements

- An ESP32 with BLE, using the **ESP-IDF** framework. Developed on an ESP32-C6. Give the pump its own ESP: a busy `bluetooth_proxy` on the same chip made the link unreliable.
- `esp32_ble: auth_req_mode: sc_bond`. The pump only accepts writes over a bonded LE Secure Connections link, which is what the app uses too. Read-only use works without it.
- **The pump accepts one BLE connection at a time.** While Grundfos GO is connected (or a phone keeps the connection open in the background), the ESP cannot connect, and vice versa. Fully close the app when you are done with it.

## Installation

```yaml
external_components:
  - source: github://drgreenbaum/esphome-alpha2go
    components: [alpha2]

esp32_ble_tracker:

esp32_ble:
  auth_req_mode: sc_bond

ble_client:
  - mac_address: XX:XX:XX:XX:XX:XX
    id: alpha2_ble

sensor:
  - platform: alpha2
    id: alpha2_pump
    ble_client_id: alpha2_ble
    flow:
      name: "Flow"
    head:
      name: "Head"
    power:
      name: "Power"
    warning_code:
      name: "Warning code"
```

[`example-alpha2go.yaml`](example-alpha2go.yaml) is a complete configuration with every entity.

## Configuration

### `sensor` (hub)

The `sensor` platform creates the hub (`id`), which the other platforms reference with `alpha2_id`. It also takes `ble_client_id` and `update_interval` (default `15s`). All keys are optional, and only the registers behind configured entities are polled.

| Key | Unit | Notes |
| --- | --- | --- |
| `flow`, `head` | m³/h, m | |
| `speed`, `power`, `voltage`, `current` | rpm, W, V, A | |
| `media_temperature` | °C | Calculated by the pump |
| `start_count` | – | Total number of starts |
| `warning_code`, `alarm_code` | – | First active code, `0` = none |
| `flow_coefficient_kv`, `dc_link_voltage` | –, V | |
| `differential_temperature`, `converter_temperature`, `inverter_temperature` | K, °C, °C | NaN on some variants |
| `setpoint_head`, `setpoint_speed`, `setpoint_flow` | m, rpm, m³/h | Set point the pump runs right now; only the active mode's value is set |
| `operating_mode`, `external_control`, `night_setback`, `venting_status`, `venting_countdown` | – | Raw diagnostic values; prefer the select/switches |

### `text_sensor`

`active_warning`, `active_alarm` (e.g. `25 – Incorrect PWM configuration`, `None` when empty), `last_event` (e.g. `35 – Air in pump head (operating time 11 d 4 h 14 min)`), `control_source` (e.g. `Local (panel or app)`, `PWM`, `AutoNight`), `operation_mode` (`Auto`, `Stop`, `Min curve`, `Max curve`, …), `control_mode`, plus product info: `product_type`, `product_no`, `serial_no`, `production_code`, `gsc_description`, `gsc_identification`, `app_software`, `ble_software`.

#### Translating texts

All status and event texts are English by default. `text_labels` (on any `text_sensor: platform: alpha2` block) replaces them, and the selects take `option_labels` the same way:

```yaml
text_sensor:
  - platform: alpha2
    alpha2_id: alpha2_pump
    text_labels:
      "Proportional pressure": "Proportionellt tryck"
      "Incorrect PWM configuration": "Felaktig PWM-konfiguration"
      "None": "Ingen"
      "operating time": "drifttid"
    control_mode:
      name: "Styrläge"

select:
  - platform: alpha2
    type: curve
    alpha2_id: alpha2_pump
    name: "Kurva"
    option_labels:
      "Min curve": "Minkurva"
      "Max curve": "Maxkurva"
```

The keys are the English texts exactly as listed in this README; `option_labels` keys are validated.

### `binary_sensor`

`venting_active` – a manual venting cycle is running. `autoadapt` – an AutoAdapt control mode is active.

### `switch` (`type:`)

`power` (start/stop), `external_control`, `night_setback`, `min_flow_limit`, `max_flow_limit`, `continuous_venting`, `panel_lock`, `prevent_display_sleep`.

### `select` (`type:`)

| Type | Options |
| --- | --- |
| `operating_mode` (default) | `Constant pressure`, `Proportional pressure`, `Constant curve`, `Constant flow`, `Proportional pressure + AutoAdapt`, `Constant pressure + AutoAdapt` |
| `panel_setting` | `Constant curve I`–`III`, `Constant pressure I`–`III`, `Proportional pressure I`–`III`, the two AutoAdapt settings, and `Advanced` (any other set point) |
| `curve` | `Normal`, `Min curve`, `Max curve` |

All three follow what the pump reports, so changes made on the panel or in the app show up too. `panel_setting` uses the pump's own I/II/III values (read from the pump, so they match your variant); they are read one per poll cycle and each value must be read twice before it is used, so a selection made within the first ~4.5 minutes after boot is refused with a log warning. `Normal` sends START, which also starts a stopped pump.

### `number` (`type:`)

| Type | Range | Effective in |
| --- | --- | --- |
| `constant_pressure_setpoint` | 1.0–4.5 m | Constant pressure |
| `proportional_pressure_setpoint` | 1.0–4.5 m | Proportional pressure (`pressure_setpoint` is a deprecated alias) |
| `flow_setpoint` | 0.3–2.9 m³/h | Constant flow |
| `speed_setpoint` | 2163–5163 rpm | Constant curve |
| `min_flow_limit`, `max_flow_limit` | 0.25–3.5 m³/h | Setting a value also enables the limit |

The ranges are the ones Grundfos GO offers for an XX-60. Check your variant's limits in the I/O manual.

### Venting

```yaml
button:
  - platform: template
    name: "Vent pump"
    on_press:
      - lambda: id(alpha2_pump)->write_venting(true, 0x01);  # 0x02 = system + pump
  - platform: template
    name: "Cancel venting"
    on_press:
      - lambda: id(alpha2_pump)->write_venting(false);
```

## Alarm and warning codes

Codes as listed in the ALPHA2 GO installation and operating instructions (chapter 11):

| Panel | Code | Meaning | Pump |
| --- | --- | --- | --- |
| E1 | 51 | Blocked pump | Alarm, stops |
| E2 | 40 | Undervoltage | Alarm, stops |
| E3 | 4 | Overvoltage | Alarm, stops |
| E3 | 29 | Forced pumping | Alarm, stops |
| E3 | 43 | Forced pumping | Warning, runs |
| E3 | 72, 76, 85 | Internal fault | Alarm, stops |
| E3 | 132 | Pump configuration corrupted or missing | Alarm, stops |
| E4 | 57 | Dry running | Alarm, stops |
| E9 | 25 | Incorrect PWM configuration | Warning, runs |
| – | 35 | Air in media (log only) | Warning, runs |

Warning 25 and the event log have been verified on hardware. The split between the warning list (`58000b`) and the alarm list (`580000`) is inferred: the live warning appeared only in `58000b`. A real alarm has not been observed yet.

## Troubleshooting

- **`auth fail reason=82` / data stops after some days.** Restarting the ESP has fixed it every time so far. Power-cycling the pump helped once. An automation that presses a `restart` button when e.g. `power` has been `unknown` for 15 minutes works well.
- **Entities stay `unknown` after flashing.** Make sure no phone is still connected to the pump (see Requirements).
- **E9 / code 25 on the display.** The pump sees a signal on its PWM input while it is not configured for external control. Leave the PWM input disconnected (or low) unless you use it.

## Protocol notes

The pump speaks Grundfos GENIbus over a single GATT characteristic (service `0xFE5D`). Requests start with `0x27`, responses with `0x24`; CRC-16-CCITT (poly `0x1021`, init and final XOR `0xFFFF`) over everything after the start byte.

- **Reads** are Class 10 (`0x0a`) GET requests, `27 07 e7 f8 0a 03 <obj> <sub-id hi> <sub-id lo> <crc>`. Responses are matched on an 8-byte type header, and several registers share one header and are told apart by a data byte.
- **Writes** are Class 10 SET frames. They **must be split into two BLE writes** with the same fragment sizes the app uses (20 + 4 bytes for the 24-byte frames, 20 + 15 for the 35-byte frames). Sent as a single write, the pump ignores them. That was the cause of every early write failure, not CRC, bonding or session state.
- **Start/stop** uses Class 3 commands (REMOTE + START/STOP); min/max curve are the single commands MIN (25) / MAX (26), and START (6) returns to normal.
- **Set points** are type-302 objects: `86/16` constant pressure, `86/18` proportional pressure, `86/14` constant curve (rpm), `86/40` constant flow (m³/s). Pressure is in Pa (1 m = 9804 Pa). The three controller floats after the value differ per object and are written back unchanged. CP and PP answer with the same type and feedback id, so only one of them is requested per cycle.
- **Status** objects `[source][operation mode][control mode][set point]`: `86/10` local control mode, `86/6` local request, `86/8` realized request (what the pump runs). The panel settings are `86/351`–`359` (`[control mode][set point]`); choosing one in the app writes the control mode (`86/10`) and then the set point.
- **Alarms and warnings:** `58000b` (active warnings) and `580000` (active alarms) answer `[count u16][code u16]…` with the same response type, so only one is requested per poll cycle. The event log is `5827`: index `d7` is the header (newest sequence number, 20 entries) and `d8`–`eb` is a 20-slot ring buffer with `[00][seq][00][code][0000][type 03/04][operating seconds u32 BE][0000]`. Slot = `(seq − 1) mod 20`.

Register numbers for the other entities are documented next to their constants in [`alpha2.h`](components/alpha2/alpha2.h).

## Credits and license

- Based on [parameter-pollution/esphome-grundfos-alpha2-go](https://github.com/parameter-pollution/esphome-grundfos-alpha2-go), which adapted ESPHome's [`alpha3`](https://github.com/esphome/esphome/tree/dev/esphome/components/alpha3) component to the ALPHA2 GO's measurement registers.
- The constant-pressure set point, panel settings, status objects, min/max curve commands and the extra measurement offsets follow [Birkemosen/esphome-alpha2go](https://github.com/Birkemosen/esphome-alpha2go)'s BLE traffic documentation (GPL-3.0), checked against this project's own captures and tested on an XX-60. It is an independent implementation worth a look too.
- Licensed under the [GNU GPL v3](LICENSE).
