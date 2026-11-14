#!/usr/bin/env python3
"""Apply mining GPU support and PCIe Gen2 patches to nv-kernel.o_binary."""

from pathlib import Path
import sys

# https://github.com/dartraiden/NVIDIA-patcher/blob/857547313869458f39c1abed55a2601c4459d964/linux.sh
CMP_PATTERN_NAME = "CMP"
# https://github.com/WJNKAC/nvidia-p104-unlock-gen2-for-linux/blob/19d59424ec9d69fe0283706e1ce4e7d70fd258fb/drivers/580.178.04/offsets.sh
GEN2_PATTERN_NAME = "PCIe Gen2"
GEN2_EXPECTED_MATCHES = 1
PATTERNS = (
    ("P1xx",
     "071b0700871b0700c71b0700071c0700091c07",
     "ffff0700ffff0700ffff0700ffff0700ffff07"),
    ("CMP",
     "091e0700491e0700bc1e0700fc1e07000b1f0700812007008220070083200700c2200700892107000d2207004d2207008a2407",
     "ffff0700ffff0700ffff0700ffff0700ffff0700ffff0700ffff0700ffff0700ffff0700ffff0700ffff0700ffff0700ffff07"),
    ("RTX 3080 Ti 20 GB",
     "c7210000052200000922000014220000",
     "c7210000ffff00000922000014220000"),
    ("RTX 3060 3840SP",
     "f7240000f82400000025000001250000",
     "f7240000f824000000250000ffff0000"),
    ("RTX 4070 10 GB",
     "85270000af270000bf270000c2270000",
     "ffff0000af270000bf270000c2270000"),
    ("NVIDIA L40 ES",
     "87260000af260000b0260000bf260000",
     "87260000ffff0000b0260000bf260000"),
    ("RTX 5080M ES N22W-ES-A1",
     "172c00001a2c00002b2c00002c2c0000",
     "172c00001a2c00002b2c0000ffff0000"),
    (GEN2_PATTERN_NAME,
     "8d42ff415641554989fd41544531e4534883ed404889751089550c83f8020f87",
     "8d42fe415641554989fd41544531e4534883ed404889751089550c83f8010f87"),
)


def main():
    if len(sys.argv) != 2:
        sys.exit(f"Usage: {sys.argv[0]} /path/to/nv-kernel.o_binary")

    path = Path(sys.argv[1])
    original = path.read_bytes()
    patched = original
    cmp_matches = 0
    for name, search_hex, replacement_hex in PATTERNS:
        search = bytes.fromhex(search_hex)
        replacement = bytes.fromhex(replacement_hex)
        if len(search) != len(replacement):
            sys.exit(f"Patch size mismatch: {name}")
        count = patched.count(search)
        print(f"{name}: {count} occurrence(s)")
        if name == CMP_PATTERN_NAME:
            cmp_matches = count
        if name == GEN2_PATTERN_NAME and count != GEN2_EXPECTED_MATCHES:
            sys.exit(f"Gen2 signature must occur exactly {GEN2_EXPECTED_MATCHES} time(s); found {count}")
        patched = patched.replace(search, replacement)

    if not cmp_matches:
        sys.exit("CMP pattern missing; check the driver version and patch patterns")
    if len(patched) != len(original):
        sys.exit("Patched binary size mismatch")
    path.write_bytes(patched)


if __name__ == "__main__":
    main()
