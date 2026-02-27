#!/usr/bin/env python3

import os
import sys

if len(sys.argv) < 2:
    print("Usage: python make_seeds.py <output_directory>")
    sys.exit(1)

outdir = sys.argv[1]

os.makedirs(outdir, exist_ok=True)

for b in range(256):
    filename = os.path.join(outdir, f"seed_{b:02x}.bin")
    with open(filename, "wb") as f:
        f.write(bytes([b]) * 150)

print(f"wrote {len(os.listdir(outdir))} files to {outdir}/")