# kiku release checklist

This document is the release gate for production firmware. A firmware build is
not a production release until every software gate passes and every applicable
hardware gate is signed off on the exact PCB/BOM/enclosure revision being sold.

## Automated software gate

Run from the repository root:

```text
python Firmware/tools/release_check.py --require-clean
```

The gate runs native regression tests, clean release and debug cross-builds,
checks first-party static stack frames, runs `git diff --check`, rebuilds the
release image last, and emits `Firmware/build/kiku-build-manifest.json` with
SHA-256 hashes and the exact compiler/Git identity.

Release only the final `kiku.hex`/`kiku.bin` that matches that manifest.

## Required functional regression

- Cold boot from battery at low, nominal and near-full cell voltage.
- 24-hour watchdog/reset soak with no unexplained resets.
- Bluetooth: first pairing, power-cycle reconnect, phone-side disconnect and
  reconnect, out-of-range/re-entry, AVRCP controls, metadata changes, pause /
  resume, and repeated source switching. The exact golden radio must sustain at
  least 250 successful physical `0x5D` metadata responses (comfortably beyond
  the former ~90-response failure depth) with no rising timeout/backpressure
  trend, while A2DP and AVRCP remain connected. Exercise multiple NEXT, PREV and
  source-side track changes and verify title/artist/album change atomically with
  no stale-field mixing.
- FM: all presets, manual tuning, seek in both directions, RDS/no-RDS stations,
  weak-signal behaviour, volume adjustment and source switching.
- microSD: empty socket, hot insert, hot removal during idle and playback,
  reinsertion, FAT32 cards at supported sizes, long filenames, malformed MP3,
  truncated MP3, unsupported files and a library larger than the indexed limit.
- Audio outputs: headphones, speaker, AUTO and BOTH modes; plug/unplug while
  playing; maximum-volume clipping/noise check.
- UI: every screen/menu item, encoder/button short/long press, power-save mode,
  SD shuffle, error/diagnostics screen and text truncation with long metadata.
- Settings: save, power loss during/after save, corrupt settings file and
  migration from the legacy development filename.

## Power / charging release blockers

- The current Rev-A USB-C fault must be electrically resolved. Firmware has
  already observed the BQ25895 alive while reporting no valid VBUS with USB-C
  connected; do not release hardware until J1/F1/protection/U2 VBUS is fixed.
- Verify the final board cannot negotiate or expose the 5-V-class VBUS
  protection to >5 V at any point, including the interval before MCU firmware
  starts. Rev-A currently relies partly on early BQ25895 configuration.
- Resolve the observed MAX17048-vs-BQ25895 battery-voltage disagreement and
  verify SOC against a calibrated DMM across charge/discharge cycles.
- Freeze the exact production cell, charge-current limit, thermistor/TS policy
  and enclosure thermal limits before enabling a production charge-current
  policy.

## Bluetooth production gate

- Use a compatible MSPK2 v1.3-or-newer **SPP Host-Mode** radio image. The v1.2
  development module is not a production golden unit: Microchip documents and
  fixes in v1.3 the defect where firmware stops responding to UART `0x4A` after
  many requests.
- Freeze and archive the exact BM83 radio firmware and Config Tool image used in
  production, including SHA-256 hashes, outside the public source repository.
- Record BM83 UART/FW/EEPROM/DSP versions from a golden unit.
- Confirm Host MCU mode, 115200 UART, A2DP sink, AVRCP and 48-kHz/24-bit I2S
  host-clock settings match the firmware contract.
- Disable OTA DFU if it is not an intentional product feature. If enabled,
  provision a non-default production secret outside Git and verify that no
  development/example key ships in the golden configuration.
- Verify pairing records survive MCU/BM83 resets and full battery disconnect.

## Manufacturing / programming gate

- Program only from the release manifest and verify flash after programming.
- Record unit serial/lot, firmware manifest hash and PCB revision.
- Run a fixture self-test covering display, controls, SD, codec/headphones,
  speaker, FM, Bluetooth, fuel gauge and charger status.
- Decide and document SWD access policy for shipped units (serviceable vs
  locked). Do not enable readout protection until the recovery/service process
  is validated.

## Mechanical / regulatory gate

- Final enclosure must preserve BM83 antenna keep-out and FM coax clearance.
- Validate speaker/battery restraint, drop/thermal behaviour and connector
  strain relief using production materials.
- Complete applicable electrical, EMC/RF, battery transport/safety and market
  compliance work for the jurisdictions in which kiku will be sold.
