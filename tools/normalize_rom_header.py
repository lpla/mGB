#!/usr/bin/env python3
"""Normalize legacy mGB ROM header bytes after GBDK-2020 makebin."""

from __future__ import annotations

import argparse
from pathlib import Path


def update_checksums(data: bytearray) -> None:
    header_checksum = 0
    for byte in data[0x134:0x14D]:
        header_checksum = (header_checksum - byte - 1) & 0xFF
    data[0x14D] = header_checksum

    data[0x14E] = 0
    data[0x14F] = 0
    global_checksum = sum(data) & 0xFFFF
    data[0x14E] = global_checksum >> 8
    data[0x14F] = global_checksum & 0xFF


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("rom", type=Path)
    args = parser.parse_args()

    data = bytearray(args.rom.read_bytes())
    if len(data) < 0x150:
        raise SystemExit(f"{args.rom}: too small to be a Game Boy ROM")

    data[0x144] = 0x00
    data[0x145] = 0x00
    update_checksums(data)
    args.rom.write_bytes(data)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
