# Compiled Arena Cache — Design Note

_Branch: `develop-gz` • Status: proposed, not yet implemented_

This note specs a **compiled arena cache**: a one-time, on-disk snapshot of the
fully-populated resource arena (`gResArena`) that is reloaded verbatim on every
subsequent boot. It exists to solve two problems at once:

1. **Boot cost** of the deterministic full preload (parse/decompress every resource
   each launch) — pay it **once**, then reuse.
2. **Cross-session savestate corruption** — caused by the bump arena's offsets being
   **load-order dependent**, so each process lays resources out differently and a
   verbatim `.gzs` restore clobbers/aliases the new process's live resource pointers.

The cache makes the arena layout **byte-identical across every boot**, which removes
the load-order variance entirely — so a `.gzs` restore becomes sound by construction.

---

## 1. Background (why this works)

The existing cross-session machinery already relies on three invariants that this
design reuses unchanged:

- **Fixed virtual base.** `gResArena` is reserved at `0x0000030000000000`
  (`GzResArena.cpp`), so absolute pointers into it are stable across launches.
- **ASLR disabled** (`/DYNAMICBASE:NO`). C++ vtable pointers in restored `Resource`
  objects remain valid across launches of the same binary.
- **Build-hash gate.** State files are rejected if the build hash differs.

Because the `ReadResource` route scope (`ResourceLoader.cpp:~231`) routes the **entire
parsed resource graph — the `Resource` object itself, its payload vectors, nested
data — into the arena**, a verbatim arena image restored at the fixed base yields
fully-valid resource objects (vtables included) with no rebasing. The arena cache is
just that image, computed once and persisted.

---

## 2. Core idea

```
First boot (or cache miss):
  preload all resources deterministically  ->  arena = [0, S_full)
  dump arena bytes + index to disk
Every later boot (cache hit):
  memcpy/mmap cached image -> arena base
  rebuild ResourceManager lookup from the index
  (no parse, no decompress)
```

After either path, the arena holds the **same** image, so every session agrees on
where each resource lives.

---

## 3. On-disk format (`resarena_cache.bin`)

Single file next to the exe (same location as `savestate_<N>.gzs`).

```
struct ArenaCacheHeader {
    char     magic[8];        // "SOHARENA"
    uint32_t formatVersion;   // bump on any layout change to this file format
    uint32_t buildHash;       // same hash used to gate .gzs files
    uint64_t archiveSetHash;  // hash of the loaded archive set (see 6)
    uint64_t arenaUsed;       // S_full: bytes of arena payload that follow the index
    uint64_t indexCount;      // number of ArenaCacheEntry records
    uint64_t indexOffset;     // file offset of the index blob
    uint64_t payloadOffset;   // file offset of the arena bytes
};

struct ArenaCacheEntry {       // one per cached resource
    uint64_t pathHash;         // CRC64 of the resource path (ResourceManager key)
    uint64_t arenaOffset;      // offset of the Resource object within the arena
    uint32_t resourceType;     // ResourceType
    uint32_t resourceVersion;  // ResourceVersion
};
```

- **Payload** = raw bytes `[0, arenaUsed)` of the arena, written verbatim.
- **Index** = `indexCount` × `ArenaCacheEntry`, used to rebuild the ResourceManager
  cache without parsing.
- The file is only loaded if `magic`, `formatVersion`, `buildHash`, and
  `archiveSetHash` all match the running process; otherwise it is regenerated.

---

## 4. Boot-time flow

A single entry point called during engine init, **after** archives are mounted but
**before** any gameplay/resource loading:

```cpp
void GzArenaCache_InitAtBoot() {
    if (GzArenaCache_TryLoad()) {   // cache hit: image + index restored
        return;
    }
    GzArenaCache_Generate();        // cache miss: full preload + write
}
```

### 4a. `GzArenaCache_TryLoad()` (cache hit)

1. Open `resarena_cache.bin`; read + validate header (magic/format/build/archiveSet).
   On any mismatch or read error → return false (fall through to generate).
2. `GzResArena_EnsureInit()`, then read `payload` directly into the arena base and
   `GzResArena_SetUsed(arenaUsed)`.
3. For each `ArenaCacheEntry`, reconstruct a `shared_ptr<IResource>` that **points at**
   `base + arenaOffset` with a **no-op deleter** (the arena never frees individually),
   and insert it into `ResourceManager::mResourceCache` keyed by the path hash.
   See §5 for the ownership detail.
4. Return true.

### 4b. `GzArenaCache_Generate()` (cache miss / first boot)

1. Enumerate **all** resource files via `ArchiveManager::ListFiles()`; **sort** the
   list (stable, by path hash or path string) for determinism.
2. **Single-threaded**, in sorted order, `LoadResource(path)` each entry. Single-
   threaded is mandatory: the thread pool would reorder allocations and break layout
   reproducibility. (The arena allocator is now mutex-guarded, but order still varies
   under concurrency.)
3. Skip entries that fail to load (non-resource files, metadata) without aborting —
   a failed load allocates nothing, so it does not perturb offsets.
4. After the pass, snapshot: write header, then the index (one entry per successfully
   cached resource, recording `GzResArena` offset of each `Resource` object), then the
   arena payload `[0, GzResArena_GetUsed())`.
5. Leave the arena populated and the ResourceManager cache as-is (already correct from
   the live loads) — no need to reload.

> **Note on scope:** §4b preloads *everything*. If boot time/RAM is unacceptable, the
> fallback is to preload only savestate-relevant types (scenes, rooms, objects,
> skeletons, animations, collision, audio seq/font/sample). That is smaller/faster but
> only sound if **no other resource type ever reaches the arena** — otherwise those
> types regain load-order variance and the cross-session bug returns for them. Start
> with preload-everything; narrow only with evidence.

---

## 5. The ownership detail (the fiddly part)

`ResourceManager::mResourceCache` maps to `shared_ptr<IResource>`. On a cache **hit**
we restore raw `Resource` objects sitting in the arena and must wrap them in
`shared_ptr` without re-running constructors and without ever calling `delete` on them.

Two viable approaches:

- **(Preferred) Aliasing/no-op-deleter shared_ptr.** Construct
  `std::shared_ptr<IResource>((IResource*)(base + offset), [](IResource*){})`. The
  object was fully constructed at generation time and persisted verbatim; the no-op
  deleter matches the arena's no-free model. The control block is a normal heap
  allocation (small, not in the arena) — acceptable, one per resource.
- **(Alternative) Snapshot control blocks too.** Allocate the `make_shared` control
  blocks inside the arena at generation time and restore them as well. More faithful
  but more invasive; only needed if something relies on `weak_ptr`/`use_count`
  semantics across the cache, which the resource system does not appear to.

Go with the no-op-deleter approach unless a concrete need for real refcounts surfaces.

`IResource` already carries the metadata the cache cares about (type/version/init
data); confirm `ResourceInitData` for a restored entry is reconstructed or stored in
the index if any consumer reads it post-load. (Add fields to `ArenaCacheEntry` if so.)

---

## 6. Cache invalidation

Regenerate whenever either changes:

- **`buildHash`** — same value already computed for `.gzs` gating. Any rebuild changes
  it, so a stale image is rejected automatically.
- **`archiveSetHash`** — hash of the mounted archive set: each archive's path + size +
  mtime, folded in **mount order**. Catches mod toggles, asset updates, OTR swaps. If
  the set differs, the deterministic layout would differ, so regenerate.

Alt-assets / mods that change which resources exist must therefore be reflected in the
archive-set hash. If alt-asset enablement can change *within* a fixed archive set,
fold that flag into the hash too.

On any validation failure the loader silently falls back to `Generate()`, so a stale or
corrupt cache is self-healing (costs one slow boot).

---

## 7. Interaction with `.gzs` savestates

Once the arena is always boot-loaded from the cache:

- **Cross-session `Load()` simplifies.** The current
  `GzClearCacheForStateLoad()` + `GzResArena_Reset()` + `memcpy(snapshot)` +
  `SetUsed(S)` dance is no longer needed to make resource pointers valid — the arena
  already holds the canonical image, identical to the saving session. The `.gzs` need
  only restore the **heaps + audio + statics** (like a same-session load).
- This means a `.gzs` **no longer needs to embed `resArenaCopy`** at all — drop it,
  shrinking state files dramatically. (Keep the build-hash + archive-set gate so a
  `.gzs` is only applied against the matching arena image.)
- Net effect: cross-session load converges onto the **same code path as same-session**,
  which already works — strongly reducing the surface area of the current bug.

> This supersedes the arena snapshot/restore described in `HANDOVER.md §3` step 2 of
> the cross-session load sequence.

---

## 8. Files to add / change

New (libultraship submodule, alongside `GzResArena.*`):
- `GzArenaCache.h` / `GzArenaCache.cpp` — header struct, `TryLoad`, `Generate`,
  `InitAtBoot`, archive-set hashing.

Changed:
- `GzResArena.{h,cpp}` — expose `GzResArena_EnsureInit()` publicly if not already, and
  a way to read raw bytes into the arena (or reuse `GetBase()` + `SetUsed`).
- `ResourceManager.{h,cpp}` — a method to bulk-insert reconstructed cache entries
  (e.g. `GzPopulateCacheFromArena(entries)`), and a hook to build the index during a
  generation pass (record each loaded resource's arena offset).
- Engine boot (libultraship `Context` init, after archive mount) — call
  `GzArenaCache_InitAtBoot()`.
- `soh/.../savestates.cpp` — drop `resArenaCopy` save/restore; cross-session `Load`
  becomes the same as same-session apart from the gate (per §7).

---

## 9. Open questions / risks

1. **Generation memory ceiling.** Preloading everything must fit in the 4 GiB reserve.
   Measure `GzResArena_GetUsed()` after a full generation pass; if it approaches the
   reserve, raise the reserve or narrow scope (§4b note).
2. **Resources loaded outside `ReadResource`.** If any resource payload is built
   without going through `ResourceLoader::LoadResource` (audit per `HANDOVER.md §5.1`),
   it will not be in the arena image and stays load-order-variant. Such paths must be
   routed or excluded.
3. **Non-determinism sources.** Anything that makes load order or content differ
   between the generation run and a normal run (locale, settings affecting which alt
   assets resolve, time-based codepaths) would desync the index from a regenerated
   image. Generation should run in a fixed, minimal configuration.
4. **Index completeness.** The index must capture every cache key a consumer might
   request, or a post-cache `LoadResource` for a missing key re-parses and lands at a
   fresh offset (harmless for that resource, but defeats the "no parsing" goal). Verify
   `ListFiles()` covers the full keyspace the game queries.
5. **Disk footprint.** Hundreds of MB to ~1 GB. Acceptable for a practice tool; note it
   in user-facing docs.

---

## 10. Suggested implementation order

1. Add the generation pass (`Generate()`) + index recording; verify a full preload
   completes, measure used bytes and boot time.
2. Add `TryLoad()` + cache-rebuild; verify a second boot is fast and the game runs
   normally off the restored image (no parsing).
3. Add invalidation (build + archive-set hash); verify regeneration on change.
4. Simplify cross-session `.gzs` `Load()` per §7; re-test the save/export/close/
   reopen/import/load cycle — corruption should be gone because layouts now match.
5. Drop `resArenaCopy` from the state file format.
