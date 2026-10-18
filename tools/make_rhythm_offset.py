#!/usr/bin/env python3
"""Writes sc5p2_rhythm_offset.cpp (Space Channel 5 Part 2's rhythm offset) from its template and the generated code.

The rhythm offset runs the dance judge's update, sub_823EE390 (the task "ShInput<Judge>"), as a copy of the generated
function with three insertions: `now` taken the offset earlier, and two calls that note presses and misses for the
"[rhythm]" report. The copy is made at build time from the generated code of your own default.xex, so the repository
holds no recompiled game code (as sa2-nx's localized copies, tools/localize_native.py).

  python3 tools/make_rhythm_offset.py --generated app_sc5p2/generated/default \\
      --template app_sc5p2/src/cpu/sc5p2_rhythm_offset.cpp.in --out <build>/sc5p2_rhythm_offset.cpp
"""
import argparse
import glob
import os
import re
import sys

FUNCTION = 'sub_823EE390'
# (anchor, text, where): each anchor must appear exactly once in the generated function.
INSERTIONS = [
    ('\t// lwz r27,100(r1)\n\tr27.u64 = REX_LOAD_D_U32(ctx.r1.u32, 100);\n',
     '\t// sc5p2: the rhythm offset. `now` (ticks since the task\'s start) is taken that much earlier.\n'
     '\tr27.u64 = uint32_t(int32_t(r27.u32) - OffsetTicks(base));\n', 'after'),
    ('\tctx.lr = 0x823EE5BC;\n\tsub_823EE250(ctx, base);\n',
     '\tNotePress(base, ctx.r3.u32, ctx.r4.u32, int32_t(r27.u32));\n', 'before'),
    ('\tcr6.compare<int32_t>(ctx.r3.s32, 3, xer);\n\t// blt cr6,0x823ee564\n\tif (cr6.lt) goto loc_823EE564;\n',
     '\tNoteMiss();\n', 'after'),
]


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument('--generated', required=True, help='the code generator\'s output folder (generated/default)')
    ap.add_argument('--template', required=True)
    ap.add_argument('--out', required=True)
    a = ap.parse_args()
    body = None
    pattern = re.compile(r'DEFINE_REX_FUNC\(' + FUNCTION + r'\) \{\n(.*?)\n\}\n', re.S)
    for path in sorted(glob.glob(os.path.join(a.generated, '*_recomp.*.cpp'))):
        with open(path, encoding='utf-8', errors='replace') as f:
            m = pattern.search(f.read())
        if m:
            body = m.group(1)
            break
    if body is None:
        sys.exit(f'{FUNCTION} is not in {a.generated}: run the code generator first')
    for anchor, text, where in INSERTIONS:
        if body.count(anchor) != 1:
            sys.exit(f'{FUNCTION} is not the code the rhythm offset was written for (anchor {anchor.strip()[:40]!r})')
        body = body.replace(anchor, anchor + text if where == 'after' else text + anchor)
    with open(a.template, encoding='utf-8') as f:
        template = f.read()
    if template.count('@@JUDGE_BODY@@') != 1:
        sys.exit('the template has no @@JUDGE_BODY@@')
    os.makedirs(os.path.dirname(os.path.abspath(a.out)), exist_ok=True)
    text = template.replace('@@JUDGE_BODY@@', body)
    old = None
    if os.path.exists(a.out):
        with open(a.out, encoding='utf-8') as f:
            old = f.read()
    if old != text:  # unchanged output keeps its time stamp: no rebuild
        with open(a.out, 'w', encoding='utf-8') as f:
            f.write(text)
    return 0


if __name__ == '__main__':
    sys.exit(main())
