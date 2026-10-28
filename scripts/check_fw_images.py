#!/usr/bin/env python3
"""Verify the A/B firmware image metadata produced by scripts/build.sh.

Checks the link base, absolute slot marker, vector table, version string and
the 8 MiB slot limit.  This is a build check, not a host unit test.

Usage:
  check_fw_images.py --version 0.1.0 \
      --a-elf .debug/nuttx-a.elf --a-bin .debug/nuttx-a.bin \
      --b-elf .debug/nuttx-b.elf --b-bin .debug/nuttx-b.bin
"""

import argparse
import os
import re
import struct
import subprocess
import sys

SLOT_A_BASE = 0x90000000
SLOT_B_BASE = 0x90800000
SLOT_SIZE = 8 * 1024 * 1024


def fail(message: str) -> int:
    print(f"check_fw_images: FAIL: {message}", file=sys.stderr)
    return 1


def symbols(elf: str, nm: str) -> dict:
    out = subprocess.run(
        [nm, "-g", "--defined-only", elf],
        check=True,
        text=True,
        capture_output=True,
    ).stdout
    result = {}
    for line in out.splitlines():
        parts = line.split()
        if len(parts) >= 3:
            try:
                result[parts[2]] = int(parts[0], 16)
            except ValueError:
                continue
    return result


def check_image(tag: str, elf: str, binary: str, base: int, slot: str,
                version: bytes, build_id: bytes, nm: str) -> list:
    errors = []

    try:
        syms = symbols(elf, nm)
    except (OSError, subprocess.CalledProcessError) as exc:
        return [f"{tag}: cannot read symbols: {exc}"]

    if syms.get("_stext") != base:
        errors.append(f"{tag}: _stext={syms.get('_stext')} expected {base:#x}")
    if syms.get("__vg_link_base") != base:
        errors.append(
            f"{tag}: __vg_link_base={syms.get('__vg_link_base')} "
            f"expected {base:#x}")
    expected_marker = ord(slot)
    if syms.get("__vg_slot_letter") != expected_marker:
        errors.append(
            f"{tag}: __vg_slot_letter={syms.get('__vg_slot_letter')} "
            f"expected {expected_marker:#x}")

    try:
        with open(binary, "rb") as f:
            data = f.read()
    except OSError as exc:
        return errors + [f"{tag}: cannot read binary: {exc}"]

    if len(data) > SLOT_SIZE:
        errors.append(f"{tag}: image {len(data)} bytes exceeds {SLOT_SIZE}")
    if len(data) < 8:
        return errors + [f"{tag}: image too short for a vector table"]

    stack, reset = struct.unpack_from("<II", data, 0)
    if not (0x20000000 <= stack < 0x40000000 and stack % 4 == 0):
        errors.append(f"{tag}: initial SP {stack:#x} is not a valid SRAM stack")
    reset_addr = reset & ~1
    if reset & 1 == 0 or not (base <= reset_addr < base + SLOT_SIZE):
        errors.append(f"{tag}: reset vector {reset:#x} is outside the slot")
    if version not in data:
        errors.append(f"{tag}: version string {version!r} not in binary")
    if build_id and build_id not in data:
        errors.append(f"{tag}: build id {build_id!r} not in binary")

    return errors


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--version", required=True)
    parser.add_argument("--build-id", default="")
    parser.add_argument("--a-elf", required=True)
    parser.add_argument("--a-bin", required=True)
    parser.add_argument("--b-elf", required=True)
    parser.add_argument("--b-bin", required=True)
    args = parser.parse_args()

    if not re.fullmatch(r"\d+\.\d+\.\d+", args.version):
        return fail(f"version {args.version!r} is not MAJOR.MINOR.PATCH")

    nm = os.environ.get("NM", "arm-none-eabi-nm")
    version = args.version.encode()
    build_id = args.build_id.encode() if args.build_id else b""

    errors = []
    errors += check_image("A", args.a_elf, args.a_bin, SLOT_A_BASE, "A",
                          version, build_id, nm)
    errors += check_image("B", args.b_elf, args.b_bin, SLOT_B_BASE, "B",
                          version, build_id, nm)

    with open(args.a_bin, "rb") as f:
        a_data = f.read()
    with open(args.b_bin, "rb") as f:
        b_data = f.read()
    if a_data == b_data:
        errors.append("A and B binaries are identical")

    if errors:
        for error in errors:
            print(f"check_fw_images: {error}", file=sys.stderr)
        return 1

    print(
        f"check_fw_images: OK version={args.version} "
        f"A={SLOT_A_BASE:#x} B={SLOT_B_BASE:#x} "
        f"a_size={len(a_data)} b_size={len(b_data)}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
