#!/usr/bin/env python3
# Offline replication of mzxrules' wrong-warp algorithm for spot-checking.
# Run from anywhere:  python tools/wrongwarp/analyze.py
import json, os
from collections import defaultdict, Counter

HERE = os.path.dirname(__file__)
def load(name): return json.load(open(os.path.join(HERE, name), encoding="utf-8-sig"))

ent = load("EntranceTable.json")
spawn = load("SpawnResults.json")
scenes = {s["Id"]: s["Scene"] for s in load("Scenes.json")}
byidx = {e["Index"]: e for e in ent}

res = defaultdict(list)
for r in spawn:
    res[(r["Scene"], r["Spawn"], r["Cs"])].append(r)
def getRes(sc, sp, cs):
    return res.get((sc, sp, cs), []) or res.get((sc, sp, -1), [])

print("Out codes:", dict(Counter(r["Out"] for r in spawn)))

# replicate: base entrance, +cs+4 overshoot, look up outcome
clean = []
for x in (e for e in ent if e["Base"] == e["Index"]):
    for cs in range(8):
        li = x["Index"] + cs + 4
        if li in byidx:
            d = byidx[li]
            for r in getRes(d["Scene"], d["Spawn"], cs):
                if r["Out"] == 3:
                    clean.append((x["Index"], cs, li, d["Scene"], scenes.get(d["Scene"], "?"), d["Spawn"]))

print(f"clean (Out=3) control wrong warps: {len(clean)}")
# ganondoor validation
for (idx, cs, li, sc, nm, sp) in clean:
    if li == 0x257:
        print(f"  ganondoor: start=0x{idx:X} cs={cs} -> 0x{li:X} {nm} spawn={sp} (Out=3)")
