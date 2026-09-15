# kiku firmware

Production-oriented firmware for the STM32H523VET6 board in the repository root. The KiCad schematic/PCB is the hardware source of truth; the firmware pin map is deliberately kept in `Core/Inc/board.h` and `Core/Src/board.c` rather than a generic development-board definition.

## Implemented product functions

- BM83 Bluetooth A2DP receive path over I2S2, Host MCU UART framing/ACK, MFB wake timing, AVRCP play/pause/next/previous and AVRCP 1.3 title/artist/album metadata requests.
- microSD library scan over 4-bit SDMMC1 with UTF-8 long filenames, hot-insert remount/rescan, PCM WAV playback and MP3 playback through minimp3. A missing card is non-fatal.
- SI4705 FM power-up, tune, seek, signal-quality polling and RDS Program Service/RadioText decoding. FM analogue L/R is routed into the TLV320AIC3104 ADC and looped through the I2S1 DAC/speaker path so both headphones and the internal speaker receive the selected station.
- TLV320AIC3104 I2S/headphone routing plus MAX98357A speaker control with selectable AUTO, headphones, speaker and both-output modes. AUTO uses the speaker when the jack is empty and headphones when a plug is inserted.
- BQ25895 charger status and MAX17048 battery state-of-charge monitoring. Rev A is forced to a 5-V-only, 500-mA input profile at the very start of platform initialisation: automatic D+/D- detection, HVDCP and MaxCharge negotiation are disabled before slow peripheral work. Charge-current programming remains disabled by default because the final cell/enclosure/thermistor limits cannot be derived from the PCB.
- ST7789 320x240 status/now-playing UI, PWM backlight, rotary encoder and three push controls.
- CRC-protected persistent settings (`KIKU.CFG` on microSD, with read-only migration from the older development filename), watchdog, non-destructive peripheral self-test API and recoverable optional-peripheral failures.
- FreeRTOS on Cortex-M33, HAL timebase isolated on TIM6, DMA-capable peripheral setup and exact STM32H523 startup/linker configuration.

## Controls

| Control | Local audio | FM | Bluetooth |
| --- | --- | --- | --- |
| Encoder | Volume | Volume by default; fine tune when toggled | Volume |
| Encoder press | Open SD/library menu | Toggle FM knob between volume/tune | Return to sources |
| Encoder long press | Return to sources | Return to sources | Return to sources |
| Button 1 | Play/pause | Previous preset | Play/pause |
| Button 1 long press | Switch to local | Seek down | Pair if disconnected; otherwise local |
| Button 2 | Next/random track | Next preset | Next track |
| Button 2 long press | Switch to FM | Seek up | Switch to FM |

Shuffle is owned by the SD/library menu rather than global Settings. It changes
local next-track selection only and is persisted with the other user settings.

When Bluetooth is selected, firmware automatically reconnects the last paired
A2DP device with bounded backoff. Normal BM83 UART traffic does not pulse MFB;
BM83 does not expose the `UART_RX_IND` function used by earlier BM62/BM64 parts.
If no device reconnects, the Bluetooth screen shows `HOLD B1 TO PAIR`; holding
Button 1 enters BM83 pairing mode without clearing existing pairing records.

## Required BM83 configuration

The PCB gives the BM83 its own digital-audio clock path but does not route BM83 MCLK to the STM32. This firmware therefore expects the BM83 configuration image to use:

- Host MCU mode enabled;
- UART at 115200 baud;
- A2DP sink and AVRCP enabled;
- I2S audio output at 48 kHz, 24-bit stereo;
- BM83 acting as the I2S BCLK/LRCLK host (STM32 I2S2 is configured as slave receive).

The connected development BM83 has now been measured as MSPK2 firmware v1.2.
That image is suitable for bench compatibility testing but is **not** the kiku
production radio baseline: Microchip's MSPK2 v1.3 release notes explicitly fix
the defect where firmware stops responding to UART `0x4A` after many requests.
Use an SPP Host-Mode MSPK2 v1.3-or-newer compatible image for a golden unit,
then archive its exact firmware/configuration hashes with manufacturing records.
Vendor tools, documentation and generated BM83 images are intentionally not
redistributed by this public repository; see [`BM83_tools/README.md`](BM83_tools/README.md).

If OTA DFU is not a deliberate product feature, disable it in the BM83
configuration. If it is shipped, replace all example/default credentials with a
production secret provisioned outside source control.

## Build and test

Dependencies are checked in or pinned as Git submodules. On Windows the build script automatically finds the GNU Arm toolchain installed by PlatformIO; elsewhere `arm-none-eabi-gcc`, `objcopy` and `size` can be on `PATH` or supplied through `ARM_GCC_PREFIX`.

```text
git submodule update --init --recursive
python Firmware/tools/test_host.py
python Firmware/tools/build.py --clean
```

Outputs are written to `Firmware/build/`:

- `kiku.elf`
- `kiku.bin`
- `kiku.hex`
- `kiku.map`

The same host tests plus clean release and debug cross-builds run in GitHub Actions. The workflow rebuilds release mode last so its uploaded artifacts are the production binaries.

For a release candidate, run `python Firmware/tools/release_check.py --require-clean`.
The build also emits `kiku-build-manifest.json` containing SHA-256 hashes, the
compiler identity and Git revision. The complete software/hardware sign-off is
in [`RELEASE_CHECKLIST.md`](RELEASE_CHECKLIST.md); a passing build alone does
not waive unresolved hardware validation.

## Programming

This board is programmed through the 10-pin ARM SWD connector. The USB-C D+/D- lines terminate at the BQ25895 charging circuitry; STM32 PA11/PA12 are not connected on this hardware revision, so USB DFU/CDC/MSC is not available.

With an ST-Link and STM32CubeProgrammer, a typical command after building is:

```text
STM32_Programmer_CLI -c port=SWD -w Firmware/build/kiku.hex -v -rst
```

## Audio architecture

The MCU/codec audio domain is fixed at 48 kHz. Local files from 8-96 kHz are converted to 48 kHz by a deterministic nearest-neighbour resampler before I2S output. Bluetooth PCM arrives from BM83 on I2S2. SI4705 FM analogue audio enters TLV320AIC3104 LINE2; the codec ADC return is forwarded through I2S1 to the codec DAC and MAX98357A path. AUTO output disables the speaker amplifier whenever headphones are inserted; explicit Speaker and Both modes override that automatic policy. Software volume scaling is applied before I2S transmission.

The simple resampler is intentional for first hardware bring-up and correctness. A band-limited asynchronous resampler is the appropriate later upgrade if listening tests show the BM83 and codec clock domains need drift compensation or higher-quality sample-rate conversion.

## First hardware bring-up checks

The repository is fully build/test checked without attached hardware. These physical facts still require measurement on the assembled product and are kept isolated so they do not contaminate the protocol/board implementation:

1. Program and record a golden BM83 using a compatible MSPK2 v1.3-or-newer SPP
   Host-Mode image, confirm the interface requirements above, and verify
   long-duration clock-domain and metadata behaviour. The STM32 host is hardened
   against the v1.2 repeated-`0x4A` defect, but that does not make v1.2 an
   acceptable production radio baseline.
2. Confirm `HP_DET` polarity with the actual SJ1-3525N jack; change the single active-level argument in `Platform/Src/platform.c` if required.
3. Confirm ST7789 rotation, RGB/BGR order and inversion for the exact HS20HS072RX panel; adjust the startup sequence/offset constants only if the physical panel requires it.
4. Set BQ25895 charge current/input-current/thermal policy only after the exact LiPo cell rating, NTC network and enclosure thermal performance are known. `APP_BENCH_CHARGE_POLICY_ENABLE` remains `0` by default.
5. Rev A uses F1 = C883127 (6-V, 750-mA PTC) and a 5-V-class TVS on VBUS. The BQ25895 reset defaults permit D+/D- high-voltage-adapter negotiation, so firmware explicitly disables AUTO_DPDM/HVDCP/MaxCharge and fixes IINLIM at 500 mA before SD/Bluetooth/display initialisation. This greatly reduces risk when the MCU is running, but a future PCB revision should make the 5-V-only requirement true in hardware as well rather than relying solely on post-reset I2C configuration.

## Source layout

```text
Firmware/
  Core/        STM32H523 clocks, pins, HAL MSP/IRQs, FreeRTOS config
  Drivers/     BM83, BQ25895, MAX17048, SI4705, ST7789, TLV320AIC3104, audio I/O
  Audio/       minimp3 adapter
  Storage/     FatFs/SDMMC storage and settings adapters
  App/         product state, playback, input, radio, power, settings, self-test
  Platform/    HAL-to-driver integration and audio routing
  UI/          display renderer
  tests/       native protocol/state unit tests
  tools/       reproducible build/test scripts
```

