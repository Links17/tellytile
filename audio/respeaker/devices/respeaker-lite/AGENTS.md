# reSpeaker Lite (XIAO ESP32-S3)

## Overview

XMOS I2S audio, BOOT input and RGB LED.

## Profile

| Field | Value |
| --- | --- |
| ID | respeaker-lite |
| Format | 5 |
| Revision | unknown |
| Maintenance | unassigned |
| Maturity | experimental |

## Usage

Use the listed standalone examples as the supported development entry points.

## Physical layout

unknown; revision-specific orientation and imagery have not been verified.

## Hardware notes

### Hardware facts

- Module: XIAO ESP32-S3, 8 MB flash (DIO 80 MHz), 8 MB octal PSRAM (80 MHz).
  Source: reference `../../devices/respeaker-lite/targets/sdkconfig.defaults`.
- Button: XIAO BOOT button on GPIO 0. Source: the native pins/configuration and vendor locations below.
- Audio: XMOS-processed microphone over 16 kHz I2S. The XMOS must run
  Seeed's **16 kHz I2S firmware v1.0.9**; the USB-audio and 48 kHz I2S
  images do not work. Flash the XMOS through the larger board's USB port
  per Seeed's instructions — flashing the ESP32 does not update the XMOS.
  Source: reference `../../devices/respeaker-lite/targets/bsp/include/bsp_pins.h`.
- Status: RGB status LED. Source: the native pins/configuration and vendor locations below.
- USB console: connect to the **small XIAO board**; it exposes the ESP32's
  USB Serial/JTAG console (`303a:1001`). Source: the native pins/configuration and vendor locations below.
- RF: attach the XIAO's external antenna; 2.4 GHz Wi-Fi only. Source: the native pins/configuration and vendor locations below.


### Vendor source

[respeaker/reSpeaker_Lite][vendor]:
`doc/images/pinout.png`, I2C examples, `xmos_firmwares/`.

### Source locations

Primary links identify vendor sources; inspect the stated files and revisions before changing hardware facts.

| Board | Vendor source | Where to look |
| --- | --- | --- |
| reSpeaker Lite | [respeaker/reSpeaker_Lite][vendor] | `doc/images/pinout.png`, the I2C examples, `xmos_firmwares/`. See also Seeed wiki pages for the XIAO ESP32-S3 combination. |

Native pin citations: [bsp_pins.h][bsp-pins]. Software configuration and examples belong to the [Target][readme].

## Implementation references

The [esp-idf/respeaker-lite Target][readme] provides the native reference implementation. Header and example names below are relative to that Target. Keep hardware behavior and implementation guidance here; the Target owns executable code, software-stack bindings and build configuration.

### Reference interfaces
| Header | Native interface |
|---|---|
| `bsp_audio.h` | XMOS + AIC3204 over I2C, 16 kHz I2S slave capture/playback |
| `bsp_led.h` | WS2812 RGB LED |
| `bsp_button.h` | BOOT button with debounce task |

Pin facts with sources: [bsp_pins.h][bsp-pins].

### Reference examples

- `audio_meter` — XMOS mic RMS level, mirrored on the WS2812 (needs the
  16 kHz I2S XMOS firmware v1.0.9, see Verified facts).
- `button_led` — hold XIAO BOOT to cycle the WS2812 through red/green/blue.
- `wifi_scan` — 2.4 GHz AP scan, blinks the LED green each round (attach
  the external antenna).

## Specifications

### Controller

| Fact | Type | Value | Unit | Status | Source | Assertion / reason |
| --- | --- | --- | --- | --- | --- | --- |
| Flash capacity (`flash`) | integer | 8 | MB | documented | [device-notes][device-notes] | {value} MB flash |
| Controller (`mcu`) | string | XIAO ESP32-S3 | — | documented | [device-notes][device-notes] | Module: {value}, |
| PSRAM capacity (`psram`) | integer | 8 | MB | documented | [device-notes][device-notes] | {value} MB octal PSRAM |

### Connectivity

| Fact | Type | Value | Unit | Status | Source | Assertion / reason |
| --- | --- | --- | --- | --- | --- | --- |
| Radio (`radio`) | string | 2.4 GHz Wi-Fi | — | documented | [device-notes][device-notes] | {value} only. |

### Inputs

| Fact | Type | Value | Unit | Status | Source | Assertion / reason |
| --- | --- | --- | --- | --- | --- | --- |
| Audio input (`audio`) | string | XMOS-processed microphone | — | documented | [device-notes][device-notes] | Audio: {value} over |
| Button (`button`) | string | XIAO BOOT button | — | documented | [device-notes][device-notes] | Button: {value} on |

### Outputs

| Fact | Type | Value | Unit | Status | Source | Assertion / reason |
| --- | --- | --- | --- | --- | --- | --- |
| Status light (`led`) | string | RGB status LED | — | documented | [device-notes][device-notes] | Status: {value}. |

### Power

| Fact | Type | Value | Unit | Status | Source | Assertion / reason |
| --- | --- | --- | --- | --- | --- | --- |
| Power (`power_details`) | string | — | — | unknown | — | No independently confirmed value is retained; consult the original reference for limitations. |

### Accessories

| Fact | Type | Value | Unit | Status | Source | Assertion / reason |
| --- | --- | --- | --- | --- | --- | --- |
| Accessories (`accessories_details`) | string | — | — | unknown | — | No independently confirmed value is retained; consult the original reference for limitations. |

## Limitations

- Physical board revision and fresh on-device verification are not recorded.
- Read the original hardware reference for unsupported drivers, prerequisites and known limitations.

## Physical acceptance

### Preconditions

Identify the actual unit, revision, required peripherals and authorized write scope. Run one selected native example at a time. Record source digest, toolchain, stimuli, actual observations and recovery; a plan or build is not a physical pass.

### identity

- Interfaces: none
- Source: [target][target]

Record board identity, observed revision, connected port/drive and approved write scope.

Expected: Identity and revision are unambiguous and match this Device.

### recovery

- Interfaces: none
- Source: [target][target]

After the functional cases, power-cycle/restart using the documented normal procedure.

Expected: Device returns to the documented working state; record failures and recovery steps.

### audio-meter

- Interfaces: audio, led
- Source: [target][target]

Run `audio_meter` under the confirmed write scope and observe the selected hardware.

Expected: XMOS mic RMS level, mirrored on the WS2812 (needs the
  16 kHz I2S XMOS firmware v1.0.9, see Verified facts).

### button-led

- Interfaces: input, led
- Source: [target][target]

Run `button_led` under the confirmed write scope and observe the selected hardware.

Expected: hold XIAO BOOT to cycle the WS2812 through red/green/blue.

### wifi-scan

- Interfaces: led
- Source: [target][target]

Run `wifi_scan` under the confirmed write scope and observe the selected hardware.

Expected: 2.4 GHz AP scan, blinks the LED green each round (attach
  the external antenna).

## Sources

- [device-notes][device-notes]
- [bsp-pins][bsp-pins]
- [vendor][vendor]

[device-notes]: AGENTS.md#hardware-notes
[target]: targets/target.json
[bsp-pins]: targets/bsp/include/bsp_pins.h
[vendor]: https://github.com/respeaker/reSpeaker_Lite
[readme]: targets/README.md
