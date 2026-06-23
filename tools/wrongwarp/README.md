# Wrong-warp data (mzxrules)

Source data + generator for the **Wrong Warps** glitch restoration
(`soh/soh/Enhancements/Restorations/WrongWarp.cpp`).

## Where the data comes from

mzxrules' wrong-warp calculator — <https://mzxrules.github.io/zelda64/ocarina/ww/> — which computes every
cutscene-pointer wrong-warp result directly from the NTSC-1.0 ROM. Its `app.js` loads three JSON files,
saved here verbatim:

- `EntranceTable.json` — every entrance `Index → { Base, Scene, Spawn, Dest }`.
- `SpawnResults.json` — every `(Scene, Spawn, Cs) → Out` outcome. `Out`: **1**=invalid room, **2**=unresolved
  scene-setup address (garbage/crash), **3**=clean gameplay control, **4**=a real cutscene plays. `Cs == -1`
  means "any cutscene number".
- `Scenes.json` — scene id → name (reference only).

## The algorithm (replicated in WrongWarp.cpp)

```
cs          = cutsceneIndex & 0xF                 # cutscene number (cutsceneIndex >= 0xFFF0)
lookupIndex = entranceIndex + cs + 4              # the "+cs+4" entrance overshoot
                                                  #   (the decomp itself does this:
                                                  #    gEntranceTable[entranceIndex + sceneSetupIndex],
                                                  #    sceneSetupIndex = 4 + cs  -- z_play.c:494/466)
dest        = gEntranceTable[lookupIndex]         # destination scene + spawn
Out         = SpawnResults[(dest.Scene, dest.Spawn, cs)]
```

For `Out == 3` (clean control) the feature sets `entranceIndex = lookupIndex, cutsceneIndex = 0`, so the
destination loads at setup 0 with no cutscene → control on frame 1, matching console.

Validated: ganondoor = `0x252` (Deku, from boss) `+ cs1 + 4 = 0x257` (Tower Collapse Interior, spawn 3) →
`Out=3`.

## Regenerating the bundled table

`WrongWarpData.h` (the compiled `(scene,spawn,cs)->Out` table) is generated, not hand-edited:

```
python tools/wrongwarp/gen_wwdata.py     # run from repo root
```

`analyze.py` replicates the algorithm offline for spot-checking / extracting the clean-warp list.
