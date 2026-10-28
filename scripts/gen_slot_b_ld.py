#!/usr/bin/env python3
"""Generate the slot-B NuttX linker script from the slot-A script.

The same objects are linked twice.  Only the flash origin, the slot size and
the absolute slot marker change; keeping one source script avoids drift.

Usage: gen_slot_b_ld.py <slot-a.ld> <slot-b.ld>
"""

import re
import sys


def main() -> int:
    if len(sys.argv) != 3:
        print("usage: gen_slot_b_ld.py <slot-a.ld> <slot-b.ld>", file=sys.stderr)
        return 2

    src, dst = sys.argv[1], sys.argv[2]
    with open(src, "r", encoding="utf-8") as f:
        text = f.read()

    text, flash_n = re.subn(
        r"(?m)^(\s*flash\s*\(rx\)\s*:\s*ORIGIN\s*=\s*)0x90000000"
        r"(,\s*LENGTH\s*=\s*)64M\s*$",
        r"\g<1>0x90800000\g<2>8M",
        text,
    )
    if flash_n != 1:
        print("gen_slot_b_ld: flash memory line not found", file=sys.stderr)
        return 1

    text, slot_n = re.subn(
        r"(?m)^__vg_slot_letter = 0x41;\s*$",
        "__vg_slot_letter = 0x42;",
        text,
    )
    if slot_n != 1:
        print("gen_slot_b_ld: slot marker not found", file=sys.stderr)
        return 1

    text, base_n = re.subn(
        r"(?m)^__vg_link_base = ORIGIN\(flash\);\s*$",
        "__vg_link_base = ORIGIN(flash);\n"
        'ASSERT(ORIGIN(flash) == 0x90800000, "slot B flash base");',
        text,
    )
    if base_n != 1:
        print("gen_slot_b_ld: link base symbol not found", file=sys.stderr)
        return 1

    text = (
        "/* Generated from "
        + src.replace("\\", "/")
        + " for slot B.  Do not edit. */\n"
        + text
    )
    with open(dst, "w", encoding="utf-8") as f:
        f.write(text)
    return 0


if __name__ == "__main__":
    sys.exit(main())
