// speedrun-style watches for ship -- see speedrun_watches.h for the design and threading notes.

#include "speedrun_watches.h"

#include <cstdio>
#include <cstring>
#include <mutex>

#include <libultraship/bridge.h>
#include <ship/Context.h>
#include <ship/config/Config.h>
#include "soh/cvar_prefixes.h"

extern "C" {
#include "z64.h"
#include "macros.h"
}

#define CVAR_SPEEDRUN_WATCHES_NAME CVAR_CHEAT("SpeedrunWatches")

// --- Catalog -----------------------------------------------------------------

// Accessor ids: how to fetch each catalog variable's live value.
enum SpeedrunWatchSource {
    SRC_POS_X,
    SRC_POS_Y,
    SRC_POS_Z,
    SRC_SPEED,
    SRC_YVEL,
    SRC_FACING,
    SRC_CAM_YAW,
    SRC_CAM_PITCH,
    SRC_FRAMES,
};

struct SpeedrunWatchCatalog {
    const char* label;
    SpeedrunWatchSource source;
    SpeedrunWatchType naturalType;
};

static const SpeedrunWatchCatalog kCatalog[] = {
    { "pos x", SRC_POS_X, SPEEDRUN_WATCH_F32 },
    { "pos y", SRC_POS_Y, SPEEDRUN_WATCH_F32 },
    { "pos z", SRC_POS_Z, SPEEDRUN_WATCH_F32 },
    { "speed", SRC_SPEED, SPEEDRUN_WATCH_F32 },
    { "y vel", SRC_YVEL, SPEEDRUN_WATCH_F32 },
    { "facing", SRC_FACING, SPEEDRUN_WATCH_S16 },
    { "cam yaw", SRC_CAM_YAW, SPEEDRUN_WATCH_S16 },
    { "cam pitch", SRC_CAM_PITCH, SPEEDRUN_WATCH_S16 },
    { "frames", SRC_FRAMES, SPEEDRUN_WATCH_U32 },
};
static const int kCatalogCount = (int)(sizeof(kCatalog) / sizeof(kCatalog[0]));

int SpeedrunWatch_CatalogCount() {
    return kCatalogCount;
}
const char* SpeedrunWatch_CatalogName(int i) {
    return (i >= 0 && i < kCatalogCount) ? kCatalog[i].label : "";
}

// --- Type metadata -----------------------------------------------------------

static const char* const kTypeName[SPEEDRUN_WATCH_TYPE_COUNT] = { "u8",  "s8",  "x8",  "u16", "s16",
                                                            "x16", "u32", "s32", "x32", "f32" };
static const int kTypeSize[SPEEDRUN_WATCH_TYPE_COUNT] = { 1, 1, 1, 2, 2, 2, 4, 4, 4, 4 };

static void FormatValue(SpeedrunWatchType type, uint64_t bits, char* out, size_t n) {
    switch (type) {
        case SPEEDRUN_WATCH_U8:
            snprintf(out, n, "%u", (unsigned)(uint8_t)bits);
            break;
        case SPEEDRUN_WATCH_S8:
            snprintf(out, n, "%d", (int)(int8_t)bits);
            break;
        case SPEEDRUN_WATCH_X8:
            snprintf(out, n, "0x%02X", (unsigned)(uint8_t)bits);
            break;
        case SPEEDRUN_WATCH_U16:
            snprintf(out, n, "%u", (unsigned)(uint16_t)bits);
            break;
        case SPEEDRUN_WATCH_S16:
            snprintf(out, n, "%d", (int)(int16_t)bits);
            break;
        case SPEEDRUN_WATCH_X16:
            snprintf(out, n, "0x%04X", (unsigned)(uint16_t)bits);
            break;
        case SPEEDRUN_WATCH_U32:
            snprintf(out, n, "%u", (unsigned)(uint32_t)bits);
            break;
        case SPEEDRUN_WATCH_S32:
            snprintf(out, n, "%d", (int)(int32_t)bits);
            break;
        case SPEEDRUN_WATCH_X32:
            snprintf(out, n, "0x%08X", (unsigned)(uint32_t)bits);
            break;
        case SPEEDRUN_WATCH_F32: {
            float f;
            uint32_t lo = (uint32_t)bits;
            memcpy(&f, &lo, sizeof(f));
            snprintf(out, n, "%.3f", f);
            break;
        }
        default:
            snprintf(out, n, "?");
            break;
    }
}

// --- Live value read (game thread) -------------------------------------------

static uint64_t FloatBits(float f) {
    uint32_t b;
    memcpy(&b, &f, sizeof(b));
    return b;
}

// Read a catalog variable's current value into `bits`. Returns false when the source is missing
// (e.g. no player mid-transition) so the value shows blank instead of dereferencing a dead pointer.
static bool ReadSource(SpeedrunWatchSource src, PlayState* play, uint64_t* bits) {
    Player* player = (play != nullptr) ? GET_PLAYER(play) : nullptr;
    switch (src) {
        case SRC_POS_X:
            if (!player) return false;
            *bits = FloatBits(player->actor.world.pos.x);
            return true;
        case SRC_POS_Y:
            if (!player) return false;
            *bits = FloatBits(player->actor.world.pos.y);
            return true;
        case SRC_POS_Z:
            if (!player) return false;
            *bits = FloatBits(player->actor.world.pos.z);
            return true;
        case SRC_SPEED:
            if (!player) return false;
            *bits = FloatBits(player->linearVelocity);
            return true;
        case SRC_YVEL:
            if (!player) return false;
            *bits = FloatBits(player->actor.velocity.y);
            return true;
        case SRC_FACING:
            if (!player) return false;
            *bits = (uint16_t)player->actor.shape.rot.y;
            return true;
        case SRC_CAM_YAW:
        case SRC_CAM_PITCH: {
            if (!play) return false;
            Camera* cam = GET_ACTIVE_CAM(play);
            if (!cam) return false;
            *bits = (uint16_t)(src == SRC_CAM_YAW ? cam->inputDir.y : cam->inputDir.x);
            return true;
        }
        case SRC_FRAMES:
            if (!play) return false;
            *bits = (uint32_t)play->gameplayFrames;
            return true;
        default:
            return false;
    }
}

// --- Active watch list (game thread) -----------------------------------------

struct SpeedrunWatch {
    int catalogIndex;
    SpeedrunWatchType type;
    float x, y;
};

static const int kMaxWatches = 18;
static std::vector<SpeedrunWatch> sWatches;

static std::mutex sSnapshotMutex;
static std::vector<SpeedrunWatchDisplay> sSnapshot;

// Pending position update from the draw thread (mouse drag), applied on the game thread
// in the next snapshot so the active list stays single-threaded.
static std::mutex sPendingMutex;
static int sPendingMoveIdx = -1;
static float sPendingMoveX = 0.0f, sPendingMoveY = 0.0f;
static bool sPendingMoveSave = false;

int SpeedrunWatch_Count() {
    return (int)sWatches.size();
}
int SpeedrunWatch_Max() {
    return kMaxWatches;
}

// Persist the active list as "cat,type,x,y;..." in a CVar.
static void SpeedrunWatch_Save() {
    std::string s;
    char buf[64];
    for (const auto& w : sWatches) {
        snprintf(buf, sizeof(buf), "%d,%d,%d,%d;", w.catalogIndex, (int)w.type, (int)w.x, (int)w.y);
        s += buf;
    }
    CVarSetString(CVAR_SPEEDRUN_WATCHES_NAME, s.c_str());
    // CVarSave() syncs the console variables into the config and flushes to disk; calling
    // GetConfig()->Save() directly would persist the config without our just-set cvar.
    CVarSave();
}

bool SpeedrunWatch_Add(int catalogIndex) {
    if (catalogIndex < 0 || catalogIndex >= kCatalogCount) {
        return false;
    }
    if ((int)sWatches.size() >= kMaxWatches) {
        return false;
    }
    SpeedrunWatch w;
    w.catalogIndex = catalogIndex;
    w.type = kCatalog[catalogIndex].naturalType;
    // Default position: stack down the upper-left corner (unscaled viewport pixels).
    w.x = 16.0f;
    w.y = 16.0f + 12.0f * (float)sWatches.size();
    sWatches.push_back(w);
    SpeedrunWatch_Save();
    return true;
}

void SpeedrunWatch_Remove(int i) {
    if (i < 0 || i >= (int)sWatches.size()) {
        return;
    }
    sWatches.erase(sWatches.begin() + i);
    SpeedrunWatch_Save();
}

// Cycle the watch's type among the interpretations matching its byte size, so reads stay
// in bounds (the one safe deviation from speedrun's any-type-on-any-address).
void SpeedrunWatch_CycleType(int i, int dir) {
    if (i < 0 || i >= (int)sWatches.size()) {
        return;
    }
    const int size = kTypeSize[sWatches[i].type];
    int t = sWatches[i].type;
    for (int step = 0; step < SPEEDRUN_WATCH_TYPE_COUNT; step++) {
        t = (t + (dir >= 0 ? 1 : SPEEDRUN_WATCH_TYPE_COUNT - 1)) % SPEEDRUN_WATCH_TYPE_COUNT;
        if (kTypeSize[t] == size) {
            sWatches[i].type = (SpeedrunWatchType)t;
            break;
        }
    }
    SpeedrunWatch_Save();
}

void SpeedrunWatch_Nudge(int i, float dx, float dy) {
    if (i < 0 || i >= (int)sWatches.size()) {
        return;
    }
    sWatches[i].x += dx;
    sWatches[i].y += dy;
    if (sWatches[i].x < 0.0f) sWatches[i].x = 0.0f;
    if (sWatches[i].y < 0.0f) sWatches[i].y = 0.0f;
    SpeedrunWatch_Save();
}

void SpeedrunWatch_RequestMove(int i, float x, float y, bool save) {
    std::lock_guard<std::mutex> lock(sPendingMutex);
    sPendingMoveIdx = i;
    sPendingMoveX = x;
    sPendingMoveY = y;
    sPendingMoveSave = sPendingMoveSave || save;
}

void SpeedrunWatch_Load() {
    sWatches.clear();
    std::string s = CVarGetString(CVAR_SPEEDRUN_WATCHES_NAME, "");
    size_t pos = 0;
    while (pos < s.size() && (int)sWatches.size() < kMaxWatches) {
        size_t end = s.find(';', pos);
        if (end == std::string::npos) {
            break;
        }
        int cat = 0, type = 0, x = 0, y = 0;
        if (sscanf(s.c_str() + pos, "%d,%d,%d,%d", &cat, &type, &x, &y) == 4 && cat >= 0 &&
            cat < kCatalogCount && type >= 0 && type < SPEEDRUN_WATCH_TYPE_COUNT) {
            SpeedrunWatch w;
            w.catalogIndex = cat;
            w.type = (SpeedrunWatchType)type;
            w.x = (float)x;
            w.y = (float)y;
            sWatches.push_back(w);
        }
        pos = end + 1;
    }
}

// --- Snapshot ----------------------------------------------------------------

void SpeedrunWatch_UpdateSnapshot(void* playState) {
    PlayState* play = (PlayState*)playState;

    // Apply any pending mouse-drag move (from the draw thread), persisting once on release.
    {
        std::lock_guard<std::mutex> lock(sPendingMutex);
        if (sPendingMoveIdx >= 0 && sPendingMoveIdx < (int)sWatches.size()) {
            sWatches[sPendingMoveIdx].x = sPendingMoveX < 0.0f ? 0.0f : sPendingMoveX;
            sWatches[sPendingMoveIdx].y = sPendingMoveY < 0.0f ? 0.0f : sPendingMoveY;
            const bool save = sPendingMoveSave;
            sPendingMoveIdx = -1;
            sPendingMoveSave = false;
            if (save) {
                SpeedrunWatch_Save();
            }
        }
    }

    std::vector<SpeedrunWatchDisplay> snap;
    snap.reserve(sWatches.size());
    for (const auto& w : sWatches) {
        SpeedrunWatchDisplay d;
        d.label = kCatalog[w.catalogIndex].label;
        d.typeName = kTypeName[w.type];
        d.x = w.x;
        d.y = w.y;
        uint64_t bits = 0;
        if (ReadSource(kCatalog[w.catalogIndex].source, play, &bits)) {
            char buf[32];
            FormatValue(w.type, bits, buf, sizeof(buf));
            d.value = buf;
        } else {
            d.value = "--";
        }
        snap.push_back(std::move(d));
    }
    std::lock_guard<std::mutex> lock(sSnapshotMutex);
    sSnapshot = std::move(snap);
}

std::vector<SpeedrunWatchDisplay> SpeedrunWatch_Snapshot() {
    std::lock_guard<std::mutex> lock(sSnapshotMutex);
    return sSnapshot;
}
