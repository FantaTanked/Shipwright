# Savestates — Handover

_Branch: `develop-gz` • Last updated: 2026-06-15_

Context for picking this work up cold. Covers the shipped loadzone fix and the
plan for on-disk savestate export/import.

---

## 1. Shipped: loadzone / transition-actor fix

**Commit:** `fix(savestates): respawn transition actors after a savestate load` (latest on `develop-gz`).

**Symptom:** After loading an F-key savestate and re-entering a scene, crossing a
loading zone / door / crawlspace did nothing — the room transition silently never
fired. Same-scene loads worked; only scene re-entry after a load broke.

**Root cause:** Transition actors (doors, the `En_Holl` loadzone/crawlspace planes)
mark themselves "already spawned" by **negating their `id` in place** in the scene's
transition-actor list; the actor's `Destroy` un-negates it on scene exit. On N64 the
list was re-DMA'd from ROM each scene load, so it always started positive. SoH points
`transiActorCtx.list` at a **cached resource that is never reloaded in-session**, and a
**savestate load replaces the heap wholesale without running any actor's `Destroy`** —
so the id stays negative. The next visit's spawn loop (`if (id >= 0)`) skips the entry
and the transition actor never spawns.

**Fix:** In `Scene_CommandTransitionActorList` (`soh/soh/z_scene_otr.cpp`), reset every
entry id to positive when the list is established on scene load — restoring the N64
"fresh list" invariant. ~15 lines, one file. General (covers all transition actors).

**Verification:** Confirmed in-game on the Kokiri Forest maze crawlspace. Found via a
`[ship-gz][trans]` probe showing `id=-35 willSpawn=0` on the *second* scene load. All
diagnostic probes have been removed; only the fix remains.

---

## 2. Base-version status

The in-memory savestate feature is considered good enough to ship as a base version.
Remaining known gaps and why they don't block a base release:

- **Spot-check the fix generalizes (recommended, ~5 min):** verified only on the maze
  crawlspace. Try a normal door, a blue warp, and a load inside a dungeon. The fix is
  general so this is insurance, not an expected failure.
- **`z_player.c` interaction statics uncaptured** (`sTouchedWallFlags`, `sFloorType`,
  `sConveyorSpeed`, …): live outside the snapshotted heap. Recompute each frame from
  collision/input, so stale for ~1 frame then self-correct. Not the loadzone bug (that
  hypothesis was investigated and reverted). Effectively invisible.
- **Actors don't get `Destroy` on load:** we fixed the transition-actor consequence.
  Most actor `Destroy`s only free heap memory (moot — the heap is overwritten anyway).
  The transition-actor id was special because it mutated *cached resource memory outside
  the heap*. Other actors doing that in `Destroy` are rare; could resurface as an
  isolated oddity, not a base-version blocker.
- **No idempotency/determinism measurement:** gz's frame-perfect guarantee matters only
  for TAS-grade use. For "save a state, retry a fight/room," visually-correct +
  progressable is the bar, and we're there.

---

## 3. Next: on-disk export / import (planned, not started)

**Goal:** Let players save states to disk and reload them in a later session of the
**same build**, so they don't have to replay the game to recreate states. **No** cross
compatibility with real gz / GameCube states (different platform & RAM layout — not
feasible and not wanted). SoH's own state files only.

### Why it's bounded (not a research project)
OoT uses **segmented addressing** — actors reference object/scene data via segment
numbers resolved through `gSegments` each frame, not raw pointers. So the set of
*absolute* resource pointers that break across a process restart is small and
**enumerable**, and it is already enumerated by the `GzRebaseSet` in git commit
`4a87ee75f` (`feat(savestates): gz-style reload-then-restore for cross-scene loads`).
That reverted engine is the foundation — cross-session is the same problem as the
cross-scene case it handled (resources live at new addresses).

### What export/import needs
1. **Serialize/deserialize** the `SaveStateInfo` blob to a file + header (magic, build
   hash, scene id). Trivial I/O. The state today is a single
   `std::make_shared<SaveStateInfo>()` (~7.5 MB: two heap copies + scalar fields).
2. **Pin the heaps** — allocate `gSystemHeap`/`gAudioHeap` at **fixed virtual
   addresses** (`VirtualAlloc` / `mmap` fixed base) instead of `_aligned_malloc`
   (`soh/src/buffers/heaps.c`). Keeps every *internal* heap pointer valid across runs.
3. **Disable ASLR** on the exe (`/DYNAMICBASE:NO`, MSVC) so the code base is fixed and
   the function pointers baked into every saved actor (`init/destroy/update/draw`,
   `actionFunc`, plus an explicit `void(*)()` field) survive a restart. This makes
   states **build-specific** — add a build-hash gate in the file header so a state from
   a different build is rejected cleanly, not crash-loaded. **(Decision pending: OK to
   disable ASLR? It's the one real gate on the whole approach.)**
4. **Reload-then-restore + rebase on import** — revive `4a87ee75f`: load the saved
   scene fresh (repopulates ResourceManager + `gSegments` + PlayState resource fields at
   this session's addresses), overlay the saved heap, then rebase the `GzRebaseSet`
   pointers + `gSegments`. The cosmetic 1-frame "snap" was already solved via the
   deferred-restore work in that same history.

### The one real risk
The rebase set may miss an absolute resource pointer for some specific scene/actor —
same whack-a-mole flavor as the loadzone bug. Segmented addressing keeps that set small,
but not provably zero. Expect to test across scene types and fix stragglers iteratively.

### Suggested build order
Foundation first (high confidence): fixed-address heaps + ASLR off + serialize/import
scaffolding + build-hash header → confirm a state survives a restart **in the same
scene**. Then layer the reload-then-restore rebase for **cross-scene** states.

---

## 4. Reference
- Reverted reload-then-restore engine + `GzRebaseSet`: git commit `4a87ee75f` (no longer
  on the branch; recoverable from reflog / git history).
- The 9 prior exploratory commits (spikes, design/handover docs) were squashed out of
  `develop-gz`; old tip was `40ae2fc77` if anything there is needed.
- Active savestate code: `soh/soh/Enhancements/savestates.cpp` / `.h` /
  `savestates_extern.inc`; scene-command path in `soh/soh/z_scene_otr.cpp`.
