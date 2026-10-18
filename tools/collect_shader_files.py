#!/usr/bin/env python3
"""Copies the shader containers a game keeps as files (SA2: Shader/xbox360/obj/*.wvu / *.wpu; Space Channel 5 Part 2:
ch52_g/ShaderXbox360/*.wvu / *.wpu) into a dump folder, named the way the run-time dump names them
(<stage>_<XXH3-64 of the whole container>.bin), so tools/build_shader_library.sh takes them without a run that
creates every shader first.

Each file must be exactly one 2008 container: signature 0x102A11xx (bit 0 of the low byte set for a vertex shader),
then the virtual and physical sizes, which add up to the file size. Anything else is listed and skipped.

  python3 tools/collect_shader_files.py --out out/sc5p2_shaders/containers ../assets/game_root/ch52_g/ShaderXbox360
"""
import argparse
import glob
import os
import struct
import sys

import xxhash


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument('inputs', nargs='+', help='container files, or folders of *.wvu / *.wpu')
    ap.add_argument('--out', required=True)
    a = ap.parse_args()
    paths = []
    for p in a.inputs:
        if os.path.isdir(p):
            paths += sorted(glob.glob(os.path.join(p, '*.wvu')) + glob.glob(os.path.join(p, '*.wpu')))
        else:
            paths.append(p)
    os.makedirs(a.out, exist_ok=True)
    names = {}
    skipped = 0
    for p in paths:
        data = open(p, 'rb').read()
        if len(data) < 12:
            print(f'skip {p}: too short')
            skipped += 1
            continue
        sig, virtual, physical = struct.unpack_from('>III', data, 0)
        if (sig & 0xFFFFFF00) != 0x102A1100 or virtual + physical != len(data):
            print(f'skip {p}: not one container (signature {sig:08X}, sizes {virtual}+{physical}, file {len(data)})')
            skipped += 1
            continue
        name = f"{'vs' if sig & 1 else 'ps'}_{xxhash.xxh3_64_intdigest(data):016x}"
        names.setdefault(name, []).append(os.path.basename(p))
        with open(os.path.join(a.out, name + '.bin'), 'wb') as f:
            f.write(data)
    for name, files in sorted(names.items()):
        print(f"{name}  {' = '.join(files)}")
    print(f'{len(paths) - skipped} files, {len(names)} distinct containers, {skipped} skipped')
    return 0


if __name__ == '__main__':
    sys.exit(main())
