# Savestates — Handover (cross-session / gResArena work)

_Branch: `develop-gz` • Last updated: 2026-06-16_

Pick-up-cold context for the cross-session savestate effort. The in-memory
(same-session) savestate feature works; this round added on-disk export/import that
must survive **closing and reopening soh.exe**. That goal led to a fixed-base
**resource arena** (`gResArena`). **Current status: cross-session save/reload still
crashes in some cases — not yet shippable. Same-session F5/F7 should still work.**

---

## 1. The goal

Let a player press F5 (save) / F3 (export to disk), **fully close soh.exe**,
relaunch, F4 (import) / F7 (load), and be back exactly where they saved — same
build only. No compatibility with real gz/GameCube states.

Keys: **F5** save (in-memory), **F7** load, **F6** cycle slot, **F3** export slot
(opens a native Save dialog, defaulting to `savestates/savestate_<N>.gzs`), **F4**
import slot (native Open dialog). After F4, F7 applies. (F3/F4 chosen because F11 is
the libultraship fullscreen key and F2 is mouse-capture.)

---

## 2. Why this is hard (the core finding)

A savestate is a raw memory snapshot full of baked-in pointers. Three classes break
across a process restart, fixed in order of discovery:

1. **Internal heap pointers** → fixed by pinning `gSystemHeap`/`gAudioHeap` at fixed
   virtual addresses (`heaps.c`).
2. **Function pointers** baked into actors → fixed by disabling ASLR
   (`/DYNAMICBASE:NO`, `soh/CMakeLists.txt`) + a build-hash gate on the state file.
3. **Resource pointers** — the wall. Actors hold **direct pointers into
   `ResourceManager` (C++ heap) memory** (skeletons, display lists, vertices,
   collision, scene data). `DmaMgr_SendRequest1` is a **no-op in SoH**
   (`soh/src/boot/z_std_dma.c:439`) — objects are NOT copied into the game arena;
   the arena object/room buffers are vestigial. So `skelAnime.skeleton` etc. point
   straight into ResourceManager allocations that move (and are freed → `0xDDDD`)
   across a restart. These are pervasive (every actor with a skeleton/anim), not an
   enumerable set, so PlayState-field "rebasing" can never fix cross-session.

**Key memory-layout fact:** `SystemHeap_Init((void*)gSystemHeap, ...)` (`main.c:97`)
makes the system arena == `gSystemHeap`. PlayState, the game arena, room buffers and
object banks all live INSIDE the pinned heap. So a plain heap overwrite already
restores actors + their heap-internal pointers correctly. Only **ResourceManager
memory** is the problem.

---

## 3. The chosen solution: `gResArena` (fixed-base resource arena)

Route **all resource payload memory** to a bump arena at a **fixed virtual base**, so
it can be snapshot/restored like the heaps. Then a cross-session load restores the
arena verbatim and every actor pointer into resource memory is valid again — **no
reload, no rebase, no audio-skip, no snap.**

**Files (all inside the `libultraship` submodule unless noted):**

- `libultraship/include/ship/resource/GzResArena.h` — arena API + `GzResArena_RouteScope`.
- `libultraship/src/ship/resource/GzResArena.cpp` — the arena. Fixed base
  `0x0000030000000000`, 4 GiB reserve, commit-on-demand bump allocator (no per-object
  free — deliberate, see below). Also contains a **global `operator new`/`delete`
  replacement**: while a thread-local route depth is >0, `new` serves from the arena;
  `delete` is a no-op for arena pointers (range check `GzResArena_Owns`), else
  `_aligned_free`. Non-arena `new` → `_aligned_malloc`.
- `libultraship/src/ship/resource/ResourceLoader.cpp:~227` — wraps
  `factory->ReadResource(...)` in a `Ship::GzResArena_RouteScope`. **This is the one
  hook**: the entire parsed resource graph (the `Resource` object itself, its payload
  vectors, nested data) lands in the arena. (Chosen over per-type STL allocators
  because `Skeleton::GetPointer()` returns `&skeletonData`, a member of the Resource
  *object* created via `make_shared`, which typed allocators would miss.)
- `libultraship/include/ship/resource/ResourceManager.h` +
  `libultraship/src/ship/resource/ResourceManager.cpp` —
  `GzClearCacheForStateLoad()`: waits for the thread pool, locks `mMutex`, clears
  `mResourceCache`. Releases every cached `shared_ptr<IResource>` before the arena is
  overwritten.
- `soh/soh/Enhancements/savestates.cpp` / `.h` — save/load/export/import.
  `SaveState::resArenaCopy` (`std::vector<uint8_t>`) holds the arena snapshot.

**Why bump / no-free:** it makes the flush+restore safe. A free-list would have live
metadata in the arena that a verbatim restore would corrupt. Bump never frees, so
flushing the ResourceManager and overwriting the arena can't double-free. Tradeoff:
each state load orphans pre-restore resources (memory growth) — acceptable for a
practice tool; falls back to the normal heap if the 4 GiB reserve fills.

**Cross-session load sequence** (`SaveState::Load(crossSession=true)`):
1. `GzClearCacheForStateLoad()` — release all live Resource objects.
2. `GzResArena_Reset()` → `memcpy(base, resArenaCopy, size)` → `GzResArena_SetUsed(size)`.
3. Full heap + audio + static restore (same as same-session; no rebase, no audio-skip).

Same-session load (`fromDisk == false`): plain heap overwrite, no arena ops (resources
are still resident — bump never freed them).

---

## 4. Current status / what works

- **Builds clean.** Latest `x64/Debug/soh.exe` from 2026-06-16 ~11:07.
- **Normal play + scene changes work** with all resources routed through the arena
  (validated). Memory climbs over time (bump never reclaims) — expected.
- **Cross-session save/reload CRASHES in some cases** (the reason for this handover).
  Same-session F5/F7 should be intact (it doesn't touch the arena), but re-verify.

---

## 5. Debugging leads for the crashes (start here)

Crash logs: `x64/Debug/logs/Ship of Harkinian.log` — the crash handler prints a
**symbolicated traceback** + registers + scene. Grep for `Exception: 0xc0000005`,
take the **last** one. `0xDDDDDDDD` in a register = CRT freed-heap (a released
ResourceManager allocation). An address `0x3000_xxxx_xxxx` = inside the arena.

Likely suspects, roughly in order:

1. **Resources loaded OUTSIDE `ReadResource`** won't be in the arena, so their
   pointers are still stale cross-session. Audit paths that build resource data
   without going through `ResourceLoader::LoadResource` (some audio, some
   `ResourceMgr_*` helpers, anything `make_shared`-ing a payload directly, textures
   uploaded to GPU then freed). The crash stack names the system — chase that.
2. **Flush incompleteness.** `GzClearCacheForStateLoad` only clears `mResourceCache`.
   If any `shared_ptr<IResource>` is held elsewhere (factory static caches — e.g.
   `SkeletonPatcher::skeletons`, the renderer, gfx command buffers) it survives the
   flush, then its Resource object (in the arena) gets overwritten by the restore →
   corrupted vtable → crash on next use or destruct. Audit long-lived
   `shared_ptr<IResource>` holders.
3. **Arena overflow / fallback.** If `GzResArena_GetUsed()` exceeds 4 GiB during a
   long session before saving, later allocations fall back to the normal heap and are
   NOT in the snapshot → stale on restore. Check the used size at save time.
4. **Save-time vs load-time arena mismatch.** `Save()` snapshots `[base, used)`. If
   anything wrote arena memory between snapshot and the heap snapshot, or if the
   ordering with the audio mutex is wrong, you can capture an inconsistent image.
5. **Same-session crash?** If F5/F7 (not disk) now crashes, the regression is in the
   `Load()` rewrite or the global `operator new`/`delete` replacement (e.g. an
   allocation that crosses arena/non-arena and is freed by the wrong path). The
   global new/delete is the riskiest change — verify pointers allocated outside a
   route scope are never freed as arena and vice-versa.

Suggested next step: reproduce, read the latest crash's traceback, and identify which
resource/system the faulting pointer belongs to. If it's a resource type not going
through `ReadResource`, route it (or snapshot it). If it's a surviving `shared_ptr`,
extend the flush.

---

## 6. Build

VS BuildTools cmake is NOT on PATH:
```
"C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe" --build build/x64 --target soh --config Debug
```
If you add/remove files, reconfigure first: run the same cmake with just `build/x64`
(CMake GLOBs `resource/*.cpp`). Output: `x64/Debug/soh.exe`. Each rebuild changes the
build hash, so **old `.gzs` files are rejected** — re-save/re-export after a build.

Confirm ASLR off: `dumpbin /headers x64\Debug\soh.exe` → "DLL characteristics"
should NOT include the dynamic-base bit (value was `0x8120`).

---

## 7. Changed files summary

- `soh/src/buffers/heaps.c` — pinned `gSystemHeap`/`gAudioHeap` (VirtualAlloc/mmap fixed base).
- `soh/CMakeLists.txt` — `/DYNAMICBASE:NO` on the 64-bit link.
- `soh/soh/Enhancements/savestates.cpp` / `.h` — disk export/import, build-hash header,
  arena snapshot/restore, `Load(crossSession)`. (Also fixed an old `LoadMiscCodeData`
  self-memcpy bug.) Note: dead `deferred*` members remain in the header (harmless).
- `soh/soh/OTRGlobals.cpp` — F3 (export) / F4 (import) key handlers; opens the native
  file dialog (via `savestate_filedialog`) and passes the chosen path to the mgr.
- `soh/soh/Enhancements/savestate_filedialog.{h,cpp}` — isolated wrapper around
  portable-file-dialogs (kept in its own TU so the Windows shell headers it drags in
  don't collide with game enums like `PS_NONE`).
- `libultraship` (submodule, uncommitted): `GzResArena.{h,cpp}` (new),
  `ResourceLoader.cpp` (route scope), `ResourceManager.{h,cpp}`
  (`GzClearCacheForStateLoad`), `fast/.../Vertex.h` (touched then reverted — should be
  a no-op diff).

**Heads up:** the libultraship changes are in a **submodule** and are uncommitted.
Commit them inside the submodule (or stash carefully) so they aren't lost.

---

## 8. Reference

- Reverted reload-then-restore engine + `GzRebaseSet` (no longer used; superseded by
  the arena): git commit `4a87ee75f`.
- Shipped earlier: transition-actor (loadzone/crawlspace) fix in
  `soh/soh/z_scene_otr.cpp` — keep.
