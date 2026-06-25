#ifndef SOH_SPEEDRUN_WATCHES_H
#define SOH_SPEEDRUN_WATCHES_H

#include <cstdint>
#include <string>
#include <vector>

// On-screen memory watches. With no flat N64 address space, each watch points at a curated named
// game variable; the game thread builds a mutex-guarded snapshot each frame that the draw thread reads.

// Value display types, ported from speedrun (watch_type).
enum SpeedrunWatchType {
    SPEEDRUN_WATCH_U8,
    SPEEDRUN_WATCH_S8,
    SPEEDRUN_WATCH_X8,
    SPEEDRUN_WATCH_U16,
    SPEEDRUN_WATCH_S16,
    SPEEDRUN_WATCH_X16,
    SPEEDRUN_WATCH_U32,
    SPEEDRUN_WATCH_S32,
    SPEEDRUN_WATCH_X32,
    SPEEDRUN_WATCH_F32,
    SPEEDRUN_WATCH_TYPE_COUNT,
};

// --- Catalog of watchable named variables (read-only static table) ---
int         SpeedrunWatch_CatalogCount();
const char* SpeedrunWatch_CatalogName(int i);

// --- Active watch list (game thread only) ---
int  SpeedrunWatch_Count();
int  SpeedrunWatch_Max();
bool SpeedrunWatch_Add(int catalogIndex); // false if the list is full
void SpeedrunWatch_Remove(int i);
void SpeedrunWatch_CycleType(int i, int dir); // cycle same-byte-size interpretations
void SpeedrunWatch_Nudge(int i, float dx, float dy);
// Move a watch to an absolute (unscaled) position; safe to call from the draw thread (e.g. mouse drag),
// applied by the game thread on the next snapshot. Pass save=true on drag release to persist it.
void SpeedrunWatch_RequestMove(int i, float x, float y, bool save);
// Load/save the active list to a CVar so watches persist across launches.
void SpeedrunWatch_Load();

// One formatted watch for the draw thread.
struct SpeedrunWatchDisplay {
    std::string label;    // catalog name, e.g. "pos x"
    std::string value;    // formatted current value
    std::string typeName; // current type name, e.g. "f32"
    float x, y;           // on-screen position (viewport pixels, unscaled)
};

// Game thread: refresh the shared snapshot from the live game state. `playState` is the
// PlayState* (may be null mid-transition; reads are guarded).
void SpeedrunWatch_UpdateSnapshot(void* playState);
// Draw thread: a copy of the latest snapshot.
std::vector<SpeedrunWatchDisplay> SpeedrunWatch_Snapshot();

#endif
