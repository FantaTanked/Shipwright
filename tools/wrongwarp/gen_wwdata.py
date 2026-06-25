#!/usr/bin/env python3
# Regenerates soh/soh/Enhancements/Restorations/WrongWarpData.h from mzxrules' SpawnResults.json.
# Run from the repo root:  python tools/wrongwarp/gen_wwdata.py
import json, os

HERE = os.path.dirname(__file__)
OUT = os.path.join("soh", "soh", "Enhancements", "Restorations", "WrongWarpData.h")

def load(name):
    return json.load(open(os.path.join(HERE, name), encoding="utf-8-sig"))

spawn = load("SpawnResults.json")
recs = sorted({(r["Scene"], r["Spawn"], r["Cs"], r["Out"]) for r in spawn})

lines = [
    "// Generated from mzxrules SpawnResults.json. Out: 1=invalid,2=garbage,3=clean control,4=cutscene; cs=-1=any.",
    "// DO NOT EDIT BY HAND -- regenerate: python tools/wrongwarp/gen_wwdata.py",
    "#pragma once",
    "#include <cstdint>",
    "struct WrongWarpOutcome { uint8_t scene; uint8_t spawn; int8_t cs; uint8_t out; };",
    "static const WrongWarpOutcome kWrongWarpOutcomes[] = {",
]
line = "    "
for (sc, sp, cs, o) in recs:
    line += "{%d,%d,%d,%d}," % (sc, sp, cs, o)
    if len(line) > 110:
        lines.append(line); line = "    "
if line.strip():
    lines.append(line)
lines.append("};")
open(OUT, "w", encoding="utf-8").write("\n".join(lines) + "\n")
print(f"wrote {OUT} ({len(recs)} records)")
