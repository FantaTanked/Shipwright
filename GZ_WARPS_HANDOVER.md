# gz Warps Menu — Handover (category/place/entrance redesign)

_Branch: `develop-gz` • Written: 2026-06-16 • Status: investigation + plan, NOT implemented_

Pick-up-cold notes for reworking the gz menu's **warps** page into gz's hierarchical
**category → place → entrance** layout. The current flat list works but is (1) messy to
navigate (~50 scenes in one scrolling list) and (2) collapses multi-entrance scenes to
their first entrance, so e.g. "Kokiri Buildings" always warps to the first building and
you can't pick a specific building or a boss room.

---

## 1. Goal

Mirror gz: top-level **categories** — Dungeons, Bosses, Towns, Houses, Shops, Misc,
Overworld — each opening a list of **places**, each place opening its **entrances**
("Entrance", "From <boss>", "<Boss>'s Lair", etc.). C-Down descends a level / warps;
`return` ascends. We already have all the entrance data; only the category grouping is new.

---

## 2. What exists today (the current warps page)

All gz-menu code is in **`soh/soh/Enhancements/gz/gz_practice.cpp`** (a single TU,
registered via `RegisterShipInitFunc`, gated by `gGzMode` CVar, driven by the
`OnGameStateMainStart` hook). Key pieces:

- **Screens** (`enum GzScreen`): `GZ_SCREEN_ROOT`, `GZ_SCREEN_MACRO`, `GZ_SCREEN_IMPORT`,
  `GZ_SCREEN_WARPS`. Root is the gz top menu (return/warps/scene/cheats/.../macro/...);
  only `warps`, `macro`, `return` are enabled, the rest are greyed stubs.
- **Warps screen state**: `sWarpSel` (atomic int, row cursor), `sWarpMutex` + `sWarpNames`
  (snapshot of names for the draw thread). Row 0 is `return`; rows 1..N are destinations.
- **Entry/confirm**: `GzEnterWarpsScreen()` snapshots `GetDefaultWarpNames()`;
  `GzConfirmWarpsSelection()` maps `sel-1` → `WarpToDefaultIndex(idx)` then closes the menu.
- **Drawing**: `GzMenuOverlay::DrawList(...)` draws the gz-style overlay (Press Start 2P
  font via `GameOverlay::TextDraw`, left edge ~22% down, blue selection, scroll window
  with `^`/`v` indicators). The overlay window is hidden unless the menu is open.
- **Input**: D-pad navigates (only the D-pad is captured; the game keeps running), C-Down
  confirms, R+C-Down closes. No B.

The warp data comes from **`soh/soh/Enhancements/Warping.{h,cpp}`**:
- `GetDefaultWarpNames()` → one string per scene (currently `SceneSelect_GetSceneName`).
- `WarpToDefaultIndex(i)` → `WarpToEntrance(SceneSelect_GetSceneEntrance(i))`.
- `WarpToEntrance(s32 entranceIndex)` → plain entrance warp (sets
  `gPlayState->nextEntranceIndex` + `transitionTrigger = TRANS_TRIGGER_START` +
  `TRANS_TYPE_FADE_BLACK`). Spawns at the entrance's default position.

The scene/entrance data itself is **`sBetterScenes[]` in
`soh/src/overlays/gamestates/ovl_select/z_select.c`** (the debug "Map Select" table). We
added read-only accessors there:
- `s32 SceneSelect_GetSceneCount(void)`
- `const char* SceneSelect_GetSceneName(s32 index)` — strips the leading `" N:"`.
- `s32 SceneSelect_GetSceneEntrance(s32 index)` — returns `entrancePairs[0].entranceIndex`
  **← this is the "collapses to first entrance" bug source.**

---

## 3. The data we already have (this is the win)

`sBetterScenes` is a curated, fully-named list — `BetterSceneSelectEntry` (struct in
`soh/include/z64.h:1352`):

```c
typedef struct {
    char* japaneseName; char* englishName; char* germanName; char* frenchName;
    void (*loadFunc)(struct SelectContext*, s32);
    u8 entranceCount;
    BetterSceneSelectEntrancePair entrancePairs[18]; // {jp,en,de,fr, s32 entranceIndex, u8 canBeMQ}
} BetterSceneSelectEntry;
```

So each scene already carries N named entrances with real `ENTR_*` indices, **including
boss rooms**. Examples from `z_select.c`:

- **32 Deku Tree** → "Entrance", "From Gohma's Lair", **"Gohma's Lair"** (`ENTR_DEKU_TREE_BOSS_ENTRANCE`)
- **39 Spirit Temple** → "Entrance", "From Left Hand", "From Right Hand", "Before Twinrova",
  "Nabooru Fight", **"Twinrova's Lair"**
- **3 Kokiri Buildings** → "Links Bed", "Kokiri Shop", "Twins House", "Know-It-All Brothers
  House", "Midos House", "Sarias House"  ← the "buildings should be multiple choice" case

The full scene list (englishName, with `" N:"` prefix stripped) — 50 entries:

| # | Scene | suggested category |
|---|-------|--------------------|
| 1 | Hyrule Field | Overworld |
| 2 | Kokiri Forest | Overworld |
| 3 | Kokiri Buildings | Houses |
| 4 | Lost Woods | Overworld |
| 5 | Sacred Forest Meadow | Overworld |
| 6 | Castle Town Entrance | Overworld |
| 7 | Market | Towns |
| 8 | Castle Town Alley | Towns |
| 9 | Castle Town Buildings | Houses |
| 10 | Temple of Time | Misc |
| 11 | Hyrule Castle | Overworld |
| 12 | Hyrule Castle Courtyard | Overworld |
| 13 | Lon Lon Ranch | Overworld |
| 14 | Lon Lon Ranch Buildings | Houses |
| 15 | Kakariko Village | Towns |
| 16 | Kakariko Buildings | Houses |
| 17 | Graveyard | Overworld |
| 18 | Graves | Misc |
| 19 | Death Mountain Trail | Overworld |
| 20 | Goron City | Towns |
| 21 | Death Mountain Crater | Overworld |
| 22 | Zora River | Overworld |
| 23 | Zoras Domain | Towns |
| 24 | Zoras Fountain | Overworld |
| 25 | Lake Hylia | Overworld |
| 26 | Lake Hylia Buildings | Houses |
| 27 | Gerudo Valley | Overworld |
| 28 | Gerudo Fortress | Overworld |
| 29 | Thieves Hideout | Misc |
| 30 | Haunted Wasteland | Overworld |
| 31 | Desert Colossus | Overworld |
| 32–43 | Deku Tree … Gerudo Training Ground | **Dungeons** (+ boss rooms → **Bosses**) |
| 44 | Warps (ocarina pads) | Misc |
| 45 | Shops | Shops |
| 46 | Great Fairies | Misc |
| 47 | Chest Grottos | Misc (grotto loader, see §6) |
| 48 | Scrub Grottos | Misc (grotto loader) |
| 49 | Other Grottos | Misc (grotto loader) |
| 50 | Debug (Use with caution) | exclude (or Misc/Debug) |

Note SoH already pre-grouped some gz-like buckets (44 Warps, 45 Shops, 46 Great Fairies,
47–49 Grottos), which map cleanly onto the Misc/Shops categories.

---

## 4. Proposed design

Three nav levels in the gz menu:

```
warps  ->  [Dungeons]      ->  [Forest Temple]   ->  [Entrance]
           [Bosses]            [Fire Temple]         [Crushing Room]
           [Towns]             ...                   [Before Phantom Ganon]
           [Houses]                                  [Phantom Ganon's Lair]
           [Shops]
           [Overworld]
           [Misc]
           return                  return                 return
```

- **Category screen**: fixed list (Dungeons, Bosses, Towns, Houses, Shops, Overworld, Misc).
- **Place screen**: the scenes mapped to that category (reuse `englishName`).
- **Entrance screen**: that scene's `entrancePairs[]` names; C-Down warps to
  `entrancePairs[i].entranceIndex`.
- **Bosses** is special: a flat list pulling the boss-room entrance out of each dungeon
  (skip the place level) — see §6.

---

## 5. Implementation plan

**a. New entrance-level accessors in `z_select.c`** (alongside the existing ones):
```c
s32         SceneSelect_GetEntranceCount(s32 scene);
const char* SceneSelect_GetEntranceName(s32 scene, s32 entrance);   // entrancePairs[e].englishName
s32         SceneSelect_GetEntranceIndexAt(s32 scene, s32 entrance); // entrancePairs[e].entranceIndex
```
Declare them `extern "C"` in `Warping.cpp` next to the current three. (Keep
`SceneSelect_GetSceneEntrance` for any other caller, or migrate it.)

**b. Category mapping.** Add a table mapping category → list of scene indices (use the §3
table). Cleanest home: a small data block in `Warping.cpp` (or a new
`gz/gz_warp_data.cpp`) exposed as string APIs so `gz_practice.cpp` stays game-type-free:
```c
int          GzWarp_CategoryCount();
const char*  GzWarp_CategoryName(int cat);
int          GzWarp_PlaceCount(int cat);
const char*  GzWarp_PlaceName(int cat, int place);
int          GzWarp_EntranceCount(int cat, int place);
const char*  GzWarp_EntranceName(int cat, int place, int entrance);
bool         GzWarp_Do(int cat, int place, int entrance); // resolves to entranceIndex + warps
```
This keeps all the mapping + `sBetterScenes` coupling in one place; the menu only deals
with (cat, place, entrance) ints and strings.

**c. gz menu (`gz_practice.cpp`).** Add screens `GZ_SCREEN_WARP_CAT`,
`GZ_SCREEN_WARP_PLACE`, `GZ_SCREEN_WARP_ENTRANCE` (replacing `GZ_SCREEN_WARPS`), plus
cursor state per level (`sWarpCat`, `sWarpPlace`, `sWarpEntrance`) and the selected
cat/place to descend. `DrawList` already supports long scrolling lists, so reuse it.
C-Down descends (or warps at the leaf + closes menu); `return` row ascends one level.

**d. Bosses category.** Two options: (i) curate an explicit `{dungeonScene, bossEntrance}`
list, or (ii) at build-time scan each dungeon's `entrancePairs` for the one whose
`entranceIndex` is an `ENTR_*_BOSS_ENTRANCE`. Option (i) is more reliable; the boss
entrances are visible in `z_select.c` lines ~667–718 (Gohma, King Dodongo, Barinade,
Phantom Ganon, Volvagia, Morpha, Bongo Bongo, Twinrova, Ganondorf/Ganon).

---

## 6. Gotchas

- **Grottos use a different loader.** Scenes 47–49 use `Select_Grotto_LoadGame`, and their
  "entranceIndex" column is a **grotto index (0x00–0x1B)**, not an `ENTR_*`. Plain
  `WarpToEntrance` will NOT work for them — they need the grotto respawn setup (see
  `Select_Grotto_LoadGame` in `z_select.c`: it sets `respawn[RESPAWN_MODE_DOWN]` +
  `respawn[RESPAWN_MODE_RETURN]` from `sBetterGrottos[]`). Either special-case grottos in
  `GzWarp_Do` (mirror `Select_Grotto_LoadGame`'s body without the SelectContext), or omit
  grottos from the first pass and add them later.
- **`Select_LoadGame` needs a `SelectContext`** we don't have in-game — that's why we warp
  via `WarpToEntrance` (nextEntranceIndex transition) instead of the table's `loadFunc`.
  Keep doing that for normal entrances.
- **Age/time.** gz lets you pick age; warping an entrance keeps current age. Some
  destinations only make sense at a specific age (e.g. adult temples). Out of scope for the
  first pass, but a likely follow-up (`gSaveContext.linkAge`).
- **Debug scene (50)** loads test maps that can crash — exclude from categories.
- **`sBetterScenes` is static** in `z_select.c`; only reach it through accessors (don't try
  to expose the array/struct to C++).

---

## 7. File map

- `soh/soh/Enhancements/gz/gz_practice.cpp` — the gz menu (screens, nav, draw).
- `soh/soh/Enhancements/Warping.{h,cpp}` — warp execution + (planned) category/place/
  entrance string API.
- `soh/src/overlays/gamestates/ovl_select/z_select.c` — `sBetterScenes` data +
  `SceneSelect_*` accessors (additive; no change to debug-menu behavior).
- `soh/include/z64.h:1343-1360` — `BetterSceneSelectEntry` / `BetterSceneSelectEntrancePair`.

## 8. Build

`"C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe" --build build/x64 --target soh --config Debug`
(reconfigure with the same cmake on `build/x64` if files are added — soh sources are
GLOB'd). Current `x64/Debug/soh.exe` builds clean; the flat warps list is functional.
