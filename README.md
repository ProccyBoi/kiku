# kiku

kiku is an open-source portable audio player built around an STM32H523 microcontroller. The project combines Bluetooth audio, local microSD playback, FM radio, a colour display, headphone and speaker outputs, battery management, and a custom mechanical enclosure in one hardware/firmware repository.

> **Project status:** this repository is a public engineering snapshot, not a production-approved hardware release. The firmware software gate passes on the checked-in source, but physical product qualification is still in progress. In particular, the Rev-A USB-C/power-path issue and the remaining hardware soak/validation items in [`Firmware/RELEASE_CHECKLIST.md`](Firmware/RELEASE_CHECKLIST.md) must be resolved before treating the design as production-ready. Files under `Production/` are therefore prototype/reference manufacturing outputs, not a production release.

## Features

- Bluetooth A2DP sink with AVRCP transport control and metadata through a Microchip BM83
- WAV and MP3 playback from a 4-bit SDMMC microSD card
- FM tuning, seek and RDS using the Silicon Labs SI4705-D60
- TLV320AIC3104 headphone/codec path and MAX98357A internal-speaker amplifier
- automatic headphone/speaker routing
- 2.0-inch 240 x 320 ST7789 display, rotary encoder and push-button controls
- BQ25895 charging/power-path control and MAX17048 battery state-of-charge monitoring
- FreeRTOS-based STM32H523 firmware with host tests, release/debug builds, watchdogs and diagnostics
- project-owned enclosure CAD, printable parts and mechanical packaging tools

## Hardware

The KiCad project in the repository root is the electrical hardware source of truth. The current board uses:

| Function | Device |
| --- | --- |
| Application MCU | STM32H523VET6 |
| Bluetooth audio | Microchip BM83, A2DP sink |
| Audio codec / headphones | TI TLV320AIC3104 |
| Speaker amplifier | Analog Devices MAX98357A |
| FM receiver | Silicon Labs SI4705-D60 |
| Charger / power path | TI BQ25895 |
| Fuel gauge | Analog Devices MAX17048 |
| Display | HS20HS072RX, ST7789, 240 x 320 |
| Storage | 4-bit SDMMC microSD |

The board is programmed and debugged through **SWD**. On this hardware revision, USB-C D+/D- are used by the BQ25895 input-detection circuitry and are **not routed to STM32 PA11/PA12**, so USB DFU/CDC is not available.

The historical `Walkman - Blobject.*` KiCad/manufacturing filenames are retained in this snapshot to avoid breaking project references and release tooling; the public project name is **kiku**.

## Firmware

Firmware is under [`Firmware/`](Firmware/README.md). It implements Bluetooth, local-file and FM source handling; audio routing; display and controls; battery/charger monitoring; persistent settings; watchdog/fault handling; and bring-up diagnostics.

Initialise the pinned submodules and run the host/build checks from the repository root:

```text
git submodule update --init --recursive
python Firmware/tools/test_host.py
python Firmware/tools/build.py --clean
```

For a software release candidate, run:

```text
python Firmware/tools/release_check.py --require-clean
```

The firmware build outputs `kiku.elf`, `kiku.bin`, `kiku.hex` and `kiku.map` under `Firmware/build/`. See [`Firmware/README.md`](Firmware/README.md) for the BM83 configuration contract, programming steps, controls and bring-up details.

## Mechanical design

[`Mechanical/`](Mechanical/README.md) contains the enclosure source, public STEP/STL outputs, review material and packaging scripts.

The project intentionally does **not** redistribute component 3D models whose upstream redistribution terms are unclear, nor generated populated/reference assemblies that embed those models. Public mechanical packages use project-owned geometry and recorded QA metadata. Exact component-level fit checking remains a local workflow after obtaining the required manufacturer/KiCad models under their original terms.

See [`Mechanical/REFERENCE_GEOMETRY.md`](Mechanical/REFERENCE_GEOMETRY.md) for the complete provenance and reproducibility workflow.

## Repository layout

```text
Firmware/                       STM32 firmware, tests and build/release tooling
Mechanical/                     enclosure CAD, exports, QA and packaging tools
Production/                     prototype/reference PCB manufacturing outputs
3dmodels/README.md              policy for locally obtained component models
Walkman - Blobject.kicad_*      KiCad project sources
Print.pdf                       generated PCB print output
Print Schematic.pdf             generated schematic print output
.github/workflows/firmware.yml  firmware CI
```

## Public-source scope

This repository publishes project-authored hardware, firmware and mechanical sources while deliberately excluding material that should remain with its upstream publisher. Excluded material includes third-party BM83 firmware/configuration tools and command-set documents, generated radio images, cached supplier pages, vendor datasheet copies, provenance-unclear component 3D models, and fit-check assemblies that serialise those models.

Third-party source code that is present in the tree or pulled as a submodule remains under its own upstream copyright and licence notices. The project licence does not replace those terms.

## Validation

The public snapshot is checked with:

- native firmware regression tests plus clean release/debug ARM builds;
- `Firmware/tools/release_check.py --require-clean`;
- stripped-checkout P0/P1 mechanical package builds that exclude third-party reference geometry;
- Git history, integrity, privacy and credential scans before publication.

Passing these source/software checks does not replace the physical hardware, RF, charging, mechanical, regulatory or long-duration soak tests listed in [`Firmware/RELEASE_CHECKLIST.md`](Firmware/RELEASE_CHECKLIST.md).

## Licence

kiku is a mixed-licence project:

- project-authored hardware, electronics and mechanical design material is licensed under **CERN-OHL-P-2.0**;
- project-authored firmware, tools, scripts and CI code is licensed under the **MIT License**;
- files or portions carrying their own third-party copyright/licence notices remain under those upstream terms.

The exact scope map and full licence texts are in [`LICENSE`](LICENSE) and [`LICENSES/`](LICENSES/).
