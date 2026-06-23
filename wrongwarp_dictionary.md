# Standard (Non-Heap) Wrong-Warp Dictionary — for Restorations/WrongWarp.cpp

> Source: a 44-agent research workflow over ZSR (warp-results-by-scene, cutscene-pointer-values),
> the discovery timeline, and per-warp web resolution. Scope = STANDARD (non-heap, deterministic)
> cutscene-pointer wrong warps only. Heap warps (LACS / bombchu / dynawarp) are deferred (§3).

## The structural split (load-bearing)

- **Case-1 (scene owns its cutscene):** the destination scene has the `0x18` alt-setup header, so the
  arriving cutscene NUMBER (cs 0/1/3/7; setup = 4+N) selects *that scene's own* cutscene. The pointer is
  **refreshed** — no stale read. **Deterministic, version-independent. These almost certainly already
  work on mainline** (the scene plays its own legit cutscene → its terminator → result, exactly as
  console). The whole-rdram alt-setup residency covers them. **→ likely need NO intervention; verify.**
- **Case-2 (stale-pointer carry-over):** the destination scene has no setup-0 cutscene, so the carried
  `csCtx.segment` is read against the new scene's RAM. **This is the ganondoor / jabu→DC / 1080 family —
  the class that needs the apply half** (force destination + frame-1, fixing the few-seconds defect and
  the broken cases).

**So the apply-half work is small: ~4 Case-2 warps. Case-1 (the long §1B list) is mostly already faithful.**

## Case-2 dictionary (the apply-half targets)

| warp | arrive scene | carried cutscene (key) | cs# | destination | entrance | status |
|---|---|---|---|---|---|---|
| **ganondoor** | Tower Collapse Interior | Deku Tree Intro | 0xFFF1 | Tower Collapse Interior, **entrance 0x0257**, control frame-1 | **0x0257 RESOLVED** | ready (regression ref) |
| **1080** | Tower Collapse Interior | (route-filled; not canonical) | 0xFFF1 | **entrance 0x0257** (same as ganondoor; distinguish by source = Fire Temple) | **0x0257 RESOLVED** | ready |
| **jabu→DC** | Dodongo's Cavern Boss Room | {Goron City / DMT intro / Title} | 0xFFF0 | DC Boss Room (King Dodongo) | base+4+0 — **needs decomp `gEntranceTable` lookup** | resolve entrance |
| **deku→bongo / deku-timer** | Shadow Temple Boss Room (Bongo) | NOT Deku-Intro/Title; {Bolero/Minuet (1.1/1.2), Forest-complete (1.0)} | 0xFFF1 | Shadow Boss Room (adult) | ~0x0413 **UNCONFIRMED** — verify from decomp | resolve entrance |

## Keying

- **Case-1:** key `(destination sceneId, cutsceneIndex N∈{0,1,3,7})`. No last-cutscene needed. Base entrances
  derivable as `gEntranceTable base(scene) + 4 + N` (resolve from decomp; Kokiri Forest cs1 = 0x00F3 given).
- **Case-2:** key `(sceneId, cutsceneIndex N, last-watched-cutscene IDENTITY)`. ganondoor = `0x0257` +
  `0xFFF1` + last-cutscene Deku-Intro. Track the last cutscene by **semantic identity** (the OTR resource
  name at install), not the v1.0 RAM address (`0x801CA208`/`0x803825B0` are v1.0-only).

## §3 Deferred (heap-dependent — out of scope)
Dynawarp ("wrong warp to any scene"), LACS, bombchu heap-WW, Fire-Temple-death→Forest (reclassified
heap: Bolero pointer overwritten by tail allocs → softlock). jabu→Volvagia = void-loop (not clean; track
with crashes, not heap).

## §4 Notable crash/void (for a future faithful-crash layer)
Always-crash scenes: Lots'o'Pots, Zora River. cs3/cs7 crash for most Case-1 scenes. Softlocks: Inside
Jabu cs0, Ice Cavern cs0, Zora's Fountain cs1. jabu→Volvagia void-loop.

## §5 Gaps to close before/while encoding
1. **Entrance hexes (I resolve from `gEntranceTable`):** jabu→DC, deku→bongo (~0x0413 unconfirmed), all
   Case-1 bases except Kokiri (0x00F3) and ganondoor/1080 (0x0257).
2. **Unverified "gives control after CS":** Gerudo Valley/Fortress cs0/1, Ganon's Castle cs0 — capture-run.
3. **Lake Hylia Case-1-vs-stale** framing — check scene header for `0x18`.
4. **Non-1.0 cutscene-pointer identities** absent in sources — capture per version if targeting non-1.0.

## Apply-half plan (next)
1. **Last-cutscene tracker** — record the OTR identity of the last-played cutscene (hook the cutscene
   install/process), for the Case-2 key.
2. **Apply** (gated `gWrongWarp`): at the wrong-warp moment, if a Case-2 row matches `(scene, N,
   lastCutscene)`, set `nextEntranceIndex`/`cutsceneIndex`/transition to the destination and skip the
   cutscene → correct place, **frame-1 control**.
3. **Case-1:** verify they already work on mainline; intervene only where they don't.
4. **Verify** via the shipped capture diagnostic (the `[WrongWarp]` log must match the dictionary).
5. **Regression gate:** ganondoor → 0x0257 stays correct.
