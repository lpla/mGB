#!/usr/bin/env python3
"""Basic ROM regression checks for mGB builds."""

from __future__ import annotations

import argparse
import subprocess
from pathlib import Path


HEADER_FIELDS = {
    "cgb_flag": 0x143,
    "new_licensee_0": 0x144,
    "new_licensee_1": 0x145,
    "sgb_flag": 0x146,
    "cartridge_type": 0x147,
    "rom_size": 0x148,
    "ram_size": 0x149,
    "destination_code": 0x14A,
    "old_licensee": 0x14B,
    "mask_rom_version": 0x14C,
}

EXPECTED_FIELDS = {
    "title": "MGB",
    "size": 65536,
    "cgb_flag": 0x80,
    "new_licensee_0": 0x00,
    "new_licensee_1": 0x00,
    "sgb_flag": 0x00,
    "cartridge_type": 0x02,
    "rom_size": 0x01,
    "ram_size": 0x03,
    "destination_code": 0x00,
    "old_licensee": 0x00,
    "mask_rom_version": 0x01,
}


def read_rom(path: Path) -> bytes:
    if not path.exists():
        raise AssertionError(f"missing ROM: {path}")
    data = path.read_bytes()
    if len(data) < 0x150:
        raise AssertionError(f"{path} is too small to be a Game Boy ROM")
    return data


def header_checksum(data: bytes) -> int:
    value = 0
    for byte in data[0x134:0x14D]:
        value = (value - byte - 1) & 0xFF
    return value


def global_checksum(data: bytes) -> int:
    return sum(byte for i, byte in enumerate(data) if i not in (0x14E, 0x14F)) & 0xFFFF


def rom_title(data: bytes) -> str:
    return data[0x134:0x143].split(b"\0", 1)[0].decode("ascii", errors="replace")


def metadata(data: bytes) -> dict[str, int | str]:
    result: dict[str, int | str] = {"title": rom_title(data), "size": len(data)}
    result.update({name: data[offset] for name, offset in HEADER_FIELDS.items()})
    result["header_checksum"] = data[0x14D]
    result["global_checksum"] = (data[0x14E] << 8) | data[0x14F]
    return result


def assert_valid_data(label: str, data: bytes) -> dict[str, int | str]:
    meta = metadata(data)

    for field, expected in EXPECTED_FIELDS.items():
        actual = meta[field]
        if actual != expected:
            raise AssertionError(f"{label}: {field} is {actual!r}, expected {expected!r}")

    actual_header = meta["header_checksum"]
    expected_header = header_checksum(data)
    if actual_header != expected_header:
        raise AssertionError(
            f"{label}: header checksum is 0x{actual_header:02X}, expected 0x{expected_header:02X}"
        )

    actual_global = meta["global_checksum"]
    expected_global = global_checksum(data)
    if actual_global != expected_global:
        raise AssertionError(
            f"{label}: global checksum is 0x{actual_global:04X}, expected 0x{expected_global:04X}"
        )

    return meta


def assert_valid_rom(path: Path) -> dict[str, int | str]:
    return assert_valid_data(str(path), read_rom(path))


def assert_matches_meta(
    rom: str,
    rom_meta: dict[str, int | str],
    baseline: str,
    baseline_meta: dict[str, int | str],
) -> None:
    comparable_fields = (
        "title",
        "cgb_flag",
        "new_licensee_0",
        "new_licensee_1",
        "sgb_flag",
        "cartridge_type",
        "rom_size",
        "ram_size",
        "destination_code",
        "old_licensee",
        "mask_rom_version",
    )

    for field in comparable_fields:
        if rom_meta[field] != baseline_meta[field]:
            raise AssertionError(
                f"{rom}: {field} differs from {baseline}: "
                f"{rom_meta[field]!r} != {baseline_meta[field]!r}"
            )


def assert_matches_baseline(rom: Path, rom_meta: dict[str, int | str], baseline: Path) -> None:
    if not baseline.exists():
        return
    assert_matches_meta(str(rom), rom_meta, str(baseline), assert_valid_rom(baseline))


def assert_matches_git_baseline(rom: Path, rom_meta: dict[str, int | str], baseline: str) -> None:
    result = subprocess.run(["git", "show", baseline], check=True, stdout=subprocess.PIPE)
    assert_matches_meta(str(rom), rom_meta, baseline, assert_valid_data(baseline, result.stdout))


def assert_clean_link_output(path: Path | None) -> None:
    if path is None or not path.exists():
        return
    text = path.read_text(errors="replace")
    for marker in ("ASlink-Warning", "Undefined Global"):
        if marker in text:
            raise AssertionError(f"{path}: linker output contains {marker!r}")


def assert_serial_symbols(path: Path | None) -> None:
    if path is None or not path.exists():
        return
    text = path.read_text(errors="replace")
    for symbol in ("_serialBuffer", "_serialBufferPosition", "_serialBufferReadPosition"):
        if symbol not in text:
            raise AssertionError(f"{path}: missing expected serial symbol {symbol}")


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--rom", required=True, type=Path)
    parser.add_argument("--map", type=Path)
    parser.add_argument("--symbols", type=Path)
    parser.add_argument("--baseline", action="append", default=[], type=Path)
    parser.add_argument("--git-baseline", action="append", default=[])
    args = parser.parse_args()

    rom_meta = assert_valid_rom(args.rom)
    for baseline in args.baseline:
        assert_matches_baseline(args.rom, rom_meta, baseline)
    for baseline in args.git_baseline:
        assert_matches_git_baseline(args.rom, rom_meta, baseline)
    assert_clean_link_output(args.map)
    assert_clean_link_output(args.symbols)
    assert_serial_symbols(args.symbols)

    print(
        "ok: {rom} title={title} size={size} cart=0x{cart:02X} ram=0x{ram:02X}".format(
            rom=args.rom,
            title=rom_meta["title"],
            size=rom_meta["size"],
            cart=rom_meta["cartridge_type"],
            ram=rom_meta["ram_size"],
        )
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
