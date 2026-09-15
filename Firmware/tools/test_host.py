#!/usr/bin/env python3
from __future__ import annotations

import os
import pathlib
import shutil
import subprocess

ROOT = pathlib.Path(__file__).resolve().parents[1]
OUT = ROOT / "tests" / "build"


def main() -> int:
    renderer = (ROOT / "UI" / "Src" / "ui_renderer.c").read_text(encoding="utf-8")
    rejected_ui_copy = (
        "B1", "B2", "KNOB", "K-01", "MONO",
        "CONTROL FREQUENCY", "CONTROL VOLUME", "MANUAL TUNE",
        "Waiting for station text", "Waiting for track info",
        "No track selected", "4s POWER", "5s POWER",
    )
    present = [copy for copy in rejected_ui_copy if copy in renderer]
    if present:
        raise SystemExit(f"Rejected UI copy reintroduced: {', '.join(present)}")

    compiler = shutil.which("gcc") or shutil.which("clang")
    if compiler is None:
        raise SystemExit("Host C compiler not found (gcc/clang required).")
    OUT.mkdir(parents=True, exist_ok=True)
    exe = OUT / ("kiku_tests.exe" if os.name == "nt" else "kiku_tests")
    cmd = [
        compiler, "-std=c11", "-Wall", "-Wextra", "-Werror", "-pedantic", "-O2",
        f"-I{ROOT / 'Drivers' / 'Inc'}", f"-I{ROOT / 'App' / 'Inc'}",
        f"-I{ROOT / 'Audio' / 'Inc'}", f"-I{ROOT / 'UI' / 'Inc'}",
        str(ROOT / "tests" / "test_main.c"),
        str(ROOT / "Audio" / "Src" / "bt_rx_watchdog.c"),
        str(ROOT / "Drivers" / "Src" / "bm83.c"),
        str(ROOT / "Drivers" / "Src" / "bq25895.c"),
        str(ROOT / "App" / "Src" / "settings.c"),
        str(ROOT / "App" / "Src" / "input_manager.c"),
        str(ROOT / "App" / "Src" / "local_playback.c"),
        str(ROOT / "App" / "Src" / "fm_state.c"),
        str(ROOT / "App" / "Src" / "ui_state.c"),
        str(ROOT / "UI" / "Src" / "ui_renderer.c"),
        *[str(ROOT / "App" / "Src" / name) for name in
          ("audio_router.c", "power_manager.c", "self_test.c")],
        *[str(ROOT / "Drivers" / "Src" / name) for name in
          ("audio_io.c", "si4705.c", "max17048.c", "tlv320aic3104.c")],
        "-o", str(exe),
    ]
    print("+", " ".join(cmd))
    subprocess.run(cmd, check=True)
    subprocess.run([str(exe)], check=True)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
