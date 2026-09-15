# BM83 production package

This directory intentionally does **not** redistribute Microchip BM83/IS2083
firmware, configuration tools, command-set documents or generated radio images.
Obtain the applicable BM83 software package and documentation directly from
Microchip, then archive the exact production inputs and hashes with the build
and manufacturing records rather than in the public source repository.

## kiku radio requirements

The production BM83 configuration must provide:

- MSPK2 SPP firmware v1.3 or newer compatible firmware. The connected v1.2
  development module reproduced Microchip's documented defect where repeated
  UART `0x4A` AVRCP vendor-dependent requests eventually stop receiving a
  response; the BM83 MSPK2 v1.3 release notes list this defect as fixed.
- Host MCU mode at 115200 baud.
- A2DP sink and AVRCP enabled.
- 48 kHz, 24-bit stereo I2S output with the BM83 providing BCLK/LRCLK to the
  STM32 I2S2 slave receiver.
- A production-specific Bluetooth device name and profile configuration.
- OTA DFU disabled when it is not a product requirement. If OTA DFU is shipped,
  provision a non-default production secret outside Git and include it in the
  manufacturing security process; never ship the Config Tool example/default
  key.

The STM32 firmware deliberately keeps the field-proven `0x4A` metadata path and
also serialises title, artist and album into separate physical requests. The
three fields are staged and committed atomically. An accepted request whose
`0x5D` response is missing remains owned instead of being blindly retried, so a
faulty/older BM83 cannot be driven into a UART resource-exhaustion loop.

## Golden-unit record

For every production release, record at minimum:

- BM83 8051 firmware variant and version;
- DSP firmware version;
- UART command-set / Host MCU version reported by the module;
- configuration image SHA-256;
- radio firmware image SHA-256;
- production device-name/profile configuration;
- whether OTA DFU is disabled or the identifier of the external secret record;
- a sustained metadata-soak result on the exact programmed radio image.

Do not treat files left in this directory on a development workstation as
release artefacts unless they match the signed golden-unit record.
