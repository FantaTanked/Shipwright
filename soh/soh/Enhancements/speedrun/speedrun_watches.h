#ifndef SOH_SPEEDRUN_WATCHES_H
#define SOH_SPEEDRUN_WATCHES_H

#include <cstdint>
#include <string>
#include <vector>

// speedrun-style memory watches, adapted for ship. The GameCube practice ROM (speedrun) watches an
// arbitrary N64 RAM address + type and draws the value on screen. ship is a native port
// with no flat N64 address space, so instead each watch points at a curated named game
// variable (resolved through C struct pointers); the type formatting, on-screen display
// and controller repositioning mirror speedrun.
//
// Threading: the active watch list is mutated and read only on the game thread (the speedrun
// input hook). Once per frame SpeedrunWatch_UpdateSnapshot() formats the live values into a
// mutex-guarded snapshot that the GUI/draw thread renders via SpeedrunWatch_Snapshot().

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
// Request a watch be moved to an absolute (unscaled) position. Safe to call from the
// draw thread (e.g. mouse drag); the game thread applies it on the next snapshot. Pass
// save=true on drag release to persist the new position.
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
