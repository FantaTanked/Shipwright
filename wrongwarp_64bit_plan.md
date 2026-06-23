# Wrong-Warp / Glitch Outcomes on 64-bit Ship — Audit + Plan

> Branch `develop-speedrun` (off `develop`, mainline 64-bit). Goal: heap / dynawarp / cs-pointers /
> SRM / RBA reproduce their **console OUTCOMES via the actual route, validated** — NOT byte-exact.
> Grounded in a 58-agent audit of SoH's existing glitch-restoration infrastructure + Garrett OOT
> PR#37 (race build) + HarbourMasters PR#6669 (GIM fix). Faithful 32-bit work is preserved on
> `srm-cs-faithful-research`; this branch is the outcome-based 64-bit effort.

## 0. CURRENT SCOPE (decided)

**Now:** the *standard* (non-heap) cutscene-pointer wrong warps — ganondoor, jabu→DC, and the
documented `(carried-over cutscene, dest scene) → destination` set. These read resident scene/overlay
data, have **no fragile heap-placement dependency**, and resolve from documented facts. Build on 64-bit
`develop-speedrun` in the VB house style.

**Deferred (future):** heap-manipulation wrong warps (LACS, bombchu heap-WW, dynawarp) and their
**key-state "watch" validation** (snapshot the arena node-order + actor field values at the critical
moment, match against a reference captured from the 32-bit faithful build). Rigorous and achievable, but
substantial — out of scope for now. The 32-bit faithful build remains the reference factory for when we
do it.

## 1. What SoH ALREADY restores (the surprise: a lot)

SoH has a large glitch-restoration catalog already on mainline — built almost entirely through the
**GameInteractor VB ("vanilla behaviour") system**:

- **SRM/RBA — the most complete restoration in the whole catalog.** `BottleAdventure` (always-on,
  VB-driven) implements the full in-`gSaveContext` array-overrun read+write primitive
  (`RegisterBottleAdventure` BottleAdventure.cpp:460; VBs `VB_SET_BUTTON_ITEM_FROM_C_BUTTON_SLOT`
  z_parameter.c:1477/1629, `VB_UPDATE_BOTTLE_ITEM` :2682). Partial only at struct-padding regions
  (stubbed) + can't escape gSaveContext (layout model, not raw RDRAM overrun) — **but the route RBA
  setups don't need those.** RBA is effectively done.
- **Get-item SRM (GIM)** — HarbourMasters PR#6669 (OPEN, not merged): negative/OOB get-item ID →
  console table-overread substitute, via a **doc-transcribed** table (ItemTableManager.cpp:23,
  `GimIdTable[128]`). Approximation, self-documents holes.
- **Movement/timing — broad and mostly full:** N64WeirdFrames (weirdshots/slides, byte-exact ROM
  overread for 4 tables), QuickPutaway, HoverFishing, QuickBongoKill, EarlyEyeballFrog,
  WideShutterDoorRange, GraveHoles, BombchusOOB, EasyISG, EasyQPA, inverse-gates for softlock/BGS.

**The two gaps — and they're exactly our focus:**
- **HEAP (actor-arena fragmentation):** NOTHING on mainline. All heap fidelity is the 32-bit faithful
  branch's byte-exact work, which does not port to 64-bit.
- **CS-POINTER (wrong warps):** only the VB **hook points** exist on mainline
  (`VB_PLAY_ENTRANCE_CS` z_demo.c:2202, `VB_PLAY_PULL_MASTER_SWORD_CS` :502,
  `VB_PLAY_DROP_FISH_FOR_JABU_CS` :506). No faithful resolution. The `cmd->base` terminator failsafe
  (z_demo.c:586) is **completely unowned — no rando/skip/PR touches it. It is entirely ours to build.**

## 2. The house style we MUST match

All glitch behaviour is gated through **one** mechanism — match it so we slot in, not duplicate:

- **Decomp side:** `GameInteractor_Should(VB_FLAG, <vanilla_result>, <ctx…>)` — a boolean veto over a
  single decision. (Note: `GameInteractor_ShouldVanilla` does NOT exist; `GameInteractor_Should` is
  the only entry point, GameInteractor_Hooks.cpp:248.)
- **Enhancement side:** `REGISTER_VB_SHOULD(flag, body)` (GameInteractor.h:144) — ID-keyed lambda;
  write `*should`, read varargs via `va_arg` on the macro-copied `args`; one-shot self-unregister via
  `UnregisterGameHookForID` where appropriate (canonical: Warping.cpp:92).
- **One cvar** per feature (`CVAR_ENHANCEMENT("…")` + menu + ConfigUpdater rename).
- **Never edit z_demo.c / z_scene.c logic directly** — attach at the existing VBs.

## 3. Reusable prior art

- **Garrett PR#37's `(entranceIndex, cutsceneIndex[, chamberCutsceneNum]) → (destEntrance, destCs)`
  map** = a **play-tested regression ORACLE** for the in-bounds blue-warp set (Deku 0xEE/0xFFF1→0x457,
  DC 0x13D/0xFFF1→0x47A, Jabu, Forest/Fire/Water/Spirit/Shadow via `chamberCutsceneNum`, Zelda-escape
  0xCD). Our faithful resolver MUST agree with it on covered cases. It is **anti-faithful** (forces
  destinations upstream, in-bounds only) — no help for OOB carry-overs (ganondoor, jabu→DC). Key insight:
  the key is `(entrance, cs, chamberCutsceneNum)`, not just `(entrance, cs)`.
- **The csCtx.segment-writer checklist** (our coverage list): `Scene_CommandCutsceneData` (z_scene.c:457,
  the shared chokepoint), `Cutscene_SetSegment` (z_demo.c:2263 — entrance-table + ~60 actors), tower
  barrier (z_demo.c:1265). Actor writers: En_Xc (Sheik songs), En_Du, Item_Ocarina, En_Okarina_Tag,
  Bg_Gjyo_Bridge, boss CS machines (Boss_Ganon/2/drof/Tw, Demo_Im).
- **PR#6669 = method contrast:** a transcribed table is the *opposite* of reading real bytes; lesson —
  the higher-fidelity path overreads actual data, not a hand-table.

## 4. Two paths to resolve cs-pointer destinations on 64-bit — DECISION POINT

The destination ultimately comes from `cmd->base` → the real `Cutscene_Command_Terminator` switch
(z_demo.c:586), which is pure decomp logic and 1:1 on mainline. The only question is **how we obtain the
right `cmd->base` for a wrong warp** without the byte-exact heap:

- **(A) Rule table.** A `(conditions → cmd->base)` table (oracle for in-bounds; captured bases for OOB
  — we have ganondoor's + jabu→DC's from the faithful `[CsTerm]` logs). Simpler; closer to hardcoding;
  needs base data per warp; doesn't generalize to undiscovered cases.
- **(B) Port the cs-pointer residency.** Bring the faithful **data-residency** (rdram buffer @0x80000000
  + resident scene/room/overlay bytes + carry-over `gFaithfulCs*` trackers + the generalized stale-read)
  to 64-bit — **without** the byte-exact heap. The stale read then resolves `csCtx.segment` to real
  resident bytes → `cmd->base` from actual data → genuinely computed, not hardcoded, and generalizes to
  any OOB case. **This is portable to 64-bit because the cs-pointer mechanism is data+logic, not
  byte-exact-heap-dependent** (the buffer-at-0x80000000 + data-fill both work on x64; the trackers are
  u32 separate from the 8-byte host `csCtx.segment`).

**Recommendation: (B) is the "not hardcoded, 1:1" path the goal asks for, and the audit endorses it.**
But prove the fire mechanism + house style on ganondoor with the minimal version first (§6), then port
the residency for generality. (A)'s table doubles as the regression oracle for (B).

## 5. The bounded build list

1. **Generalized stale-read interception** at the shared chokepoint `Scene_CommandCutsceneData`
   (z_scene.c:457) + `Cutscene_SetSegment` (z_demo.c:2263) + tower barrier (z_demo.c:1265): record the
   segmented address + a fresh-this-scene identity flag so **all** carry-over sources (not just
   ganondoor) resolve to the vanilla address — fixing jabu→DC's host-pointer void loop.
2. **The synthesized-cutscene fire mechanism** — on a stale carry-over, drive the real
   `Cutscene_ProcessCommands` against resolved bytes (or a synthesized `[instant-terminate header]
   [CS_CMD_TERMINATOR base][CS_CMD_STOP]`), giving **frame-1 control like vanilla** (fixing SoH's current
   "few-seconds cutscene" defect) + the correct `cmd->base` → destination.
3. **Last-cutscene tracker** — records which cutscene last completed; selects clean-vs-softlock outcome.
4. **The `(conditions → cmd->base)` resolution** (path A) or the residency stale-read (path B).
5. **VB-style gating + one cvar** (`CVAR_ENHANCEMENT("FaithfulWrongWarp")`) via `REGISTER_VB_SHOULD` on
   `VB_PLAY_ENTRANCE_CS` / the terminator VBs. Menu + ConfigUpdater.
6. **Regression gate:** ganondoor stays correct; in-bounds set matches Garrett's oracle exactly.

**Explicitly NOT needed:** heap byte-exactness on 64-bit; `Entrance_OverrideCutsceneEntrance`
(skip-orthogonal); a new dispatch system; RBA out-of-struct reach.

## 6. First build (ganondoor, house idiom)

A `CVAR_ENHANCEMENT("FaithfulWrongWarp")` feature that, via `REGISTER_VB_SHOULD`, detects the wrong-warp
condition (stale carry-over, `cutsceneIndex ≥ 0xFFF0`, no fresh cutscene) and installs a **synthesized
instant-terminating cutscene carrying ganondoor's known `cmd->base`** at `csCtx.segment`, so the real
`Cutscene_Command_Terminator` fires → ganondoor's destination + **frame-1 control**. Validate: ganondoor
lands where it does today (correct) but now *instantly* (defect fixed). Then generalize to jabu→DC
(currently a host-pointer void loop) — the proof that the chokepoint interception flips the broadest set
of sources at once.
