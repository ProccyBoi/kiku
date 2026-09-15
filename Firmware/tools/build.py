#!/usr/bin/env python3
"""Reproducible bare-metal/FreeRTOS build for the kiku STM32H523 firmware."""

from __future__ import annotations

import argparse
import hashlib
import json
import os
import pathlib
import shutil
import subprocess
import sys

ROOT = pathlib.Path(__file__).resolve().parents[1]
REPO = ROOT.parent
BUILD = ROOT / "build"


def tool(name: str) -> str:
    env_prefix = os.environ.get("ARM_GCC_PREFIX", "")
    candidates = []
    if env_prefix:
        candidates.append(str(pathlib.Path(env_prefix) / name))
    found = shutil.which(name)
    if found:
        candidates.append(found)
    if os.name == "nt":
        home = pathlib.Path.home()
        candidates.append(str(home / ".platformio" / "packages" / "toolchain-gccarmnoneeabi" / "bin" / f"{name}.exe"))
    for candidate in candidates:
        if pathlib.Path(candidate).exists():
            return candidate
    raise SystemExit(f"Missing {name}. Install GNU Arm Embedded GCC or set ARM_GCC_PREFIX.")


CC = tool("arm-none-eabi-gcc")
OBJCOPY = tool("arm-none-eabi-objcopy")
SIZE = tool("arm-none-eabi-size")

INCLUDES = [
    ROOT / "Core" / "Inc",
    ROOT / "App" / "Inc",
    ROOT / "Drivers" / "Inc",
    ROOT / "Storage" / "Inc",
    ROOT / "Audio" / "Inc",
    ROOT / "Platform" / "Inc",
    ROOT / "UI" / "Inc",
    ROOT / "ThirdParty" / "FatFs",
    ROOT / "vendor" / "CMSIS_Core" / "Include",
    ROOT / "vendor" / "CMSIS_Device_H5" / "Include",
    ROOT / "vendor" / "STM32H5xx_HAL_Driver" / "Inc",
    ROOT / "vendor" / "FreeRTOS-Kernel" / "include",
    ROOT / "vendor" / "FreeRTOS-Kernel" / "portable" / "GCC" / "ARM_CM33_NTZ" / "non_secure",
    ROOT / "vendor" / "minimp3",
]

HAL_FILES = [
    "stm32h5xx_hal.c",
    "stm32h5xx_hal_cortex.c",
    "stm32h5xx_hal_dma.c",
    "stm32h5xx_hal_dma_ex.c",
    "stm32h5xx_hal_exti.c",
    "stm32h5xx_hal_flash.c",
    "stm32h5xx_hal_flash_ex.c",
    "stm32h5xx_hal_gpio.c",
    "stm32h5xx_hal_i2c.c",
    "stm32h5xx_hal_i2c_ex.c",
    "stm32h5xx_hal_i2s.c",
    "stm32h5xx_hal_i2s_ex.c",
    "stm32h5xx_hal_icache.c",
    "stm32h5xx_hal_iwdg.c",
    "stm32h5xx_hal_pwr.c",
    "stm32h5xx_hal_pwr_ex.c",
    "stm32h5xx_hal_rcc.c",
    "stm32h5xx_hal_rcc_ex.c",
    "stm32h5xx_hal_sd.c",
    "stm32h5xx_hal_sd_ex.c",
    "stm32h5xx_hal_spi.c",
    "stm32h5xx_hal_spi_ex.c",
    "stm32h5xx_hal_tim.c",
    "stm32h5xx_hal_tim_ex.c",
    "stm32h5xx_hal_uart.c",
    "stm32h5xx_hal_uart_ex.c",
    "stm32h5xx_ll_sdmmc.c",
]

FREERTOS_FILES = [
    "tasks.c",
    "queue.c",
    "list.c",
    "timers.c",
    "event_groups.c",
    "stream_buffer.c",
    "portable/GCC/ARM_CM33_NTZ/non_secure/port.c",
    "portable/GCC/ARM_CM33_NTZ/non_secure/portasm.c",
    "portable/MemMang/heap_4.c",
]


def source_files() -> list[pathlib.Path]:
    result: list[pathlib.Path] = []
    for folder in ("Core/Src", "App/Src", "Drivers/Src", "Storage/Src", "Audio/Src", "Platform/Src", "UI/Src"):
        path = ROOT / folder
        if path.exists():
            result.extend(sorted(path.glob("*.c")))
    result.extend(ROOT / "vendor" / "STM32H5xx_HAL_Driver" / "Src" / f for f in HAL_FILES)
    result.extend(ROOT / "vendor" / "FreeRTOS-Kernel" / f for f in FREERTOS_FILES)
    result.extend([
        ROOT / "ThirdParty" / "FatFs" / "ff.c",
        ROOT / "ThirdParty" / "FatFs" / "ffunicode.c",
    ])
    return [p for p in result if p.exists()]


def startup_file() -> pathlib.Path:
    return ROOT / "vendor" / "CMSIS_Device_H5" / "Source" / "Templates" / "gcc" / "startup_stm32h523xx.s"


def run(cmd: list[str]) -> None:
    print("+", " ".join(cmd))
    subprocess.run(cmd, check=True)


def sha256(path: pathlib.Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as handle:
        for block in iter(lambda: handle.read(1024 * 1024), b""):
            digest.update(block)
    return digest.hexdigest()


def git_output(*args: str) -> str:
    try:
        return subprocess.check_output(
            ["git", *args], cwd=REPO, text=True, stderr=subprocess.DEVNULL
        ).strip()
    except (OSError, subprocess.CalledProcessError):
        return "unavailable"


def write_manifest(artifacts: list[pathlib.Path], debug: bool) -> None:
    try:
        compiler_version = subprocess.check_output(
            [CC, "--version"], text=True, stderr=subprocess.STDOUT
        ).splitlines()[0]
    except (OSError, subprocess.CalledProcessError, IndexError):
        compiler_version = "unavailable"

    # Include untracked files in provenance. A release assembled from a tree
    # containing uncommitted source/assets is not reproducible even when all
    # tracked files happen to be clean.
    status = git_output("status", "--porcelain")
    manifest = {
        "product": "kiku",
        "target": "STM32H523VET6",
        "configuration": "debug" if debug else "release",
        "git_commit": git_output("rev-parse", "HEAD"),
        "working_tree_dirty": bool(status and status != "unavailable"),
        "compiler": compiler_version,
        "artifacts": {
            path.name: {"bytes": path.stat().st_size, "sha256": sha256(path)}
            for path in artifacts
        },
    }
    manifest_path = BUILD / "kiku-build-manifest.json"
    manifest_path.write_text(json.dumps(manifest, indent=2, sort_keys=True) + "\n", encoding="utf-8")
    print(f"Manifest: {manifest_path}")


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--clean", action="store_true")
    parser.add_argument("--debug", action="store_true")
    args = parser.parse_args()

    if args.clean and BUILD.exists():
        shutil.rmtree(BUILD)
    objdir = BUILD / "obj"
    objdir.mkdir(parents=True, exist_ok=True)

    common = [
        "-mcpu=cortex-m33", "-mthumb", "-mfpu=fpv5-sp-d16", "-mfloat-abi=hard",
        "-std=gnu11", "-ffunction-sections", "-fdata-sections", "-fno-common", "-fstack-usage",
        "-Wall", "-Wextra", "-Wshadow", "-Wdouble-promotion",
        "-Werror=implicit-function-declaration", "-Werror=return-type",
        "-DSTM32H523xx", "-DUSE_HAL_DRIVER", "-DMP3_ONLY",
        "-DMINIMP3_ONLY_MP3",
        "-g3" if args.debug else "-g1",
    ]
    incs = [f"-I{p}" for p in INCLUDES if p.exists()]

    objects: list[pathlib.Path] = []
    for src in source_files() + [startup_file()]:
        rel = src.relative_to(ROOT) if src.is_relative_to(ROOT) else pathlib.Path(src.name)
        obj = objdir / ("__".join(rel.parts) + ".o")
        obj.parent.mkdir(parents=True, exist_ok=True)
        # GCC 12.3 (the PlatformIO xPack toolchain commonly installed on
        # Windows) has a known optimiser ICE in STM32H5 HAL DMAEx linked-list
        # code. Compile that vendor translation unit at -O0 only; application
        # code and every other HAL unit remain optimised normally.
        opt = "-Og" if args.debug else ("-O0" if src.name == "stm32h5xx_hal_dma_ex.c" else "-O2")
        rel_parts = rel.parts
        first_party = bool(rel_parts) and rel_parts[0] in {
            "Core", "App", "Drivers", "Storage", "Audio", "Platform", "UI"
        }
        warning_policy = ["-Werror"] if first_party and src.suffix.lower() == ".c" else []
        run([CC, *common, *warning_policy, opt, *incs, "-MMD", "-MP", "-c", str(src), "-o", str(obj)])
        objects.append(obj)

    elf = BUILD / "kiku.elf"
    map_file = BUILD / "kiku.map"
    linker = ROOT / "vendor" / "CMSIS_Device_H5" / "Source" / "Templates" / "gcc" / "linker" / "STM32H523xx_FLASH.ld"
    link = [
        CC, "-mcpu=cortex-m33", "-mthumb", "-mfpu=fpv5-sp-d16", "-mfloat-abi=hard",
        *map(str, objects),
        f"-T{linker}", "-Wl,--gc-sections", f"-Wl,-Map={map_file}",
        "--specs=nano.specs", "--specs=nosys.specs", "-Wl,--start-group", "-lc", "-lm", "-Wl,--end-group",
        "-o", str(elf),
    ]
    run(link)
    bin_file = BUILD / "kiku.bin"
    hex_file = BUILD / "kiku.hex"
    run([OBJCOPY, "-O", "binary", str(elf), str(bin_file)])
    run([OBJCOPY, "-O", "ihex", str(elf), str(hex_file)])
    run([SIZE, "-A", str(elf)])
    write_manifest([elf, bin_file, hex_file, map_file], args.debug)
    print(f"Built: {elf}")
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except subprocess.CalledProcessError as exc:
        print(f"Build failed with exit code {exc.returncode}", file=sys.stderr)
        raise SystemExit(exc.returncode)
