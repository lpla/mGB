#!/usr/bin/env python3
"""Run mGB MIDI/link-cable regression scenarios through SameBoy Core."""

from __future__ import annotations

import argparse
import csv
import dataclasses
import hashlib
import os
import platform
import shutil
import subprocess
import sys
from pathlib import Path


REPO = Path(__file__).resolve().parents[1]
DEFAULT_SAMEBOY_SRC = Path(os.environ.get("SAMEBOY_SRC", "/tmp/SameBoy-src"))
DEFAULT_BOOT_DIR = Path(
    os.environ.get("SAMEBOY_BOOT_DIR", "/Applications/SameBoy.app/Contents/Resources")
)
DEFAULT_WORK_DIR = Path(os.environ.get("MGB_SAMEBOY_WORK_DIR", "/tmp/mgb-sameboy-midi-regression"))
DEFAULT_GBDK_HOME = Path(
    os.environ.get("GBDK_HOME", str(Path.home() / ".cache/gbdk-2020/4.5.0/gbdk"))
)

MODES = ("cgb", "dmg")
ROMS = ("upstream", "previous", "current")
TRIGGER_KEYS = ("trigger_pu1", "trigger_pu2", "trigger_wav", "trigger_noi")


@dataclasses.dataclass(frozen=True)
class Case:
    name: str
    midi: str
    expected: dict[str, int] = dataclasses.field(default_factory=dict)
    stable_baseline: bool = False
    min_sound_delta: int = 0
    expected_exact: dict[str, int] = dataclasses.field(default_factory=dict)
    exact_sound_delta: int | None = None
    modes: tuple[str, ...] = MODES
    roms: tuple[str, ...] = ROMS
    instances: int = 1
    press_start: bool = False
    channel_map: tuple[int, int, int, int, int] | None = None
    data_set_patch: dict[int, int] = dataclasses.field(default_factory=dict)
    joypad_script: str | None = None
    save_fixture: str = "none"
    settle_frames: int | None = None
    expect_panic_silenced: bool = False
    expect_save_valid: bool = False
    expected_values: dict[str, str] = dataclasses.field(default_factory=dict)

    @property
    def byte_count(self) -> int:
        return len(parse_hex(self.midi))


def h(*values: int) -> str:
    return " ".join(f"{value:02x}" for value in values)


def parse_hex(text: str) -> list[int]:
    if not text.strip():
        return []
    return [int(part, 16) for part in text.replace(",", " ").split()]


def stress_bytes(iterations: int = 20) -> str:
    data: list[int] = []
    for i in range(iterations):
        channel = i % 5
        note = 48 + (i % 24)
        velocity = 72 + (i % 32)
        on = 0x90 + channel
        off = 0x80 + channel
        data.extend((0xF8, on, note, velocity, 0xF8, off, note, 0x00))
    return h(*data)


PU1_CH9_JOYPAD_SCRIPT = ",".join(
    token
    for pair in [("down", "wait:6")] * 7 + [("a+right", "wait:8")] * 8
    for token in pair
)
TUNING_JUST_JOYPAD_SCRIPT = "select+start,wait:20,right,wait:8,right,wait:8,a+right,wait:20"
TUNING_19EDO_JOYPAD_SCRIPT = (
    "select+start,wait:20,right,wait:8,right,wait:8,"
    "a+right,wait:8,a+right,wait:8,a+right,wait:20"
)
RUNTIME_CHANNEL_MAP = (8, 9, 10, 11, 12)
PATCH_MPE_ON = {35: 1}
PATCH_VELOCITY_FULL = {36: 3}
PATCH_LEGATO_ON = {38: 1}

STANDARD_CASES = (
    Case("baseline", "", stable_baseline=True),
    Case("pu1_note", h(0x90, 0x3C, 0x64, 0x80, 0x3C, 0x00), {"trigger_pu1": 1}, True),
    Case("pu2_note", h(0x91, 0x3E, 0x64, 0x81, 0x3E, 0x00), {"trigger_pu2": 1}, True),
    Case("wav_note", h(0x92, 0x40, 0x64, 0x82, 0x40, 0x00), stable_baseline=True, min_sound_delta=4),
    Case("noise_note", h(0x93, 0x2A, 0x64, 0x83, 0x2A, 0x00), {"trigger_noi": 1}, True),
    Case(
        "poly_ch5_three_notes",
        h(
            0x94, 0x3C, 0x64,
            0x94, 0x40, 0x64,
            0x94, 0x43, 0x64,
            0x84, 0x3C, 0x00,
            0x84, 0x40, 0x00,
            0x84, 0x43, 0x00,
        ),
        {"trigger_pu1": 1, "trigger_pu2": 1},
        True,
        10,
    ),
    Case(
        "running_status_pu1",
        h(
            0x90, 0x3C, 0x64,
            0x40, 0x64,
            0x43, 0x64,
            0x80, 0x3C, 0x00,
            0x40, 0x00,
            0x43, 0x00,
        ),
        {"trigger_pu1": 1},
        True,
    ),
    Case(
        "realtime_interleaved_pu1",
        h(0x90, 0xF8, 0x3C, 0xF8, 0x64, 0x80, 0xF8, 0x3C, 0x00),
        {"trigger_pu1": 1},
    ),
    Case(
        "system_then_pu1",
        h(0xF0, 0x7E, 0x00, 0xF7, 0x90, 0x3C, 0x64, 0x80, 0x3C, 0x00),
        {"trigger_pu1": 1},
    ),
    Case(
        "program_change_then_note",
        h(0xC0, 0x01, 0x90, 0x3C, 0x64, 0x80, 0x3C, 0x00),
        {"trigger_pu1": 1},
    ),
    Case("program_change_only", h(0xC0, 0x01)),
    Case(
        "pan_all_channels",
        h(0xB0, 0x0A, 0x00, 0xB1, 0x0A, 0x7F, 0xB2, 0x0A, 0x00, 0xB3, 0x0A, 0x7F),
    ),
    Case(
        "sustain_all_channels",
        h(
            0xB0, 0x40, 0x7F, 0x90, 0x3C, 0x64, 0x80, 0x3C, 0x00, 0xB0, 0x40, 0x00,
            0xB1, 0x40, 0x7F, 0x91, 0x3E, 0x64, 0x81, 0x3E, 0x00, 0xB1, 0x40, 0x00,
            0xB2, 0x40, 0x7F, 0x92, 0x40, 0x64, 0x82, 0x40, 0x00, 0xB2, 0x40, 0x00,
            0xB3, 0x40, 0x7F, 0x93, 0x2A, 0x64, 0x83, 0x2A, 0x00, 0xB3, 0x40, 0x00,
        ),
        {"trigger_pu1": 1, "trigger_pu2": 1, "trigger_noi": 1},
        min_sound_delta=12,
    ),
    Case(
        "all_notes_off_all_channels",
        h(
            0x90, 0x3C, 0x64,
            0x91, 0x3E, 0x64,
            0x92, 0x40, 0x64,
            0x93, 0x2A, 0x64,
            0xB0, 0x7B, 0x00,
            0xB1, 0x7B, 0x00,
            0xB2, 0x7B, 0x00,
            0xB3, 0x7B, 0x00,
        ),
        {"trigger_pu1": 1, "trigger_pu2": 1, "trigger_noi": 1},
        min_sound_delta=12,
    ),
    Case(
        "pitch_bend_extremes_all",
        h(
            0x90, 0x3C, 0x64, 0xE0, 0x00, 0x00, 0xE0, 0x7F, 0x7F,
            0x91, 0x3E, 0x64, 0xE1, 0x00, 0x00, 0xE1, 0x7F, 0x7F,
            0x92, 0x40, 0x64, 0xE2, 0x00, 0x00, 0xE2, 0x7F, 0x7F,
            0x93, 0x2A, 0x64, 0xE3, 0x00, 0x00, 0xE3, 0x7F, 0x7F,
            0x94, 0x43, 0x64, 0xE4, 0x00, 0x00, 0xE4, 0x7F, 0x7F,
        ),
        {"trigger_pu1": 1, "trigger_pu2": 1, "trigger_noi": 1},
        min_sound_delta=20,
    ),
    Case("wav_shape_offset_max", h(0xB2, 0x01, 0x7F, 0xB2, 0x02, 0x7F, 0x92, 0x40, 0x64, 0x82, 0x40, 0x00), min_sound_delta=5),
    Case(
        "note_extremes_all",
        h(
            0x90, 0x00, 0x64, 0x80, 0x00, 0x00, 0x90, 0x7F, 0x64, 0x80, 0x7F, 0x00,
            0x91, 0x00, 0x64, 0x81, 0x00, 0x00, 0x91, 0x7F, 0x64, 0x81, 0x7F, 0x00,
            0x92, 0x00, 0x64, 0x82, 0x00, 0x00, 0x92, 0x7F, 0x64, 0x82, 0x7F, 0x00,
            0x93, 0x00, 0x64, 0x83, 0x00, 0x00, 0x93, 0x7F, 0x64, 0x83, 0x7F, 0x00,
        ),
        {"trigger_pu1": 2, "trigger_pu2": 2, "trigger_noi": 2},
        min_sound_delta=20,
    ),
    Case("serial_stress_realtime", stress_bytes(), {"trigger_pu1": 4, "trigger_pu2": 4, "trigger_noi": 4}, min_sound_delta=40),
)

SPECIAL_CASES = (
    Case(
        "synccross_4way_stress",
        stress_bytes(30),
        {"trigger_pu1": 4, "trigger_pu2": 4, "trigger_noi": 4},
        roms=ROMS,
        instances=4,
        min_sound_delta=50,
    ),
    Case(
        "panic_start_active_notes",
        h(0x90, 0x3C, 0x64, 0x91, 0x40, 0x64, 0x92, 0x43, 0x64, 0x93, 0x24, 0x64),
        {"trigger_pu1": 1, "trigger_pu2": 1, "trigger_noi": 1},
        roms=ROMS,
        press_start=True,
        min_sound_delta=10,
        expect_panic_silenced=True,
    ),
    Case(
        "runtime_channel_map_old_channels_ignored",
        h(
            0x90, 0x3C, 0x64,
            0x91, 0x3E, 0x64,
            0x92, 0x40, 0x64,
            0x93, 0x2A, 0x64,
            0x94, 0x43, 0x64,
        ),
        roms=("current",),
        channel_map=RUNTIME_CHANNEL_MAP,
        expected_exact={
            "trigger_pu1": 0,
            "trigger_pu2": 0,
            "trigger_wav": 0,
            "trigger_noi": 0,
        },
        exact_sound_delta=0,
    ),
    Case(
        "runtime_channel_map_notes",
        h(
            0x98, 0x3C, 0x64, 0x88, 0x3C, 0x00,
            0x99, 0x3E, 0x64, 0x89, 0x3E, 0x00,
            0x9A, 0x40, 0x64, 0x8A, 0x40, 0x00,
            0x9B, 0x2A, 0x64, 0x8B, 0x2A, 0x00,
        ),
        {"trigger_pu1": 1, "trigger_pu2": 1, "trigger_noi": 1},
        min_sound_delta=12,
        roms=("current",),
        channel_map=RUNTIME_CHANNEL_MAP,
    ),
    Case(
        "runtime_channel_map_poly",
        h(
            0x9C, 0x3C, 0x64,
            0x9C, 0x40, 0x64,
            0x9C, 0x43, 0x64,
            0x8C, 0x3C, 0x00,
            0x8C, 0x40, 0x00,
            0x8C, 0x43, 0x00,
        ),
        {"trigger_pu1": 1, "trigger_pu2": 1},
        min_sound_delta=10,
        roms=("current",),
        channel_map=RUNTIME_CHANNEL_MAP,
    ),
    Case(
        "runtime_channel_map_cc_pan",
        h(0xB8, 0x0A, 0x00, 0xB9, 0x0A, 0x7F, 0xBA, 0x0A, 0x00, 0xBB, 0x0A, 0x7F),
        min_sound_delta=4,
        roms=("current",),
        channel_map=RUNTIME_CHANNEL_MAP,
    ),
    Case(
        "runtime_channel_map_poly_cc_controls",
        h(0xBC, 0x0A, 0x40, 0xBC, 0x40, 0x7F, 0xBC, 0x40, 0x00, 0xBC, 0x7B, 0x00),
        min_sound_delta=4,
        roms=("current",),
        channel_map=RUNTIME_CHANNEL_MAP,
    ),
    Case(
        "runtime_channel_map_program_change",
        h(0xC8, 0x01, 0xC9, 0x01, 0xCA, 0x01, 0xCB, 0x01, 0xCC, 0x01),
        min_sound_delta=4,
        roms=("current",),
        channel_map=RUNTIME_CHANNEL_MAP,
    ),
    Case(
        "ui_channel_row_pu1_ch9",
        h(0x90, 0x3C, 0x64, 0x98, 0x3E, 0x64),
        expected_exact={"trigger_pu1": 1},
        min_sound_delta=3,
        roms=("current",),
        joypad_script=PU1_CH9_JOYPAD_SCRIPT,
        expected_values={"data_set_28": "8"},
    ),
    Case(
        "persistent_global_channel_map",
        h(
            0x98, 0x3C, 0x64, 0x88, 0x3C, 0x00,
            0x99, 0x3E, 0x64, 0x89, 0x3E, 0x00,
            0x9A, 0x40, 0x64, 0x8A, 0x40, 0x00,
            0x9B, 0x2A, 0x64, 0x8B, 0x2A, 0x00,
        ),
        {"trigger_pu1": 1, "trigger_pu2": 1, "trigger_noi": 1},
        min_sound_delta=12,
        roms=("current",),
        save_fixture="valid_global_map",
        expect_save_valid=True,
    ),
    Case(
        "persistent_global_profile3",
        h(
            0x98, 0x3C, 0x64, 0x88, 0x3C, 0x00,
            0x99, 0x3E, 0x64, 0x89, 0x3E, 0x00,
            0x9A, 0x40, 0x64, 0x8A, 0x40, 0x00,
            0x9B, 0x2A, 0x64, 0x8B, 0x2A, 0x00,
        ),
        {"trigger_pu1": 1, "trigger_pu2": 1, "trigger_noi": 1},
        min_sound_delta=12,
        roms=("current",),
        save_fixture="valid_global_profile3",
        expect_save_valid=True,
    ),
    Case(
        "channel_off_pu1_ignored",
        h(0x90, 0x3C, 0x64),
        expected_exact={"trigger_pu1": 0},
        exact_sound_delta=0,
        roms=("current",),
        data_set_patch={28: 16},
    ),
    Case(
        "mpe_channel_pressure_pu1",
        h(0x90, 0x3C, 0x20, 0xD0, 0x7F),
        {"trigger_pu1": 1},
        min_sound_delta=4,
        roms=("current",),
        data_set_patch=PATCH_MPE_ON,
        expected_values={"nr12": "f6"},
    ),
    Case(
        "mpe_cc74_timbre_pu1",
        h(0xB0, 0x4A, 0x7F),
        min_sound_delta=1,
        roms=("current",),
        data_set_patch=PATCH_MPE_ON,
    ),
    Case(
        "velocity_curve_full_pu1",
        h(0x90, 0x3C, 0x10),
        {"trigger_pu1": 1},
        min_sound_delta=3,
        roms=("current",),
        data_set_patch=PATCH_VELOCITY_FULL,
        expected_values={"nr12": "f6"},
    ),
    Case(
        "tuning_just_pu1_csharp",
        h(0x90, 0x3D, 0x64),
        {"trigger_pu1": 1},
        min_sound_delta=3,
        roms=("current",),
        joypad_script=TUNING_JUST_JOYPAD_SCRIPT,
        expected_values={"nr13": "2a", "nr14": "86"},
    ),
    Case(
        "tuning_19edo_pu1_csharp",
        h(0x90, 0x3D, 0x64),
        {"trigger_pu1": 1},
        min_sound_delta=3,
        roms=("current",),
        joypad_script=TUNING_19EDO_JOYPAD_SCRIPT,
        expected_values={"nr13": "db", "nr14": "84"},
    ),
    Case(
        "legato_pu1_suppresses_retrigger",
        h(0x90, 0x3C, 0x64, 0x90, 0x3E, 0x65),
        expected_exact={"trigger_pu1": 1},
        min_sound_delta=5,
        roms=("current",),
        data_set_patch=PATCH_LEGATO_ON,
    ),
    Case(
        "cc120_panic_all_sound_off",
        h(0x90, 0x3C, 0x64, 0x91, 0x40, 0x64, 0x92, 0x43, 0x64, 0x93, 0x24, 0x64, 0xB0, 0x78, 0x00),
        {"trigger_pu1": 1, "trigger_pu2": 1},
        min_sound_delta=10,
        roms=("current",),
        expect_panic_silenced=True,
    ),
    Case(
        "cc120_panic_global_unmapped_channel",
        h(0x90, 0x3C, 0x64, 0xB9, 0x78, 0x00),
        {"trigger_pu1": 1},
        min_sound_delta=6,
        roms=("current",),
        data_set_patch={28: 0, 29: 1, 30: 2, 31: 3, 32: 4},
        expect_panic_silenced=True,
    ),
    Case("save_empty", "", roms=("current",), save_fixture="empty", expect_save_valid=True),
    Case("save_ff", "", roms=("current",), save_fixture="ff", expect_save_valid=True),
    Case("save_pattern", "", roms=("current",), save_fixture="pattern", expect_save_valid=True),
    Case("save_valid", "", roms=("current",), save_fixture="valid", expect_save_valid=True),
    Case("save_valid_empty_checksum", "", roms=("current",), save_fixture="valid_empty_checksum", expect_save_valid=True),
    Case("save_corrupt_checksum", "", roms=("current",), save_fixture="corrupt_checksum", expect_save_valid=True),
    Case(
        "long_current_soak",
        stress_bytes(300),
        {"trigger_pu1": 40, "trigger_pu2": 40, "trigger_noi": 40},
        roms=("current",),
        settle_frames=3600,
        min_sound_delta=600,
    ),
    Case(
        "long_synccross_4way_soak",
        stress_bytes(120),
        {"trigger_pu1": 20, "trigger_pu2": 20, "trigger_noi": 20},
        modes=("cgb",),
        roms=("current",),
        instances=4,
        settle_frames=1800,
        min_sound_delta=240,
    ),
)

ALL_CASES = STANDARD_CASES + SPECIAL_CASES


def run(cmd: list[str], cwd: Path | None = None, check: bool = True) -> subprocess.CompletedProcess[str]:
    proc = subprocess.run(cmd, cwd=cwd, text=True, stdout=subprocess.PIPE, stderr=subprocess.PIPE)
    if check and proc.returncode != 0:
        sys.stderr.write(proc.stdout)
        sys.stderr.write(proc.stderr)
        raise SystemExit(f"command failed ({proc.returncode}): {' '.join(cmd)}")
    return proc


def sha1(path: Path) -> str:
    digest = hashlib.sha1()
    with path.open("rb") as handle:
        for chunk in iter(lambda: handle.read(65536), b""):
            digest.update(chunk)
    return digest.hexdigest()


def ensure_sameboy(sameboy_src: Path, boot_dir: Path) -> None:
    if not sameboy_src.exists():
        raise SystemExit(f"SameBoy source not found: {sameboy_src}")
    if not (boot_dir / "cgb_boot.bin").exists() or not (boot_dir / "dmg_boot.bin").exists():
        raise SystemExit(f"SameBoy boot ROMs not found in: {boot_dir}")

    tester = sameboy_src / "build/bin/tester/sameboy_tester"
    core_objects = list((sameboy_src / "build/obj/Core").glob("*.o"))
    if tester.exists() and core_objects:
        return

    run(
        [
            "make",
            "tester",
            "CONF=debug",
            f"BOOTROMS_DIR={boot_dir}",
            f"-j{os.cpu_count() or 2}",
        ],
        cwd=sameboy_src,
    )


def compile_harness(sameboy_src: Path, work_dir: Path) -> Path:
    harness = work_dir / "sameboy_midi_harness"
    core_objects = sorted((sameboy_src / "build/obj/Core").glob("*.o"))
    if not core_objects:
        raise SystemExit("SameBoy core objects were not built")

    cmd = [
        "cc",
        f"-I{sameboy_src}",
        str(REPO / "tools/sameboy_midi_harness.c"),
        *[str(path) for path in core_objects],
        "-o",
        str(harness),
        "-lc",
        "-lm",
        "-ldl",
    ]

    if platform.system() == "Darwin":
        sdk = run(["xcrun", "--show-sdk-path"]).stdout.strip()
        cmd.extend(["-mmacosx-version-min=10.9", "-isysroot", sdk])

    run(cmd)
    return harness


def build_current_rom(gbdk_home: Path) -> None:
    lcc = gbdk_home / "bin/lcc"
    if not lcc.exists():
        raise SystemExit(f"GBDK-2020 lcc not found: {lcc}")
    run(["make", "-C", "Source", "clean"], cwd=REPO)
    run(["make", "-C", "Source", "build", f"GBDK_HOME={gbdk_home}"], cwd=REPO)


def read_symbol(path: Path, symbol: str) -> int:
    prefix = f"DEF _{symbol} "
    for line in path.read_text().splitlines():
        if line.startswith(prefix):
            return int(line.split()[-1], 16)
    raise SystemExit(f"missing symbol _{symbol} in {path}")


def current_symbols() -> dict[str, int]:
    noi = REPO / "Source/mgb.noi"
    return {
        "dataSet": read_symbol(noi, "dataSet"),
        "saveData": read_symbol(noi, "saveData"),
        "checkMemory": read_symbol(noi, "checkMemory"),
    }


def stage_roms(work_dir: Path) -> dict[str, Path]:
    rom_dir = work_dir / "roms"
    rom_dir.mkdir(parents=True, exist_ok=True)

    paths = {
        "current": rom_dir / "current.gb",
        "previous": rom_dir / "previous.gb",
        "upstream": rom_dir / "upstream.gb",
    }
    shutil.copyfile(REPO / "Source/mgb.gb", paths["current"])
    for label, rev in (("previous", "HEAD:mGB.gb"), ("upstream", "upstream/master:mGB.gb")):
        proc = subprocess.run(["git", "show", rev], cwd=REPO, stdout=subprocess.PIPE, stderr=subprocess.PIPE)
        if proc.returncode != 0:
            sys.stderr.buffer.write(proc.stderr)
            raise SystemExit(f"failed to stage {label} ROM from {rev}")
        paths[label].write_bytes(proc.stdout)
    return paths


def parse_output(text: str) -> dict[str, str]:
    values: dict[str, str] = {}
    for line in text.splitlines():
        if "=" in line:
            key, value = line.split("=", 1)
            values[key] = value
    return values


def int_value(row: dict[str, str], key: str) -> int:
    return int(row[key], 10)


def run_case(
    harness: Path,
    boot_dir: Path,
    symbols: dict[str, int],
    rom_label: str,
    rom: Path,
    mode: str,
    case: Case,
) -> dict[str, str]:
    boot = boot_dir / ("cgb_boot.bin" if mode == "cgb" else "dmg_boot.bin")
    cmd = [
        str(harness),
        "--rom",
        str(rom),
        "--boot",
        str(boot),
        "--mode",
        mode,
        "--label",
        case.name,
        "--bytes",
        case.midi,
        "--instances",
        str(case.instances),
    ]
    if case.press_start:
        cmd.append("--press-start")
    if case.channel_map is not None:
        cmd.extend(
            [
                "--channel-map",
                ",".join(str(channel) for channel in case.channel_map),
                "--data-set-addr",
                hex(symbols["dataSet"]),
            ]
        )
    if case.data_set_patch:
        cmd.extend(
            [
                "--data-set-patch",
                ",".join(f"{offset}={value}" for offset, value in sorted(case.data_set_patch.items())),
                "--data-set-addr",
                hex(symbols["dataSet"]),
            ]
        )
    if (
        any(key.startswith("data_set_") for key in case.expected_values)
        and case.channel_map is None
        and not case.data_set_patch
    ):
        cmd.extend(["--data-set-addr", hex(symbols["dataSet"])])
    if case.joypad_script is not None:
        cmd.extend(["--joypad-script", case.joypad_script])
    if case.save_fixture != "none":
        cmd.extend(
            [
                "--save-fixture",
                case.save_fixture,
                "--save-data-addr",
                hex(symbols["saveData"]),
                "--check-memory-addr",
                hex(symbols["checkMemory"]),
            ]
        )
    if case.settle_frames is not None:
        cmd.extend(["--settle-frames", str(case.settle_frames)])

    proc = run(cmd, check=False)
    row = parse_output(proc.stdout)
    row.update(
        {
            "rom_label": rom_label,
            "mode": mode,
            "case": case.name,
            "byte_count": str(case.byte_count),
            "returncode": str(proc.returncode),
        }
    )
    if proc.stderr.strip():
        row["stderr"] = proc.stderr.strip().replace("\n", " | ")
    return row


def trigger_delta(row: dict[str, str], baseline: dict[str, str], key: str) -> int:
    return int_value(row, key) - int_value(baseline, key)


def analyze(results: list[dict[str, str]], cases: tuple[Case, ...]) -> tuple[list[str], list[str]]:
    by_key = {(r["rom_label"], r["mode"], r["case"]): r for r in results}
    failures: list[str] = []
    notes: list[str] = []

    for row in results:
        label = f'{row["rom_label"]}/{row["mode"]}/{row["case"]}'
        if row["returncode"] != "0":
            failures.append(f"{label}: harness returned {row['returncode']}")
        for key, expected in {
            "ok": 1,
            "timeout": 0,
            "boot_finished": 1,
            "all_boot_finished": 1,
            "all_serial_ready": 1,
            "instances_match": 1,
            "bytes_sent": int(row["byte_count"]),
            "logs": 0,
        }.items():
            if int_value(row, key) != expected:
                failures.append(f"{label}: {key}={row[key]}, expected {expected}")

    for case in cases:
        for rom in case.roms:
            for mode in case.modes:
                row = by_key[(rom, mode, case.name)]
                label = f"{rom}/{mode}/{case.name}"
                baseline = by_key.get((rom, mode, "baseline"))
                if baseline is not None:
                    for key, expected_min in case.expected.items():
                        delta = trigger_delta(row, baseline, key)
                        if delta < expected_min:
                            failures.append(
                                f"{label}: {key} delta {delta}, expected at least {expected_min}"
                            )
                    for key, expected in case.expected_exact.items():
                        delta = trigger_delta(row, baseline, key)
                        if delta != expected:
                            failures.append(
                                f"{label}: {key} delta {delta}, expected exactly {expected}"
                            )
                    if case.min_sound_delta:
                        sound_delta = int_value(row, "sound_writes") - int_value(baseline, "sound_writes")
                        if sound_delta < case.min_sound_delta:
                            failures.append(
                                f"{label}: sound_writes delta {sound_delta}, "
                                f"expected at least {case.min_sound_delta}"
                            )
                    if case.exact_sound_delta is not None:
                        sound_delta = int_value(row, "sound_writes") - int_value(baseline, "sound_writes")
                        if sound_delta != case.exact_sound_delta:
                            failures.append(
                                f"{label}: sound_writes delta {sound_delta}, "
                                f"expected exactly {case.exact_sound_delta}"
                            )
                if case.channel_map is not None and int_value(row, "channel_map_applied") != 1:
                    failures.append(f"{label}: channel map was not applied")
                if case.data_set_patch and int_value(row, "data_set_patch_applied") != 1:
                    failures.append(f"{label}: dataSet patch was not applied")
                if case.joypad_script is not None and int_value(row, "joypad_script_ran") != 1:
                    failures.append(f"{label}: joypad script did not run")
                for key, expected in case.expected_values.items():
                    if row[key].lower() != expected.lower():
                        failures.append(f"{label}: {key}={row[key]}, expected {expected}")
                if case.expect_panic_silenced and int_value(row, "panic_silenced") != 1:
                    failures.append(f"{label}: panic_silenced={row['panic_silenced']}, expected 1")
                if case.expect_save_valid:
                    if int_value(row, "save_fixture_applied") != 1:
                        failures.append(f"{label}: save fixture was not applied")
                    if int_value(row, "save_valid") != 1:
                        failures.append(f"{label}: save_valid={row['save_valid']}, expected 1")
                    if row["save_magic"] != "f7":
                        failures.append(f"{label}: save_magic={row['save_magic']}, expected f7")

    for mode in MODES:
        for case in cases:
            if ("previous" not in case.roms) or ("upstream" not in case.roms) or (mode not in case.modes):
                continue
            previous = by_key[("previous", mode, case.name)]
            upstream = by_key[("upstream", mode, case.name)]
            comparable = [
                "sound_hash",
                "apu_hash",
                "nr51",
                "nr52",
                *TRIGGER_KEYS,
            ]
            for key in comparable:
                if previous[key] != upstream[key]:
                    failures.append(
                        f"previous/upstream mismatch {mode}/{case.name}/{key}: "
                        f"{previous[key]} != {upstream[key]}"
                    )

    for mode in MODES:
        for case in cases:
            if not case.stable_baseline or ("current" not in case.roms) or ("previous" not in case.roms):
                continue
            if mode not in case.modes:
                continue
            current = by_key[("current", mode, case.name)]
            previous = by_key[("previous", mode, case.name)]
            baseline_current = by_key[("current", mode, "baseline")]
            baseline_previous = by_key[("previous", mode, "baseline")]
            for key in TRIGGER_KEYS:
                current_delta = trigger_delta(current, baseline_current, key)
                previous_delta = trigger_delta(previous, baseline_previous, key)
                if current_delta < previous_delta:
                    failures.append(
                        f"stable trigger regression {mode}/{case.name}/{key}: "
                        f"current delta {current_delta} < previous delta {previous_delta}"
                    )
            if current["nr51"] != previous["nr51"] or current["nr52"] != previous["nr52"]:
                failures.append(
                    f"stable final APU control mismatch {mode}/{case.name}: "
                    f"current NR51/NR52 {current['nr51']}/{current['nr52']} vs "
                    f"previous {previous['nr51']}/{previous['nr52']}"
                )
            if current["apu_hash"] != previous["apu_hash"]:
                notes.append(
                    f"stable APU hash differs {mode}/{case.name}: "
                    f"current {current['apu_hash']} vs previous {previous['apu_hash']}"
                )

    for mode in MODES:
        clean = by_key[("current", mode, "pu1_note")]
        realtime = by_key[("current", mode, "realtime_interleaved_pu1")]
        clean_base = by_key[("current", mode, "baseline")]
        if trigger_delta(realtime, clean_base, "trigger_pu1") < trigger_delta(clean, clean_base, "trigger_pu1"):
            failures.append(f"current/{mode} realtime_interleaved_pu1 triggered fewer PU1 notes than clean pu1_note")

        pc_only = by_key[("current", mode, "program_change_only")]
        pc_base = by_key[("current", mode, "baseline")]
        if int_value(pc_only, "sound_writes") <= int_value(pc_base, "sound_writes"):
            failures.append(f"current/{mode} program_change_only did not produce additional sound-register activity")

    return failures, notes


def write_results(path: Path, results: list[dict[str, str]]) -> None:
    keys = [
        "rom_label",
        "mode",
        "case",
        "byte_count",
        "returncode",
        "ok",
        "timeout",
        "boot_finished",
        "all_boot_finished",
        "serial_ready",
        "all_serial_ready",
        "instances",
        "instances_match",
        "sram_fixture",
        "save_fixture",
        "save_fixture_applied",
        "channel_map",
        "channel_map_applied",
        "data_set_patch",
        "data_set_patch_applied",
        "joypad_script_ran",
        "start_pressed",
        "bytes_sent",
        "sound_writes",
        "sound_hash",
        "serial_register_writes",
        *TRIGGER_KEYS,
        "audio_samples",
        "audio_energy",
        "audio_hash",
        "apu_hash",
        "apu_hex",
        "nr51",
        "nr52",
        "nr12",
        "nr13",
        "nr14",
        "nr22",
        "nr32",
        "nr42",
        "panic_silenced",
        "logs",
        "log_hash",
        "pc",
        "sp",
        "sram_size",
        "sram_hash",
        "sram_magic",
        "sram_checksum",
        "sram_expected_checksum",
        "sram_valid",
        "save_hash",
        "save_magic",
        "save_checksum",
        "save_expected_checksum",
        "save_valid",
        "stderr",
    ]
    with path.open("w", newline="") as handle:
        writer = csv.DictWriter(handle, fieldnames=keys, delimiter="\t", extrasaction="ignore")
        writer.writeheader()
        writer.writerows(results)


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--sameboy-src", type=Path, default=DEFAULT_SAMEBOY_SRC)
    parser.add_argument("--boot-dir", type=Path, default=DEFAULT_BOOT_DIR)
    parser.add_argument("--work-dir", type=Path, default=DEFAULT_WORK_DIR)
    parser.add_argument("--gbdk-home", type=Path, default=DEFAULT_GBDK_HOME)
    parser.add_argument("--skip-build-current", action="store_true")
    args = parser.parse_args()

    args.work_dir.mkdir(parents=True, exist_ok=True)
    ensure_sameboy(args.sameboy_src, args.boot_dir)
    harness = compile_harness(args.sameboy_src, args.work_dir)
    if not args.skip_build_current:
        build_current_rom(args.gbdk_home)
    symbols = current_symbols()
    roms = stage_roms(args.work_dir)

    print("ROM SHA-1:")
    for label in ROMS:
        print(f"  {label}: {sha1(roms[label])}")
    print(
        "Symbols: "
        f"dataSet=0x{symbols['dataSet']:04X} "
        f"saveData=0x{symbols['saveData']:04X} "
        f"checkMemory=0x{symbols['checkMemory']:04X}"
    )

    results: list[dict[str, str]] = []
    total = sum(len(case.roms) * len(case.modes) for case in ALL_CASES)
    current = 0
    for case in ALL_CASES:
        for mode in case.modes:
            for rom_label in case.roms:
                current += 1
                print(f"[{current:03d}/{total:03d}] {rom_label}/{mode}/{case.name}", flush=True)
                results.append(run_case(harness, args.boot_dir, symbols, rom_label, roms[rom_label], mode, case))

    results_path = args.work_dir / "sameboy_midi_results.tsv"
    write_results(results_path, results)
    failures, notes = analyze(results, ALL_CASES)

    summary_path = args.work_dir / "sameboy_midi_summary.txt"
    with summary_path.open("w") as handle:
        handle.write(f"runs={len(results)}\n")
        handle.write(f"failures={len(failures)}\n")
        handle.write(f"notes={len(notes)}\n")
        for failure in failures:
            handle.write(f"FAIL {failure}\n")
        for note in notes:
            handle.write(f"NOTE {note}\n")

    print(f"results: {results_path}")
    print(f"summary: {summary_path}")
    if notes:
        print(f"notes: {len(notes)} informational APU-hash differences")
    if failures:
        print(f"FAIL: {len(failures)} regression checks failed")
        for failure in failures[:20]:
            print(f"  {failure}")
        return 1

    print(f"PASS: {len(results)} SameBoy MIDI/link regression runs passed")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
