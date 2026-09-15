# Local firmware test audit — 2026-09-14

The working tree contained unpublished changes to the BM83 driver, application,
FM state, renderer, platform initialization and `test_main.c`. The metadata and
connection recovery work has been retained and extended with regression tests.
The renderer uses the layout from commit `a87ce5a`, retaining the existing
button footer and partial station-name updates.

Additional ignored files found locally:

- `tests/build/fmoffs.c`, `liveoffs.c`, `offsets.c`, `target_offsets.c`, and
  `target_offsets2.c`: scratch programs that emit application/MCU structure
  offsets for debugger inspection. Their assembly and executables are generated
  artifacts; offsets must be regenerated whenever the structures change.
- `tests/build/blobject_tests.exe` and `kiku_tests.exe`: old/native test binaries.
- `BM83_tools/blobject_bm83_host_i2s1.log`: Config Tool settings export, including
  I2S master mode and 24-bit data. It is not a live UART metadata capture.
- `build/`: compiled firmware and dependency/stack-usage artifacts.
- `BM83_eeprom_0000_012f.bin` is already tracked but empty; it cannot establish
  what EEPROM configuration is actually programmed on the module.

The scratch files were left in place. Executable regressions live in
`test_main.c` and run with `python Firmware/tools/test_host.py`. The suite now
also compiles the application code and checks legacy metadata response parsing,
version-dependent commands, fallback from every browsing timeout, repeated and
out-of-order 2B RadioText, rejected corrupt text, and metadata-driven rendering.

Software checks cannot confirm reception from a physical station or phone.
On-device follow-up: tune a station that broadcasts RadioText, wait for its text
cycle, connect a phone and change songs, then disconnect/reconnect Bluetooth.
Confirm title/artist refresh and existing button actions on the restored UI.
