#ifndef SOH_GZ_WATCHES_H
#define SOH_GZ_WATCHES_H

#include <cstdint>
#include <string>
#include <vector>

// gz-style memory watches, adapted for ship. The GameCube practice ROM (gz) watches an
// arbitrary N64 RAM address + type and draws the value on screen. ship is a native port
// with no flat N64 address space, so instead each watch points at a curated named game
// variable (resolved through C struct pointers); the type formatting, on-screen display
// and controller repositioning mirror gz.
//
// Threading: the active watch list is mutated and read only on the game thread (the gz
// input hook). Once per frame GzWatch_UpdateSnapshot() formats the live values into a
// mutex-guarded snapshot that the GUI/draw thread renders via GzWatch_Snapshot().

// Value display types, ported from gz (watch_type).
enum GzWatchType {
    GZ_WATCH_U8,
    GZ_WATCH_S8,
    GZ_WATCH_X8,
    GZ_WATCH_U16,
    GZ_WATCH_S16,
    GZ_WATCH_X16,
    GZ_WATCH_U32,
    GZ_WATCH_S32,
    GZ_WATCH_X32,
    GZ_WATCH_F32,
    GZ_WATCH_TYPE_COUNT,
};

// --- Catalog of watchable named variables (read-only static table) ---
int         GzWatch_CatalogCount();
const char* GzWatch_CatalogName(int i);

// --- Active watch list (game thread only) ---
int  GzWatch_Count();
int  GzWatch_Max();
bool GzWatch_Add(int catalogIndex); // false if the list is full
void GzWatch_Remove(int i);
void GzWatch_CycleType(int i, int dir); // cycle same-byte-size interpretations
void GzWatch_Nudge(int i, float dx, float dy);
// Request a watch be moved to an absolute (unscaled) position. Safe to call from the
// draw thread (e.g. mouse drag); the game thread applies it on the next snapshot. Pass
// save=true on drag release to persist the new position.
void GzWatch_RequestMove(int i, float x, float y, bool save);
// Load/save the active list to a CVar so watches persist across launches.
void GzWatch_Load();

// One formatted watch for the draw thread.
struct GzWatchDisplay {
    std::string label;    // catalog name, e.g. "pos x"
    std::string value;    // formatted current value
    std::string typeName; // current type name, e.g. "f32"
    float x, y;           // on-screen position (viewport pixels, unscaled)
};

// Game thread: refresh the shared snapshot from the live game state. `playState` is the
// PlayState* (may be null mid-transition; reads are guarded).
void GzWatch_UpdateSnapshot(void* playState);
// Draw thread: a copy of the latest snapshot.
std::vector<GzWatchDisplay> GzWatch_Snapshot();

#endif
