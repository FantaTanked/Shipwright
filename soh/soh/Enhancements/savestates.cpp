#include "savestates.h"

#include <soh/GameVersions.h>

#include <spdlog/spdlog.h>

#include <soh/OTRGlobals.h>
#include <soh/OTRAudio.h>

#include "z64.h"
#include "z64save.h"
#include <variables.h>
#include <functions.h>
#include "z64map_mark.h"
#include "soh/Enhancements/savestate_compress.h"
#include "../../src/overlays/actors/ovl_Boss_Ganon/z_boss_ganon.h"
#include "../../src/overlays/actors/ovl_Boss_Ganon2/z_boss_ganon2.h"
#include "../../src/overlays/actors/ovl_Boss_Tw/z_boss_tw.h"
#include "../../src/overlays/actors/ovl_En_Clear_Tag/z_en_clear_tag.h"
#include "../../src/overlays/actors/ovl_En_Fr/z_en_fr.h"

#include <libultraship/libultraship.h>

#include <cstdio>
#include <cstring>
#include <filesystem>
#include <string>
#include <vector>
#include <algorithm>
#include <utility>

extern "C" PlayState* gPlayState;
extern "C" uintptr_t gExeBase; // soh.exe image base (main.c), for cross-restart code-pointer relocation
extern "C" uint32_t gExeSize;  // soh.exe image size (main.c)

// Magic + format version for on-disk savestate files (cross-session persistence). Bump the version whenever
// the on-disk layout changes (e.g. when the relocation metadata is added) so older files are cleanly rejected.
#define SAVESTATE_DISK_MAGIC 0x53534F48u // "SSOH"
#define SAVESTATE_DISK_VERSION 9u        // v9: dropped dead duplicate SaveStateInfo fields (layout change)

// The Heap Fragmentation enhancement keeps a host-side "shadow" N64 arena outside the captured heap, so the
// savestate carries it as an opaque blob; HeapFragmentation.cpp owns the (de)serialization.
#define HEAP_FRAG_BLOB_CAP (SYSTEM_HEAP_SIZE + 0x40000u)
extern "C" uint32_t HeapFragmentation_SerializeShadow(void* dst, uint32_t dstCap);
extern "C" void HeapFragmentation_DeserializeShadow(const void* src, uint32_t size, uint8_t crossRestart);

// FROM z_lights.c
// I didn't feel like moving it into a header file.
#define LIGHTS_BUFFER_SIZE 32

typedef struct {
    /* 0x000 */ s32 numOccupied;
    /* 0x004 */ s32 searchIndex;
    /* 0x008 */ LightNode buf[LIGHTS_BUFFER_SIZE];
} LightsBuffer; // size = 0x188

#include "savestates_extern.inc"

typedef struct SaveStateInfo {
    unsigned char sysHeapCopy[SYSTEM_HEAP_SIZE];
    unsigned char audioHeapCopy[AUDIO_HEAP_SIZE];

    // Heap Fragmentation shadow-arena snapshot (opaque; produced/consumed by HeapFragmentation.cpp).
    unsigned char heapFragBlob[HEAP_FRAG_BLOB_CAP];
    uint32_t heapFragBlobSize;

    SaveContext saveContextCopy;
    GameInfo gameInfoCopy;
    LightsBuffer lightBufferCopy;
    AudioContext audioContextCopy;
    MtxF mtxStackCopy[20]; // always 20 matricies
    MtxF currentMtxCopy;
    uint32_t rngSeed;
    int16_t blueWarpTimerCopy; /* From door_warp_1 */

    SeqScriptState seqScriptStateCopy[4]; // Unrelocated
    ActiveSequence gActiveSeqsCopy[4];

    ActiveSound gActiveSoundsCopy[7][MAX_CHANNELS_PER_BANK];
    uint8_t gSoundBankMutedCopy[7];

    u8 D_801333F0_copy;
    u8 gAudioSfxSwapOff_copy;
    uint16_t gAudioSfxSwapSource_copy[10];
    uint16_t gAudioSfxSwapTarget_copy[10];
    uint8_t gAudioSfxSwapMode_copy[10];
    void (*D_801755D0_copy)(void);
    MapMarkData** sLoadedMarkDataTableCopy;

    // Static Data

    // Camera data
    int32_t sInitRegs_copy;
    int32_t gDbgCamEnabled_copy;
    int32_t sDbgModeIdx_copy;
    int16_t sNextUID_copy;
    int32_t sCameraInterfaceFlags_copy;
    int32_t sCameraInterfaceAlpha_copy;
    int32_t sCameraShrinkWindowVal_copy;
    int32_t D_8011D3AC_copy;
    int32_t sDemo5PrevAction12Frame_copy;
    int32_t sDemo5PrevSfxFrame_copy;
    int32_t D_8011D3F0_copy;
    OnePointCsFull D_8011D6AC_copy[3];
    OnePointCsFull D_8011D724_copy[3];
    OnePointCsFull D_8011D79C_copy[3];
    OnePointCsFull D_8011D83C_copy[2];
    OnePointCsFull D_8011D88C_copy[2];
    OnePointCsFull D_8011D8DC_copy[3];
    OnePointCsFull D_8011D954_copy[4];
    OnePointCsFull D_8011D9F4_copy[3];
    int16_t depthPhase_copy;
    int16_t screenPlanePhase_copy;
    int32_t sOOBTimer_copy;
    f32 D_8015CE50_copy;
    f32 D_8015CE54_copy;
    CamColChk D_8015CE58_copy;

    // Gameover
    uint16_t gGameOverTimer_copy;

    // One point demo
    uint32_t sPrevFrameCs1100_copy;
    CutsceneCameraPoint D_8012013C_copy[14];
    CutsceneCameraPoint D_8012021C_copy[14];
    CutsceneCameraPoint D_801204D4_copy[14];
    CutsceneCameraPoint D_801205B4_copy[14];
    OnePointCsFull D_801208EC_copy[3];
    OnePointCsFull D_80120964_copy[2];
    OnePointCsFull D_801209B4_copy[4];
    OnePointCsFull D_80120ACC_copy[5];
    OnePointCsFull D_80120B94_copy[11];
    OnePointCsFull D_80120D4C_copy[7];
    OnePointCsFull D_80120FA4_copy[6];
    OnePointCsFull D_80121184_copy[2];
    OnePointCsFull D_801211D4_copy[2];
    OnePointCsFull D_8012133C_copy[3];
    OnePointCsFull D_801213B4_copy[5];
    OnePointCsFull D_8012151C_copy[2];
    OnePointCsFull D_8012156C_copy[2];
    OnePointCsFull D_801215BC_copy[1];
    OnePointCsFull D_80121C24_copy[7];
    OnePointCsFull D_80121D3C_copy[3];
    OnePointCsFull D_80121F1C_copy[4];
    OnePointCsFull D_80121FBC_copy[4];
    OnePointCsFull D_801220D4_copy[5];
    OnePointCsFull D_80122714_copy[4];
    OnePointCsFull D_80122CB4_copy[2];
    OnePointCsFull D_80122D04_copy[2];
    OnePointCsFull D_80122E44_copy[2][7];
    OnePointCsFull D_8012313C_copy[3];
    OnePointCsFull D_801231B4_copy[4];
    OnePointCsFull D_80123254_copy[2];
    OnePointCsFull D_801232A4_copy[1];
    OnePointCsFull D_80123894_copy[3];
    OnePointCsFull D_8012390C_copy[2];
    OnePointCsFull D_8012395C_copy[3];
    OnePointCsFull D_801239D4_copy[3];

    uint16_t gTimeIncrement_copy;

    // Overlay static data
    //   z_bg_ddan_kd
    Vec3f sBgDdanKdVelocity_copy;
    Vec3f sBgDdanKdAccel_copy;

    // z_bg_dodoago
    s16 sBgDodoagoFirstExplosiveFlag_copy;
    u8 sBgDodoagoDisableBombCatcher_copy;
    s32 sBgDodoagoTimer_copy;

    // z_bg_haka_trap
    uint32_t D_80880F30_copy;
    uint32_t D_80881014_copy;

    // z_bg_hidan_rock
    float D_8088BFC0_copy;

    // z_bg_mori_hineri
    int16_t sBgMoriHineriNextCamIdx_copy;

    // z_bg_po_event
    uint8_t sBgPoEventBlocksAtRest_copy;
    uint8_t sBgPoEventPuzzleState_copy;
    float sBgPoEventblockPushDist_copy;

    // z_bg_relay_objects
    uint32_t D_808A9508_copy;

    // z_bg_spot18_basket
    int16_t D_808B85D0_copy;

    // z_boss_ganon
    uint32_t sBossGanonSeed1_copy;
    uint32_t sBossGanonSeed2_copy;
    uint32_t sBossGanonSeed3_copy;
    void* sBossGanonGanondorf_copy;
    void* sBossGanonZelda_copy;
    void* sBossGanonCape_copy;
    GanondorfEffect sBossGanonEffectBuf_copy[200];

    // z_boss_ganon2
    Vec3f D_8090EB20_copy;
    int8_t D_80910638_copy;
    void* sBossGanon2Zelda_copy;
    void* D_8090EB30_copy;
    int32_t sBossGanon2Seed1_copy;
    int32_t sBossGanon2Seed2_copy;
    int32_t sBossGanon2Seed3_copy;
    Vec3f D_809105D8_copy[4];
    Vec3f D_80910608_copy[4];
    BossGanon2Effect sBossGanon2Particles_copy[100];

    // z_boss_tw
    uint8_t sTwInitalized_copy;
    BossTwEffect sTwEffects_copy[150];

    // z_demo_6k
    Vec3f sDemo6kVelocity_copy;

    // z_demo_du
    int32_t D_8096CE94_copy;

    // z_demo_kekkai
    Vec3f demoKekkaiVel_copy;

    // z_en_bw
    int32_t sSlugGroup_copy;

    // z_en_clear_tag
    uint8_t sClearTagIsEffectInitialized_copy;
    EnClearTagEffect sClearTagEffects_copy[CLEAR_TAG_EFFECT_MAX_COUNT];

    // z_en_fr
    EnFrPointers sEnFrPointers_copy;

    // z_en_goma
    uint8_t sSpawnNum_copy;

    // z_en_insect
    float D_80A7DEB0_copy;
    int16_t D_80A7DEB4_copy;
    int16_t D_80A7DEB8_copy;

    // z_en_ishi
    int16_t sRockRotSpeedX_copy;
    int16_t sRockRotSpeedY_copy;

    // z_en_niw
    int16_t D_80AB85E0_copy;
    uint8_t sLowerRiverSpawned_copy;
    uint8_t sUpperRiverSpawned_copy;

    // z_en_po_field
    int32_t sEnPoFieldNumSpawned_copy;
    Vec3s sEnPoFieldSpawnPositions_copy[10];
    u8 sEnPoFieldSpawnSwitchFlags_copy[10];

    // z_en_takara_man
    uint8_t sTakaraIsInitialized_copy;

    // z_en_xc
    int32_t D_80B41D90_copy;
    int32_t sEnXcFlameSpawned_copy;
    int32_t D_80B41DA8_copy;
    int32_t D_80B41DAC_copy;

    // z_en_zf
    int16_t D_80B4A1B0_copy;
    int16_t D_80B4A1B4_copy;

    int32_t D_80B5A468_copy;
    int32_t D_80B5A494_copy;
    int32_t D_80B5A4BC_copy;

    uint8_t sKankyoIsSpawned_copy;
    int16_t sTrailingFairies_copy;

    // z_en_heishi1
    uint32_t sHeishi1PlayerIsCaughtCopy;

    // Misc static data
    //  z_map_exp

    s16 sPlayerInitialPosX_copy;
    s16 sPlayerInitialPosZ_copy;
    s16 sPlayerInitialDirection_copy;

    // code_800E(something. fill me in later)
    u8 sOcarinaInpEnabled_copy;
    s8 D_80130F10_copy;
    u8 sCurOcarinaBtnVal_copy;
    u8 sPrevOcarinaNoteVal_copy;
    u8 sCurOcarinaBtnIdx_copy;
    u8 sLearnSongLastBtn_copy;
    f32 D_80130F24_copy;
    f32 D_80130F28_copy;
    s8 D_80130F2C_copy;
    s8 D_80130F30_copy;
    s8 D_80130F34_copy;
    u8 sDisplayedNoteValue_copy;
    u8 sPlaybackState_copy;
    u32 D_80130F3C_copy;
    u32 sNotePlaybackTimer_copy;
    u16 sPlaybackNotePos_copy;
    u16 sStaffPlaybackPos_copy;

    u32 sCurOcarinaBtnPress_copy;
    u32 D_8016BA10_copy;
    u32 sPrevOcarinaBtnPress_copy;
    s32 D_8016BA18_copy;
    s32 D_8016BA1C_copy;
    u8 sCurOcarinaSong_copy[8];
    u8 sOcarinaSongAppendPos_copy;
    u8 sOcarinaHasStartedSong_copy;
    u8 sOcarinaSongNoteStartIdx_copy;
    u8 sOcarinaSongCnt_copy;
    u16 sOcarinaAvailSongs_copy;
    u8 sStaffPlayingPos_copy;
    u16 sLearnSongPos_copy[0x10];
    u16 D_8016BA50_copy[0x10];
    u16 D_8016BA70_copy[0x10];
    u8 sLearnSongExpectedNote_copy[0x10];
    OcarinaNote D_8016BAA0_copy;
    u8 sAudioHasMalonBgm_copy;
    f32 sAudioMalonBgmDist_copy;

    // Message_PAL
    s16 sOcarinaNoteBufPos_copy;
    s16 sOcarinaNoteBufLen_copy;
    u8 sOcarinaNoteBuf_copy[12];

    u8 D_8014B2F4_copy;
    u8 sTextboxSkipped_copy;
    u16 sNextTextId_copy;
    s16 sLastPlayedSong_copy;
    s16 sHasSunsSong_copy;
    s16 sMessageHasSetSfx_copy;
    u16 sOcarinaSongBitFlags_copy;

    // Transition actors (doors, loadzone/crawlspace planes) are marked spawned by negating their id in cached
    // scene memory, outside the heap snapshot. Capture the flags too, or a restore mis-skips or doubles them.
    u8 transitionActorCount_copy;
    s16 transitionActorIds_copy[256];

} SaveStateInfo;

class SaveState {
    friend class SaveStateMgr;

  public:
    SaveState(std::shared_ptr<SaveStateMgr> mgr, unsigned int slot);

  private:
    unsigned int slot;
    std::shared_ptr<SaveStateMgr> saveStateMgr;
    std::shared_ptr<SaveStateInfo> info;

    // True when this state's audio was loaded from another session: its sequence/soundfont tables are stale, so
    // every load must keep the live audio instead of restoring them. Set by ReadFromDisk, cleared by Save.
    bool audioCrossSession = false;

    void Save(void);
    // crossRestart: this load follows a quit/relaunch, so the saved audio context's resource tables are stale --
    // skip restoring audio and keep this session's live audio instead (caller re-triggers the scene's music).
    void Load(bool crossRestart = false);
    void BackupSeqScriptState(void);
    void LoadSeqScriptState(void);
    void BackupStaticData(void);
    void LoadStaticData(void);

    void SaveTransitionActors(void);
    void LoadTransitionActors(void);

    // Cross-session persistence: serialize/restore the captured `info` blob to a per-slot file. A header guards
    // magic/version/size so a stale or wrong-build file is rejected, never applied.
    bool WriteToDisk(const std::string& path = ""); // path empty => the per-slot file
    bool ReadFromDisk(const std::string& path = "");

    SaveStateInfo* GetSaveStateInfo(void);
};

SaveStateMgr::SaveStateMgr() {
    this->SetCurrentSlot(0);
}
SaveStateMgr::~SaveStateMgr() {
    this->states.clear();
}

SaveState::SaveState(std::shared_ptr<SaveStateMgr> mgr, unsigned int slot)
    : slot(slot), saveStateMgr(mgr), info(nullptr) {
    this->info = std::make_shared<SaveStateInfo>();
}

void SaveState::BackupSeqScriptState(void) {
    for (unsigned int i = 0; i < 4; i++) {
        info->seqScriptStateCopy[i].value = gAudioContext.seqPlayers[i].scriptState.value;

        info->seqScriptStateCopy[i].remLoopIters[0] = gAudioContext.seqPlayers[i].scriptState.remLoopIters[0];
        info->seqScriptStateCopy[i].remLoopIters[1] = gAudioContext.seqPlayers[i].scriptState.remLoopIters[1];
        info->seqScriptStateCopy[i].remLoopIters[2] = gAudioContext.seqPlayers[i].scriptState.remLoopIters[2];
        info->seqScriptStateCopy[i].remLoopIters[3] = gAudioContext.seqPlayers[i].scriptState.remLoopIters[3];

        info->seqScriptStateCopy[i].depth = gAudioContext.seqPlayers[i].scriptState.depth;

        info->seqScriptStateCopy[i].pc =
            (u8*)((uintptr_t)gAudioContext.seqPlayers[i].scriptState.pc - (uintptr_t)gAudioHeap);

        info->seqScriptStateCopy[i].stack[0] =
            (u8*)((uintptr_t)gAudioContext.seqPlayers[i].scriptState.stack[0] - (uintptr_t)gAudioHeap);
        info->seqScriptStateCopy[i].stack[1] =
            (u8*)((uintptr_t)gAudioContext.seqPlayers[i].scriptState.stack[1] - (uintptr_t)gAudioHeap);
        info->seqScriptStateCopy[i].stack[2] =
            (u8*)((uintptr_t)gAudioContext.seqPlayers[i].scriptState.stack[2] - (uintptr_t)gAudioHeap);
        info->seqScriptStateCopy[i].stack[3] =
            (u8*)((uintptr_t)gAudioContext.seqPlayers[i].scriptState.stack[3] - (uintptr_t)gAudioHeap);
    }
}

void SaveState::LoadSeqScriptState(void) {
    for (unsigned int i = 0; i < 4; i++) {
        gAudioContext.seqPlayers[i].scriptState.value = info->seqScriptStateCopy[i].value;

        gAudioContext.seqPlayers[i].scriptState.remLoopIters[0] = info->seqScriptStateCopy[i].remLoopIters[0];
        gAudioContext.seqPlayers[i].scriptState.remLoopIters[1] = info->seqScriptStateCopy[i].remLoopIters[1];
        gAudioContext.seqPlayers[i].scriptState.remLoopIters[2] = info->seqScriptStateCopy[i].remLoopIters[2];
        gAudioContext.seqPlayers[i].scriptState.remLoopIters[3] = info->seqScriptStateCopy[i].remLoopIters[3];

        gAudioContext.seqPlayers[i].scriptState.depth = info->seqScriptStateCopy[i].depth;

        gAudioContext.seqPlayers[i].scriptState.pc =
            (u8*)((uintptr_t)info->seqScriptStateCopy[i].pc + (uintptr_t)gAudioHeap);

        gAudioContext.seqPlayers[i].scriptState.stack[0] =
            (u8*)((uintptr_t)info->seqScriptStateCopy[i].stack[0] + (uintptr_t)gAudioHeap);
        gAudioContext.seqPlayers[i].scriptState.stack[1] =
            (u8*)((uintptr_t)info->seqScriptStateCopy[i].stack[1] + (uintptr_t)gAudioHeap);
        gAudioContext.seqPlayers[i].scriptState.stack[2] =
            (u8*)((uintptr_t)info->seqScriptStateCopy[i].stack[2] + (uintptr_t)gAudioHeap);
        gAudioContext.seqPlayers[i].scriptState.stack[3] =
            (u8*)((uintptr_t)info->seqScriptStateCopy[i].stack[3] + (uintptr_t)gAudioHeap);
    }
}

// One field table (savestate_static_fields.inc) drives both capture and restore, so they can't drift.
// SS_SCALAR = a plain assignable; SS_BLOCK = an array/struct copied by the snapshot field's size.

void SaveState::BackupStaticData(void) {
#define SS_SCALAR(m, g) info->m = g;
#define SS_BLOCK(m, g) memcpy(&info->m, &g, sizeof(info->m));
#include "savestate_static_fields.inc"
#undef SS_SCALAR
#undef SS_BLOCK
}

void SaveState::LoadStaticData(void) {
#define SS_SCALAR(m, g) g = info->m;
#define SS_BLOCK(m, g) memcpy(&g, &info->m, sizeof(info->m));
#include "savestate_static_fields.inc"
#undef SS_SCALAR
#undef SS_BLOCK
}

extern "C" void ProcessSaveStateRequests(void) {
    OTRGlobals::Instance->gSaveStateMgr->ProcessSaveStateRequests();
}

// Game-overlay toast used throughout the savestate flow; the 4-deep accessor chain is otherwise repeated at
// every call site. Every current message is a single "%u" of the slot.
static void SaveStateNotify(const char* fmt, unsigned int slot) {
    Ship::Context::GetRawInstance()->GetWindow()->GetGui()->GetGameOverlay()->TextDrawNotification(1.0f, true, fmt, slot);
}

// The savestates folder (created if missing). Shared by the per-slot path and GetStateDirectory.
static std::string SaveStateDir(void) {
    const std::string dir = Ship::Context::GetPathRelativeToAppDirectory("savestates");
    std::error_code ec;
    std::filesystem::create_directories(dir, ec);
    return dir;
}

static std::string SaveStateDiskPath(unsigned int slot) {
    return SaveStateDir() + "/slot" + std::to_string(slot) + ".st";
}

// Code-pointer relocation: after a restart, ASLR shifts soh.exe by one delta, so patch every captured pointer
// in the old image range by (newBase - oldBase). Scans at 8-byte stride; non-image pointers are left alone.
static void SaveState_RelocateExePointers(SaveStateInfo* info, uint64_t oldBase, uint32_t oldSize, uint64_t newBase) {
    if (oldBase == 0 || newBase == 0 || oldBase == newBase) {
        return;
    }
    const intptr_t delta = (intptr_t)(newBase - oldBase);
    const uintptr_t lo = (uintptr_t)oldBase;
    const uintptr_t hi = lo + oldSize;
    uintptr_t* p = reinterpret_cast<uintptr_t*>(info);
    const size_t n = sizeof(SaveStateInfo) / sizeof(uintptr_t);
    size_t count = 0;
    for (size_t i = 0; i < n; i++) {
        if (p[i] >= lo && p[i] < hi) {
            p[i] = (uintptr_t)((intptr_t)p[i] + delta);
            count++;
        }
    }
    SPDLOG_INFO("[SaveState] relocated {} EXE/code pointers (delta {:#x})", count, (uintptr_t)delta);
}

// subIndex in a SaveStateResEntry selects which block of a resource an entry covers. The two negative values are
// singleton blocks; any value >= 0 is the i-th GetSubAllocations() block.
static constexpr int32_t kResMainPayload = -1; // res->GetRawPointer()
static constexpr int32_t kResObjectPtr = -2;   // the IResource object pointer itself (null-payload res, e.g. Scene)

// Resource-pointer relocation (save side): for each loaded resource, record its main payload plus every typed
// sub-allocation it declares (subIndex i). On load these are re-resolved and captured pointers relocated.
static std::vector<SaveStateResEntry> SaveState_CollectResources(void) {
    std::vector<SaveStateResEntry> table;
    auto rm = Ship::Context::GetRawInstance()->GetResourceManager();
    if (rm == nullptr) {
        return table;
    }
    const auto loaded = rm->GetLoadedResourcePointers();
    table.reserve(loaded.size() * 2); // ~1 main payload + sub-allocations per resource; avoid repeated reallocs
    for (const auto& [path, res] : loaded) {
        if (res == nullptr) {
            continue;
        }
        if (path.size() >= sizeof(SaveStateResEntry::name)) {
            SPDLOG_WARN("[SaveState]resource path too long, skipping: '{}'", path);
            continue; // can't round-trip the name
        }
        void* mainPtr = res->GetRawPointer();
        const size_t mainSize = res->GetPointerSize();
        const auto subs = res->GetSubAllocations();
        const auto addEntry = [&](void* base, size_t size, int32_t subIndex) {
            if (base == nullptr || size == 0) {
                return;
            }
            SaveStateResEntry e = {};
            e.oldBase = (uint64_t)(uintptr_t)base;
            e.oldSize = (uint64_t)size;
            e.subIndex = subIndex;
            std::strncpy(e.name, path.c_str(), sizeof(e.name) - 1);
            table.push_back(e);
        };
        if (mainPtr != nullptr && mainSize != 0) {
            addEntry(mainPtr, mainSize, kResMainPayload);
        } else {
            // Null-payload resource (Scene): game code holds the IResource object pointer directly and
            // sig-checks its bytes, so capture it to relocate that pointer across a restart.
            addEntry((void*)res.get(), sizeof(void*), kResObjectPtr);
        }
        for (size_t i = 0; i < subs.size(); i++) {
            addEntry(subs[i].first, subs[i].second, (int32_t)i);
        }
    }
    return table;
}

namespace {
struct ResourceRelocRange {
    uintptr_t oldLo, oldHi;
    intptr_t delta;
};
} // namespace

// Resource-pointer relocation (load side): reload each saved resource by name (cached, keeping its payload
// alive) and shift every captured heap word from the old payload range to the new. Fail-closed on mismatch.
static bool SaveState_RelocateResourcePointers(SaveStateInfo* info, const std::vector<SaveStateResEntry>& table) {
    if (table.empty()) {
        return true;
    }
    auto rm = Ship::Context::GetRawInstance()->GetResourceManager();
    if (rm == nullptr) {
        SPDLOG_ERROR("[SaveState]no resource manager -- refusing load");
        return false;
    }

    std::vector<ResourceRelocRange> ranges;
    ranges.reserve(table.size());
    for (const auto& e : table) {
        if (e.oldBase == 0 || e.oldSize == 0) {
            continue;
        }
        auto res = rm->LoadResource(e.name); // reload (cached) -> keeps the payload + its sub-allocations alive
        if (res == nullptr) {
            SPDLOG_ERROR("[SaveState]resource '{}' unavailable -- refusing load", e.name);
            return false; // fail-closed
        }
        void* nb = nullptr;
        size_t ns = 0;
        if (e.subIndex == kResObjectPtr) {
            nb = (void*)res.get(); // the IResource object pointer (null-payload resource, e.g. Scene)
            ns = sizeof(void*);
        } else if (e.subIndex < 0) {
            nb = res->GetRawPointer(); // kResMainPayload
            ns = res->GetPointerSize();
        } else {
            const auto subs = res->GetSubAllocations(); // >= 0 = the i-th sub-allocation
            if ((size_t)e.subIndex < subs.size()) {
                nb = subs[e.subIndex].first;
                ns = subs[e.subIndex].second;
            }
        }
        if (nb == nullptr || ns != e.oldSize) {
            SPDLOG_ERROR("[SaveState]'{}' block sub={} mismatch (newBase={}, size {} vs saved {}) -- refusing load",
                         e.name, e.subIndex, nb, (uint64_t)ns, e.oldSize);
            return false; // fail-closed: a different asset/build, or a sub-allocation that no longer exists
        }
        const uintptr_t newBase = (uintptr_t)nb;
        const intptr_t delta = (intptr_t)(newBase - (uintptr_t)e.oldBase);
        ranges.push_back({ (uintptr_t)e.oldBase, (uintptr_t)e.oldBase + e.oldSize, delta });
    }

    // Relocate: sort by old range start, binary-search each 8-byte-aligned heap word.
    std::sort(ranges.begin(), ranges.end(), [](const ResourceRelocRange& a, const ResourceRelocRange& b) { return a.oldLo < b.oldLo; });
    uintptr_t* p = reinterpret_cast<uintptr_t*>(&info->sysHeapCopy);
    const size_t n = SYSTEM_HEAP_SIZE / sizeof(uintptr_t);

    // A heap word in the old resource band that no range covers is an unrelocated resource pointer -- a
    // coverage gap, counted as a sanity signal only. The fail-closed reload above is the real safety net.
    uintptr_t oldLoBand = UINTPTR_MAX, oldHiBand = 0;
    for (const auto& r : ranges) {
        if (r.oldLo < oldLoBand) oldLoBand = r.oldLo;
        if (r.oldHi > oldHiBand) oldHiBand = r.oldHi;
    }
    size_t count = 0, leak = 0;
    for (size_t i = 0; i < n; i++) {
        const uintptr_t w = p[i];
        size_t lo = 0, hi = ranges.size();
        while (lo < hi) {
            size_t mid = (lo + hi) / 2;
            if (ranges[mid].oldLo <= w) {
                lo = mid + 1;
            } else {
                hi = mid;
            }
        }
        if (lo > 0 && w < ranges[lo - 1].oldHi) {
            p[i] = (uintptr_t)((intptr_t)w + ranges[lo - 1].delta);
            count++;
        } else if (w >= oldLoBand && w < oldHiBand && (w & 7u) == 0) {
            leak++;
        }
    }
    SPDLOG_INFO("[SaveState] relocated {} resource pointers ({} unrelocated in-band)", count, leak);
    return true;
}

bool SaveState::WriteToDisk(const std::string& explicitPath) {
    const std::string path = explicitPath.empty() ? SaveStateDiskPath(this->slot) : explicitPath;
    std::vector<SaveStateResEntry> resTable = SaveState_CollectResources();

    // Serialize the info blob + relocation table contiguously, then zlib-compress (the body is ~half zeros).
    // The header stays uncompressed so magic/version/infoSize validate before the body is decompressed.
    const size_t infoBytes = sizeof(SaveStateInfo);
    const size_t tableBytes = resTable.size() * sizeof(SaveStateResEntry);
    std::vector<uint8_t> body(infoBytes + tableBytes);
    memcpy(body.data(), this->info.get(), infoBytes);
    if (tableBytes != 0) {
        memcpy(body.data() + infoBytes, resTable.data(), tableBytes);
    }
    std::vector<uint8_t> compressed = SaveStateCompress::Compress(body.data(), body.size());
    const bool useCompression = !compressed.empty();
    const std::vector<uint8_t>& payload = useCompression ? compressed : body;

    SaveStateHeader header = {};
    header.stateMagic = SAVESTATE_DISK_MAGIC;
    header.stateVersion = SAVESTATE_DISK_VERSION;
    header.infoSize = (uint64_t)sizeof(SaveStateInfo);
    header.exeBase = (uint64_t)gExeBase;
    header.exeSize = gExeSize;
    header.resCount = (uint32_t)resTable.size();
    header.compression = useCompression ? 1u : 0u;
    header.bodyUncompressedSize = (uint64_t)body.size();
    header.bodyCompressedSize = (uint64_t)payload.size();

    FILE* f = fopen(path.c_str(), "wb");
    if (f == nullptr) {
        SPDLOG_ERROR("[SaveState] could not open '{}' for write", path);
        return false;
    }
    const bool ok = (fwrite(&header, sizeof(header), 1, f) == 1) &&
                    (fwrite(payload.data(), 1, payload.size(), f) == payload.size());
    fclose(f);
    if (!ok) {
        SPDLOG_ERROR("[SaveState] write failed for '{}'", path);
        return false;
    }
    SPDLOG_INFO("[SaveState] wrote slot {} -> '{}' ({} -> {} bytes, {})", this->slot, path, body.size(),
                payload.size(), useCompression ? "zlib" : "raw");
    return true;
}

bool SaveState::ReadFromDisk(const std::string& explicitPath) {
    const std::string path = explicitPath.empty() ? SaveStateDiskPath(this->slot) : explicitPath;
    FILE* f = fopen(path.c_str(), "rb");
    if (f == nullptr) {
        SPDLOG_ERROR("[SaveState] no disk state at '{}'", path);
        return false;
    }
    SaveStateHeader header = {};
    bool ok = (fread(&header, sizeof(header), 1, f) == 1);
    if (ok && header.stateMagic != SAVESTATE_DISK_MAGIC) {
        SPDLOG_ERROR("[SaveState] '{}' bad magic {:#x}", path, header.stateMagic);
        ok = false;
    }
    if (ok && header.stateVersion != SAVESTATE_DISK_VERSION) {
        SPDLOG_ERROR("[SaveState] '{}' format version {} != {}", path, header.stateVersion, SAVESTATE_DISK_VERSION);
        ok = false;
    }
    if (ok && header.infoSize != (uint64_t)sizeof(SaveStateInfo)) {
        SPDLOG_ERROR("[SaveState] '{}' size {} != {} (wrong build)", path, header.infoSize, (uint64_t)sizeof(SaveStateInfo));
        ok = false;
    }
    std::vector<SaveStateResEntry> resTable;
    if (ok) {
        // Read the (compressed) body, decompress it, and split it back into the info blob + relocation table.
        const size_t infoBytes = sizeof(SaveStateInfo);
        const size_t tableBytes = (size_t)header.resCount * sizeof(SaveStateResEntry);
        const size_t bodyBytes = infoBytes + tableBytes;
        if (header.bodyUncompressedSize != (uint64_t)bodyBytes) {
            SPDLOG_ERROR("[SaveState] '{}' body size {} != expected {}", path, header.bodyUncompressedSize, bodyBytes);
            ok = false;
        }

        std::vector<uint8_t> payload;
        if (ok) {
            payload.resize((size_t)header.bodyCompressedSize);
            ok = (fread(payload.data(), 1, payload.size(), f) == payload.size());
            if (!ok) {
                SPDLOG_ERROR("[SaveState] '{}' truncated body", path);
            }
        }

        std::vector<uint8_t> body;
        if (ok) {
            if (header.compression == 1u) {
                body.resize(bodyBytes);
                if (!SaveStateCompress::Decompress(payload.data(), payload.size(), body.data(), body.size())) {
                    SPDLOG_ERROR("[SaveState] '{}' decompression failed", path);
                    ok = false;
                }
            } else if (payload.size() == bodyBytes) {
                body = std::move(payload); // stored raw
            } else {
                SPDLOG_ERROR("[SaveState] '{}' raw body size mismatch", path);
                ok = false;
            }
        }

        if (ok) {
            memcpy(this->info.get(), body.data(), infoBytes);
            if (tableBytes != 0) {
                resTable.resize(header.resCount);
                memcpy(resTable.data(), body.data() + infoBytes, tableBytes);
            }
        }
    }
    fclose(f);
    if (ok) {
        // Code-pointer relocation: patch pointers into the soh.exe image for this run's ASLR before applying.
        SaveState_RelocateExePointers(this->info.get(), header.exeBase, header.exeSize, (uint64_t)gExeBase);
        // Resource-pointer relocation: reload + relocate. Fail-closed -- if a resource can't be reloaded at its saved
        // size, abort the load rather than apply a heap with dangling resource pointers.
        if (!SaveState_RelocateResourcePointers(this->info.get(), resTable)) {
            SPDLOG_ERROR("[SaveState] '{}' resource relocation failed -- not applying", path);
            return false;
        }
        SPDLOG_INFO("[SaveState] read slot {} <- '{}'", this->slot, path);
    }
    if (ok) {
        // This blob's audio belongs to the session that wrote the file -- mark it so every load keeps the live
        // audio rather than restoring the stale tables (see audioCrossSession).
        audioCrossSession = true;
    }
    return ok;
}

void SaveStateMgr::SetCurrentSlot(unsigned int slot) {
    SaveStateNotify("slot %u set", slot);
    this->currentSlot = slot;
}

unsigned int SaveStateMgr::GetCurrentSlot(void) {
    return this->currentSlot;
}

// The folder savestate files live in (also creates it). Used by the practice menu's export/import dialogs.
std::string SaveStateMgr::GetStateDirectory(void) {
    return SaveStateDir();
}

// Export: capture the live state and serialize it to `path` (reuses the SAVE_TO_DISK pipeline).
SaveStateReturn SaveStateMgr::ExportState(unsigned int slot, const std::string& path) {
    if (path.empty()) {
        return SaveStateReturn::FAIL_BAD_REQUEST;
    }
    return AddRequest({ slot, RequestType::SAVE_TO_DISK, path });
}

// Import: read `path`, relocate its pointers for this run, and apply it (reuses the LOAD_FROM_DISK pipeline).
SaveStateReturn SaveStateMgr::ImportState(unsigned int slot, const std::string& path) {
    if (path.empty()) {
        return SaveStateReturn::FAIL_BAD_REQUEST;
    }
    return AddRequest({ slot, RequestType::LOAD_FROM_DISK, path });
}

// After a cross-restart load we keep this session's live audio. The savestate restored the destination scene's
// BGM in play->sequenceCtx; blank gSaveContext's "now playing" markers so the game switches to the right track.
static void PlayDestinationSceneAudio(void) {
    if (gPlayState == nullptr) {
        return;
    }
    constexpr uint8_t kNoSequencePlaying = 0xFF;
    gSaveContext.seqId = kNoSequencePlaying;
    gSaveContext.natureAmbienceId = kNoSequencePlaying;
    Environment_PlaySceneSequence(gPlayState);
}

// Return the state in `slot`, lazily creating an empty one if it doesn't exist yet.
std::shared_ptr<SaveState>& SaveStateMgr::EnsureSlot(unsigned int slot) {
    auto& state = this->states[slot];
    if (state == nullptr) {
        state = std::make_shared<SaveState>(OTRGlobals::Instance->gSaveStateMgr, slot);
    }
    return state;
}

void SaveStateMgr::ProcessSaveStateRequests(void) {
    while (!this->requests.empty()) {
        const auto& request = this->requests.front();

        switch (request.type) {
            case RequestType::SAVE:
                EnsureSlot(request.slot)->Save();
                SaveStateNotify("saved state %u", request.slot);
                break;
            case RequestType::LOAD:
                if (this->states.contains(request.slot)) {
                    const auto& state = this->states[request.slot];
                    // An imported state's saved audio is stale this session, so load it as a cross-restart
                    // (keep live audio + re-trigger BGM). A fresh in-session state loads normally.
                    state->Load(state->audioCrossSession);
                    SaveStateNotify("loaded state %u", request.slot);
                } else {
                    SPDLOG_ERROR("Invalid SaveState slot: {}", request.slot);
                }
                break;
            case RequestType::SAVE_TO_DISK: {
                auto& state = EnsureSlot(request.slot);
                state->Save(); // capture live state into info, then serialize it
                SaveStateNotify(state->WriteToDisk(request.path) ? "saved state %u to disk" : "disk save %u FAILED",
                                request.slot);
                break;
            }
            case RequestType::LOAD_FROM_DISK: {
                auto& state = EnsureSlot(request.slot);
                if (state->ReadFromDisk(request.path)) {
                    // ReadFromDisk set audioCrossSession; Load keeps the live audio and re-triggers the BGM.
                    state->Load(state->audioCrossSession);
                    SaveStateNotify("loaded state %u from disk", request.slot);
                } else {
                    SaveStateNotify("disk load %u FAILED", request.slot);
                }
                break;
            }
            [[unlikely]] default:
                SPDLOG_ERROR("Invalid SaveState request type: Unknown ({})", static_cast<int>(request.type));
                break;
        }
        this->requests.pop();
    }
}

SaveStateReturn SaveStateMgr::AddRequest(const SaveStateRequest request) {
    if (gPlayState == nullptr) {
        SPDLOG_ERROR("[SOH] Can not save or load a state outside of \"GamePlay\"");
        SaveStateNotify("states not available here", request.slot);
        return SaveStateReturn::FAIL_WRONG_GAMESTATE;
    }

    switch (request.type) {
        case RequestType::SAVE:
        case RequestType::SAVE_TO_DISK:
        case RequestType::LOAD_FROM_DISK:
            // SAVE/SAVE_TO_DISK always enqueue; LOAD_FROM_DISK is allowed even when the slot isn't in memory
            // (it loads the state from the file).
            requests.push(request);
            return SaveStateReturn::SUCCESS;
        case RequestType::LOAD:
            // An in-memory load needs the slot to already exist.
            if (states.contains(request.slot)) {
                requests.push(request);
                return SaveStateReturn::SUCCESS;
            }
            SPDLOG_ERROR("Invalid SaveState slot: {}", request.slot);
            SaveStateNotify("state slot %u empty", request.slot);
            return SaveStateReturn::FAIL_INVALID_SLOT;
        [[unlikely]] default:
            SPDLOG_ERROR("Invalid SaveState request type: Unknown ({})", static_cast<int>(request.type));
            return SaveStateReturn::FAIL_BAD_REQUEST;
    }
}

// Transition actors are marked spawned by negating their id in cached scene memory, outside the heap snapshot.
// Capture/restore the flags so a load doesn't wrongly skip or double a door/loadzone/crawlspace actor.
void SaveState::SaveTransitionActors(void) {
    info->transitionActorCount_copy = 0;
    if (gPlayState == nullptr || gPlayState->transiActorCtx.list == nullptr) {
        return;
    }
    const u32 cap = (u32)(sizeof(info->transitionActorIds_copy) / sizeof(info->transitionActorIds_copy[0]));
    u32 numActors = gPlayState->transiActorCtx.numActors;
    if (numActors > cap) {
        numActors = cap;
    }
    info->transitionActorCount_copy = (u8)numActors;
    for (u32 i = 0; i < numActors; i++) {
        info->transitionActorIds_copy[i] = gPlayState->transiActorCtx.list[i].id;
    }
}

void SaveState::LoadTransitionActors(void) {
    if (gPlayState == nullptr || gPlayState->transiActorCtx.list == nullptr) {
        return;
    }
    u32 numActors = info->transitionActorCount_copy;
    if (numActors > gPlayState->transiActorCtx.numActors) {
        numActors = gPlayState->transiActorCtx.numActors;
    }
    for (u32 i = 0; i < numActors; i++) {
        gPlayState->transiActorCtx.list[i].id = info->transitionActorIds_copy[i];
    }
}

void SaveState::Save(void) {
    std::unique_lock<std::mutex> Lock(audio.mutex);
    memcpy(&info->sysHeapCopy, gSystemHeap, SYSTEM_HEAP_SIZE);
    memcpy(&info->audioHeapCopy, gAudioHeap, AUDIO_HEAP_SIZE);
    // Snapshot the Heap Fragmentation shadow arena (returns 0 if the enhancement is off).
    info->heapFragBlobSize = HeapFragmentation_SerializeShadow(&info->heapFragBlob, (uint32_t)sizeof(info->heapFragBlob));

    memcpy(&info->audioContextCopy, &gAudioContext, sizeof(AudioContext));
    memcpy(&info->gActiveSeqsCopy, gActiveSeqs, sizeof(info->gActiveSeqsCopy));
    BackupSeqScriptState();

    memcpy(info->gActiveSoundsCopy, gActiveSounds, sizeof(gActiveSounds));
    memcpy(&info->gSoundBankMutedCopy, gSoundBankMuted, sizeof(info->gSoundBankMutedCopy));

    info->D_801333F0_copy = D_801333F0;
    info->gAudioSfxSwapOff_copy = gAudioSfxSwapOff;

    memcpy(&info->gAudioSfxSwapSource_copy, gAudioSfxSwapSource, sizeof(info->gAudioSfxSwapSource_copy));
    memcpy(&info->gAudioSfxSwapTarget_copy, gAudioSfxSwapTarget, sizeof(info->gAudioSfxSwapTarget_copy));
    memcpy(&info->gAudioSfxSwapMode_copy, gAudioSfxSwapMode, sizeof(info->gAudioSfxSwapMode_copy));

    info->D_801755D0_copy = D_801755D0;

    memcpy(&info->saveContextCopy, &gSaveContext, sizeof(gSaveContext));
    memcpy(&info->gameInfoCopy, gGameInfo, sizeof(*gGameInfo));
    memcpy(&info->lightBufferCopy, &sLightsBuffer, sizeof(sLightsBuffer));
    memcpy(&info->mtxStackCopy, sMatrixStack, sizeof(MtxF) * 20);
    memcpy(&info->currentMtxCopy, sCurrentMatrix, sizeof(MtxF));

    // Various static data
    info->blueWarpTimerCopy = sWarpTimerTarget;
    BackupStaticData();
    SaveTransitionActors();
    // Fresh capture: this audio context is valid this session, so future loads may restore it.
    audioCrossSession = false;
}

void SaveState::Load(bool crossRestart) {
    std::unique_lock<std::mutex> Lock(audio.mutex);
    memcpy(gSystemHeap, &info->sysHeapCopy, SYSTEM_HEAP_SIZE);
    // Restore the Heap Fragmentation shadow arena (handles the crossRestart-vs-pinned gating internally).
    HeapFragmentation_DeserializeShadow(&info->heapFragBlob, info->heapFragBlobSize, crossRestart ? 1 : 0);
    if (!crossRestart) {
        // In-session load: restore the saved audio (its pointers are still valid this session).
        memcpy(gAudioHeap, &info->audioHeapCopy, AUDIO_HEAP_SIZE);
        memcpy(&gAudioContext, &info->audioContextCopy, sizeof(AudioContext));
        memcpy(gActiveSeqs, &info->gActiveSeqsCopy, sizeof(info->gActiveSeqsCopy));
        LoadSeqScriptState();
    }
    // else cross-restart: keep this session's live audio (the saved audio's resource tables are stale).

    memcpy(&gSaveContext, &info->saveContextCopy, sizeof(gSaveContext));
    memcpy(gGameInfo, &info->gameInfoCopy, sizeof(*gGameInfo));
    memcpy(&sLightsBuffer, &info->lightBufferCopy, sizeof(sLightsBuffer));
    memcpy(sMatrixStack, &info->mtxStackCopy, sizeof(MtxF) * 20);
    memcpy(sCurrentMatrix, &info->currentMtxCopy, sizeof(MtxF));
    sWarpTimerTarget = info->blueWarpTimerCopy;

    memcpy(gActiveSounds, info->gActiveSoundsCopy, sizeof(gActiveSounds));
    memcpy(gSoundBankMuted, &info->gSoundBankMutedCopy, sizeof(info->gSoundBankMutedCopy));
    D_801333F0 = info->D_801333F0_copy;
    gAudioSfxSwapOff = info->gAudioSfxSwapOff_copy;

    memcpy(gAudioSfxSwapSource, &info->gAudioSfxSwapSource_copy, sizeof(info->gAudioSfxSwapSource_copy));
    memcpy(gAudioSfxSwapTarget, &info->gAudioSfxSwapTarget_copy, sizeof(info->gAudioSfxSwapTarget_copy));
    memcpy(gAudioSfxSwapMode, &info->gAudioSfxSwapMode_copy, sizeof(info->gAudioSfxSwapMode_copy));

    // Various static data
    D_801755D0 = info->D_801755D0_copy;
    LoadStaticData();
    // The heap restore brings back a valid transiActorCtx.list (relocated to the reloaded scene if
    // cross-session); only the id signs need re-syncing.
    LoadTransitionActors();

    if (crossRestart) {
        // Re-trigger the destination scene's BGM (we kept this session's live audio). Release the audio lock
        // first -- Environment_PlaySceneSequence must not run under it.
        Lock.unlock();
        PlayDestinationSceneAudio();
    }
}
