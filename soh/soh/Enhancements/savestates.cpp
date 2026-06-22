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
#define SAVESTATE_DISK_VERSION 5u        // v5: resource table = main + typed sub-allocations (GetSubAllocations)

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

    // z_bg_menkuri_eye
    int32_t D_8089C1A0_copy;

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

    // z_boss_ganon
    uint32_t sBossGanonSeed1;
    uint32_t sBossGanonSeed2;
    uint32_t sBossGanonSeed3;
    void* sBossGanonGanondorf;
    void* sBossGanonZelda;
    void* sBossGanonCape;
    GanondorfEffect sBossGanonEffectBuf[200];

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

} SaveStateInfo;

class SaveState {
    friend class SaveStateMgr;

  public:
    SaveState(std::shared_ptr<SaveStateMgr> mgr, unsigned int slot);

  private:
    unsigned int slot;
    std::shared_ptr<SaveStateMgr> saveStateMgr;
    std::shared_ptr<SaveStateInfo> info;

    void Save(void);
    // crossRestart: this load follows a quit/relaunch, so the saved audio context's resource tables are stale --
    // skip restoring audio and keep this session's live audio instead (caller re-triggers the scene's music).
    void Load(bool crossRestart = false);
    void BackupSeqScriptState(void);
    void LoadSeqScriptState(void);
    void BackupCameraData(void);
    void LoadCameraData(void);
    void SaveOnePointDemoData(void);
    void LoadOnePointDemoData(void);
    void SaveOverlayStaticData(void);
    void LoadOverlayStaticData(void);

    void SaveMiscCodeData(void);
    void LoadMiscCodeData(void);

    // Cross-session persistence: serialize/restore the captured `info` blob to a per-slot file. POD blob, so
    // a single fwrite/fread is valid; a header guards magic/version/size so a stale or wrong-build file is
    // rejected, never applied.
    bool WriteToDisk(void);
    bool ReadFromDisk(void);

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

void SaveState::BackupCameraData(void) {
    info->sInitRegs_copy = sInitRegs;
    info->gDbgCamEnabled_copy = gDbgCamEnabled;
    info->sNextUID_copy = sNextUID;
    info->sCameraInterfaceFlags_copy = sCameraInterfaceFlags;
    info->sCameraInterfaceAlpha_copy = sCameraInterfaceAlpha;
    info->sCameraShrinkWindowVal_copy = sCameraShrinkWindowVal;
    info->D_8011D3AC_copy = D_8011D3AC;
    info->sDemo5PrevAction12Frame_copy = sDemo5PrevAction12Frame;
    info->sDemo5PrevSfxFrame_copy = sDemo5PrevSfxFrame;
    info->D_8011D3F0_copy = D_8011D3F0;
    memcpy(info->D_8011D6AC_copy, D_8011D6AC, sizeof(info->D_8011D6AC_copy));
    memcpy(info->D_8011D724_copy, D_8011D724, sizeof(info->D_8011D724_copy));
    memcpy(info->D_8011D79C_copy, D_8011D79C, sizeof(info->D_8011D79C_copy));
    memcpy(info->D_8011D83C_copy, D_8011D83C, sizeof(info->D_8011D83C_copy));
    memcpy(info->D_8011D88C_copy, D_8011D88C, sizeof(info->D_8011D88C_copy));
    memcpy(info->D_8011D8DC_copy, D_8011D8DC, sizeof(info->D_8011D8DC_copy));
    memcpy(info->D_8011D954_copy, D_8011D954, sizeof(info->D_8011D954_copy));
    memcpy(info->D_8011D9F4_copy, D_8011D9F4, sizeof(info->D_8011D9F4_copy));
    info->depthPhase_copy = depthPhase;
    info->screenPlanePhase_copy = screenPlanePhase;
    info->sOOBTimer_copy = sOOBTimer;
    info->D_8015CE50_copy = D_8015CE50;
    info->D_8015CE54_copy = D_8015CE54;
    memcpy(&info->D_8015CE58_copy, &D_8015CE58, sizeof(info->D_8015CE58_copy));
}

void SaveState::LoadCameraData(void) {
    sInitRegs = info->sInitRegs_copy;
    gDbgCamEnabled = info->gDbgCamEnabled_copy;
    sDbgModeIdx = info->sDbgModeIdx_copy;
    sNextUID = info->sNextUID_copy;
    sCameraInterfaceAlpha = info->sCameraInterfaceAlpha_copy;
    sCameraInterfaceFlags = info->sCameraInterfaceFlags_copy;
    sCameraShrinkWindowVal = info->sCameraShrinkWindowVal_copy;
    D_8011D3AC = info->D_8011D3AC_copy;
    sDemo5PrevAction12Frame = info->sDemo5PrevAction12Frame_copy;
    sDemo5PrevSfxFrame = info->sDemo5PrevSfxFrame_copy;
    D_8011D3F0 = info->D_8011D3F0_copy;
    memcpy(D_8011D6AC, info->D_8011D6AC_copy, sizeof(info->D_8011D6AC_copy));
    memcpy(D_8011D724, info->D_8011D724_copy, sizeof(info->D_8011D724_copy));
    memcpy(D_8011D79C, info->D_8011D79C_copy, sizeof(info->D_8011D79C_copy));
    memcpy(D_8011D83C, info->D_8011D83C_copy, sizeof(info->D_8011D83C_copy));
    memcpy(D_8011D88C, info->D_8011D88C_copy, sizeof(info->D_8011D88C_copy));
    memcpy(D_8011D8DC, info->D_8011D8DC_copy, sizeof(info->D_8011D8DC_copy));
    memcpy(D_8011D954, info->D_8011D954_copy, sizeof(info->D_8011D954_copy));
    memcpy(D_8011D9F4, info->D_8011D9F4_copy, sizeof(info->D_8011D9F4_copy));
    depthPhase = info->depthPhase_copy;
    screenPlanePhase = info->screenPlanePhase_copy;
    sOOBTimer = info->sOOBTimer_copy;
    D_8015CE50 = info->D_8015CE50_copy;
    D_8015CE54 = info->D_8015CE54_copy;
    memcpy(&D_8015CE58, &info->D_8015CE58_copy, sizeof(info->D_8015CE58_copy));
}

void SaveState::SaveOnePointDemoData(void) {
    info->sPrevFrameCs1100_copy = sPrevFrameCs1100;
    memcpy(info->D_8012013C_copy, D_8012013C, sizeof(info->D_8012013C_copy));
    memcpy(info->D_8012021C_copy, D_8012021C, sizeof(info->D_8012021C_copy));
    memcpy(info->D_801204D4_copy, D_801204D4, sizeof(info->D_801204D4_copy));
    memcpy(info->D_801205B4_copy, D_801205B4, sizeof(info->D_801205B4_copy));
    memcpy(info->D_801208EC_copy, D_801208EC, sizeof(info->D_801208EC_copy));
    memcpy(info->D_80120964_copy, D_80120964, sizeof(info->D_80120964_copy));
    memcpy(info->D_801209B4_copy, D_801209B4, sizeof(info->D_801209B4_copy));
    memcpy(info->D_80120ACC_copy, D_80120ACC, sizeof(info->D_80120ACC_copy));
    memcpy(info->D_80120B94_copy, D_80120B94, sizeof(info->D_80120B94_copy));
    memcpy(info->D_80120D4C_copy, D_80120D4C, sizeof(info->D_80120D4C_copy));
    memcpy(info->D_80120FA4_copy, D_80120FA4, sizeof(info->D_80120FA4_copy));
    memcpy(info->D_80121184_copy, D_80121184, sizeof(info->D_80121184_copy));
    memcpy(info->D_801211D4_copy, D_801211D4, sizeof(info->D_801211D4_copy));
    memcpy(info->D_8012133C_copy, D_8012133C, sizeof(info->D_8012133C_copy));
    memcpy(info->D_801213B4_copy, D_801213B4, sizeof(info->D_801213B4_copy));
    memcpy(info->D_8012151C_copy, D_8012151C, sizeof(info->D_8012151C_copy));
    memcpy(info->D_8012156C_copy, D_8012156C, sizeof(info->D_8012156C_copy));
    memcpy(info->D_801215BC_copy, D_801215BC, sizeof(info->D_801215BC_copy));
    memcpy(info->D_80121C24_copy, D_80121C24, sizeof(info->D_80121C24_copy));
    memcpy(info->D_80121D3C_copy, D_80121D3C, sizeof(info->D_80121D3C_copy));
    memcpy(info->D_80121F1C_copy, D_80121F1C, sizeof(info->D_80121F1C_copy));
    memcpy(info->D_80121FBC_copy, D_80121FBC, sizeof(info->D_80121FBC_copy));
    memcpy(info->D_801220D4_copy, D_801220D4, sizeof(info->D_801220D4_copy));
    memcpy(info->D_80122714_copy, D_80122714, sizeof(info->D_80122714_copy));
    memcpy(info->D_80122CB4_copy, D_80122CB4, sizeof(info->D_80122CB4_copy));
    memcpy(info->D_80122D04_copy, D_80122D04, sizeof(info->D_80122D04_copy));
    memcpy(info->D_80122E44_copy, D_80122E44, sizeof(info->D_80122E44_copy));
    memcpy(info->D_8012313C_copy, D_8012313C, sizeof(info->D_8012313C_copy));
    memcpy(info->D_801231B4_copy, D_801231B4, sizeof(info->D_801231B4_copy));
    memcpy(info->D_80123254_copy, D_80123254, sizeof(info->D_80123254_copy));
    memcpy(info->D_801232A4_copy, D_801232A4, sizeof(info->D_801232A4_copy));
    memcpy(info->D_80123894_copy, D_80123894, sizeof(info->D_80123894_copy));
    memcpy(info->D_8012390C_copy, D_8012390C, sizeof(info->D_8012390C_copy));
    memcpy(info->D_8012395C_copy, D_8012395C, sizeof(info->D_8012395C_copy));
    memcpy(info->D_801239D4_copy, D_801239D4, sizeof(info->D_801239D4_copy));
}

void SaveState::LoadOnePointDemoData(void) {
    sPrevFrameCs1100 = info->sPrevFrameCs1100_copy;
    memcpy(D_8012013C, info->D_8012013C_copy, sizeof(info->D_8012013C_copy));
    memcpy(D_8012021C, info->D_8012021C_copy, sizeof(info->D_8012021C_copy));
    memcpy(D_801204D4, info->D_801204D4_copy, sizeof(info->D_801204D4_copy));
    memcpy(D_801205B4, info->D_801205B4_copy, sizeof(info->D_801205B4_copy));
    memcpy(D_801208EC, info->D_801208EC_copy, sizeof(info->D_801208EC_copy));
    memcpy(D_80120964, info->D_80120964_copy, sizeof(info->D_80120964_copy));
    memcpy(D_801209B4, info->D_801209B4_copy, sizeof(info->D_801209B4_copy));
    memcpy(D_80120ACC, info->D_80120ACC_copy, sizeof(info->D_80120ACC_copy));
    memcpy(D_80120B94, info->D_80120B94_copy, sizeof(info->D_80120B94_copy));
    memcpy(D_80120D4C, info->D_80120D4C_copy, sizeof(info->D_80120D4C_copy));
    memcpy(D_80120FA4, info->D_80120FA4_copy, sizeof(info->D_80120FA4_copy));
    memcpy(D_80121184, info->D_80121184_copy, sizeof(info->D_80121184_copy));
    memcpy(D_801211D4, info->D_801211D4_copy, sizeof(info->D_801211D4_copy));
    memcpy(D_8012133C, info->D_8012133C_copy, sizeof(info->D_8012133C_copy));
    memcpy(D_801213B4, info->D_801213B4_copy, sizeof(info->D_801213B4_copy));
    memcpy(D_8012151C, info->D_8012151C_copy, sizeof(info->D_8012151C_copy));
    memcpy(D_8012156C, info->D_8012156C_copy, sizeof(info->D_8012156C_copy));
    memcpy(D_801215BC, info->D_801215BC_copy, sizeof(info->D_801215BC_copy));
    memcpy(D_80121C24, info->D_80121C24_copy, sizeof(info->D_80121C24_copy));
    memcpy(D_80121D3C, info->D_80121D3C_copy, sizeof(info->D_80121D3C_copy));
    memcpy(D_80121F1C, info->D_80121F1C_copy, sizeof(info->D_80121F1C_copy));
    memcpy(D_80121FBC, info->D_80121FBC_copy, sizeof(info->D_80121FBC_copy));
    memcpy(D_801220D4, info->D_801220D4_copy, sizeof(info->D_801220D4_copy));
    memcpy(D_80122714, info->D_80122714_copy, sizeof(info->D_80122714_copy));
    memcpy(D_80122CB4, info->D_80122CB4_copy, sizeof(info->D_80122CB4_copy));
    memcpy(D_80122D04, info->D_80122D04_copy, sizeof(info->D_80122D04_copy));
    memcpy(D_80122E44, info->D_80122E44_copy, sizeof(info->D_80122E44_copy));
    memcpy(D_8012313C, info->D_8012313C_copy, sizeof(info->D_8012313C_copy));
    memcpy(D_801231B4, info->D_801231B4_copy, sizeof(info->D_801231B4_copy));
    memcpy(D_80123254, info->D_80123254_copy, sizeof(info->D_80123254_copy));
    memcpy(D_801232A4, info->D_801232A4_copy, sizeof(info->D_801232A4_copy));
    memcpy(D_80123894, info->D_80123894_copy, sizeof(info->D_80123894_copy));
    memcpy(D_8012390C, info->D_8012390C_copy, sizeof(info->D_8012390C_copy));
    memcpy(D_8012395C, info->D_8012395C_copy, sizeof(info->D_8012395C_copy));
    memcpy(D_801239D4, info->D_801239D4_copy, sizeof(info->D_801239D4_copy));
}

void SaveState::SaveOverlayStaticData(void) {
    info->sBgDdanKdVelocity_copy = sBgDdanKdVelocity;
    info->sBgDdanKdAccel_copy = sBgDdanKdAccel;
    info->sBgDodoagoFirstExplosiveFlag_copy = sBgDodoagoFirstExplosiveFlag;
    info->sBgDodoagoDisableBombCatcher_copy = sBgDodoagoDisableBombCatcher;
    info->sBgDodoagoTimer_copy = sBgDodoagoTimer;
    info->D_80880F30_copy = D_80880F30;
    info->D_80881014_copy = D_80881014;
    info->D_8088BFC0_copy = D_8088BFC0;
    info->sBgMoriHineriNextCamIdx_copy = sBgMoriHineriNextCamIdx;
    info->sBgPoEventBlocksAtRest_copy = sBgPoEventBlocksAtRest;
    info->sBgPoEventPuzzleState_copy = sBgPoEventPuzzleState;
    info->sBgPoEventblockPushDist_copy = sBgPoEventblockPushDist;
    info->D_808A9508_copy = D_808A9508;
    info->D_808B85D0_copy = D_808B85D0;
    info->sBossGanonSeed1_copy = sBossGanonSeed1;
    info->sBossGanonSeed2_copy = sBossGanonSeed2;
    info->sBossGanonSeed3_copy = sBossGanonSeed3;
    info->sBossGanonGanondorf_copy = sBossGanonGanondorf;
    info->sBossGanonZelda_copy = sBossGanonZelda;
    info->sBossGanonCape_copy = sBossGanonCape;
    memcpy(info->sBossGanonEffectBuf_copy, sBossGanonEffectBuf, sizeof(info->sBossGanonEffectBuf_copy));
    info->D_8090EB20_copy = D_8090EB20;
    info->D_80910638_copy = D_80910638;
    info->sBossGanon2Zelda_copy = sBossGanon2Zelda;
    info->D_8090EB30_copy = D_8090EB30;
    info->sBossGanon2Seed1_copy = sBossGanon2Seed1;
    info->sBossGanon2Seed2_copy = sBossGanon2Seed2;
    info->sBossGanon2Seed3_copy = sBossGanon2Seed3;
    memcpy(info->D_809105D8_copy, D_809105D8, sizeof(D_809105D8));
    memcpy(info->D_80910608_copy, D_80910608, sizeof(D_80910608));
    memcpy(info->sBossGanon2Particles_copy, sBossGanon2Particles, sizeof(sBossGanon2Particles));
    info->sTwInitalized_copy = sTwInitalized;
    memcpy(info->sTwEffects_copy, sTwEffects, sizeof(sTwEffects));
    info->sDemo6kVelocity_copy = sDemo6kVelocity;
    info->D_8096CE94_copy = D_8096CE94;
    info->demoKekkaiVel_copy = demoKekkaiVel;
    info->sSlugGroup_copy = sSlugGroup;
    info->sClearTagIsEffectInitialized_copy = sClearTagIsEffectsInitialized;
    memcpy(info->sClearTagEffects_copy, sClearTagEffects, sizeof(sClearTagEffects));

    memcpy(&info->sEnFrPointers_copy, &sEnFrPointers, sizeof(info->sEnFrPointers_copy));
    info->sSpawnNum_copy = sSpawnNum;

    info->D_80A7DEB0_copy = D_80A7DEB0;
    info->D_80A7DEB4_copy = D_80A7DEB4;
    info->D_80A7DEB8_copy = D_80A7DEB8;
    info->sRockRotSpeedX_copy = sRockRotSpeedX;
    info->sRockRotSpeedY_copy = sRockRotSpeedY;
    info->D_80AB85E0_copy = D_80AB85E0;
    info->sLowerRiverSpawned_copy = sLowerRiverSpawned;
    info->sUpperRiverSpawned_copy = sUpperRiverSpawned;
    info->sEnPoFieldNumSpawned_copy = sEnPoFieldNumSpawned;
    memcpy(info->sEnPoFieldSpawnPositions_copy, sEnPoFieldSpawnPositions, sizeof(info->sEnPoFieldSpawnPositions_copy));
    memcpy(info->sEnPoFieldSpawnSwitchFlags_copy, sEnPoFieldSpawnSwitchFlags,
           sizeof(info->sEnPoFieldSpawnSwitchFlags_copy));

    info->sTakaraIsInitialized_copy = sTakaraIsInitialized;
    info->D_80B41D90_copy = D_80B41D90;
    info->sEnXcFlameSpawned_copy = sEnXcFlameSpawned;
    info->D_80B41DA8_copy = D_80B41DA8;
    info->D_80B41DAC_copy = D_80B41DAC;
    info->D_80B4A1B0_copy = D_80B4A1B0;
    info->D_80B4A1B4_copy = D_80B4A1B4;
    info->D_80B5A468_copy = D_80B5A468;
    info->D_80B5A494_copy = D_80B5A494;
    info->D_80B5A4BC_copy = D_80B5A4BC;
    info->sKankyoIsSpawned_copy = sKankyoIsSpawned;
    info->sTrailingFairies_copy = sTrailingFairies;

    info->sHeishi1PlayerIsCaughtCopy = sHeishi1PlayerIsCaught;
}

void SaveState::LoadOverlayStaticData(void) {
    sBgDdanKdVelocity = info->sBgDdanKdVelocity_copy;
    sBgDdanKdAccel = info->sBgDdanKdAccel_copy;
    sBgDodoagoFirstExplosiveFlag = info->sBgDodoagoFirstExplosiveFlag_copy;
    sBgDodoagoDisableBombCatcher = info->sBgDodoagoDisableBombCatcher_copy;
    sBgDodoagoTimer = info->sBgDodoagoTimer_copy;
    D_80880F30 = info->D_80880F30_copy;
    D_80881014 = info->D_80881014_copy;
    D_8088BFC0 = info->D_8088BFC0_copy;
    sBgMoriHineriNextCamIdx = info->sBgMoriHineriNextCamIdx_copy;
    sBgPoEventBlocksAtRest = info->sBgPoEventBlocksAtRest_copy;
    sBgPoEventPuzzleState = info->sBgPoEventPuzzleState_copy;
    sBgPoEventblockPushDist = info->sBgPoEventblockPushDist_copy;
    D_808A9508 = info->D_808A9508_copy;
    D_808B85D0 = info->D_808B85D0_copy;
    sBossGanonSeed1 = info->sBossGanonSeed1_copy;
    sBossGanonSeed2 = info->sBossGanonSeed2_copy;
    sBossGanonSeed3 = info->sBossGanonSeed3_copy;
    sBossGanonGanondorf = info->sBossGanonGanondorf_copy;
    sBossGanonZelda = info->sBossGanonZelda_copy;
    sBossGanonCape = info->sBossGanonCape_copy;
    memcpy(sBossGanonEffectBuf, info->sBossGanonEffectBuf_copy, sizeof(info->sBossGanonEffectBuf_copy));

    D_8090EB20 = info->D_8090EB20_copy;
    D_80910638 = info->D_80910638_copy;
    sBossGanon2Zelda = info->sBossGanon2Zelda_copy;
    D_8090EB30 = info->D_8090EB30_copy;
    sBossGanon2Seed1 = info->sBossGanon2Seed1_copy;
    sBossGanon2Seed2 = info->sBossGanon2Seed2_copy;
    sBossGanon2Seed3 = info->sBossGanon2Seed3_copy;
    memcpy(D_809105D8, info->D_809105D8_copy, sizeof(D_809105D8));
    memcpy(D_80910608, info->D_80910608_copy, sizeof(D_80910608));
    memcpy(sBossGanon2Particles, info->sBossGanon2Particles_copy, sizeof(sBossGanon2Particles));
    sTwInitalized = info->sTwInitalized_copy;
    memcpy(sTwEffects, info->sTwEffects_copy, sizeof(sTwEffects));
    sDemo6kVelocity = info->sDemo6kVelocity_copy;

    D_8096CE94 = info->D_8096CE94_copy;
    demoKekkaiVel = info->demoKekkaiVel_copy;
    sSlugGroup = info->sSlugGroup_copy;
    sClearTagIsEffectsInitialized = info->sClearTagIsEffectInitialized_copy;
    memcpy(sClearTagEffects, info->sClearTagEffects_copy, sizeof(sClearTagEffects));

    D_80A7DEB0 = info->D_80A7DEB0_copy;
    D_80A7DEB4 = info->D_80A7DEB4_copy;
    D_80A7DEB8 = info->D_80A7DEB8_copy;
    sRockRotSpeedX = info->sRockRotSpeedX_copy;
    sRockRotSpeedY = info->sRockRotSpeedY_copy;
    D_80AB85E0 = info->D_80AB85E0_copy;
    sLowerRiverSpawned = info->sLowerRiverSpawned_copy;
    sUpperRiverSpawned = info->sUpperRiverSpawned_copy;
    sEnPoFieldNumSpawned = info->sEnPoFieldNumSpawned_copy;
    memcpy(sEnPoFieldSpawnPositions, info->sEnPoFieldSpawnPositions_copy, sizeof(info->sEnPoFieldSpawnPositions_copy));
    memcpy(sEnPoFieldSpawnSwitchFlags, info->sEnPoFieldSpawnSwitchFlags_copy,
           sizeof(info->sEnPoFieldSpawnSwitchFlags_copy));

    sTakaraIsInitialized = info->sTakaraIsInitialized_copy;
    D_80B41D90 = info->D_80B41D90_copy;
    sEnXcFlameSpawned = info->sEnXcFlameSpawned_copy;
    D_80B41DA8 = info->D_80B41DA8_copy;
    D_80B41DAC = info->D_80B41DAC_copy;
    D_80B4A1B0 = info->D_80B4A1B0_copy;
    D_80B4A1B4 = info->D_80B4A1B4_copy;
    D_80B5A468 = info->D_80B5A468_copy;
    D_80B5A494 = info->D_80B5A494_copy;
    D_80B5A4BC = info->D_80B5A4BC_copy;
    sKankyoIsSpawned = info->sKankyoIsSpawned_copy;
    sTrailingFairies = info->sTrailingFairies_copy;

    sHeishi1PlayerIsCaught = info->sHeishi1PlayerIsCaughtCopy;
}

void SaveState::SaveMiscCodeData(void) {
    info->gGameOverTimer_copy = gGameOverTimer;
    info->gTimeIncrement_copy = gTimeIncrement;
    info->sLoadedMarkDataTableCopy = sLoadedMarkDataTable;

    info->sPlayerInitialPosX_copy = sPlayerInitialPosX;
    info->sPlayerInitialPosZ_copy = sPlayerInitialPosZ;
    info->sPlayerInitialDirection_copy = sPlayerInitialDirection;

    info->sOcarinaInpEnabled_copy = sOcarinaInpEnabled;
    info->D_80130F10_copy = D_80130F10;
    info->sCurOcarinaBtnVal_copy = sCurOcarinaBtnVal;
    info->sPrevOcarinaNoteVal_copy = sPrevOcarinaNoteVal;
    info->sCurOcarinaBtnIdx_copy = sCurOcarinaBtnIdx;
    info->sLearnSongLastBtn_copy = sLearnSongLastBtn;
    info->D_80130F24_copy = D_80130F24;
    info->D_80130F28_copy = D_80130F28;
    info->D_80130F2C_copy = D_80130F2C;
    info->D_80130F30_copy = D_80130F30;
    info->D_80130F34_copy = D_80130F34;
    info->sPlaybackState_copy = sPlaybackState;
    info->D_80130F3C_copy = D_80130F3C;
    info->sNotePlaybackTimer_copy = sNotePlaybackTimer;
    info->sPlaybackNotePos_copy = sPlaybackNotePos;
    info->sStaffPlaybackPos_copy = sStaffPlaybackPos;

    info->sCurOcarinaBtnPress_copy = sCurOcarinaBtnPress;
    info->D_8016BA10_copy = D_8016BA10;
    info->sPrevOcarinaBtnPress_copy = sPrevOcarinaBtnPress;
    info->D_8016BA18_copy = D_8016BA18;
    info->D_8016BA1C_copy = D_8016BA1C;
    memcpy(info->sCurOcarinaSong_copy, sCurOcarinaSong, sizeof(sCurOcarinaSong));
    info->sOcarinaSongAppendPos_copy = sOcarinaSongAppendPos;
    info->sOcarinaHasStartedSong_copy = sOcarinaHasStartedSong;
    info->sOcarinaSongNoteStartIdx_copy = sOcarinaSongNoteStartIdx;
    info->sOcarinaSongCnt_copy = sOcarinaSongCnt;
    info->sOcarinaAvailSongs_copy = sOcarinaAvailSongs;
    info->sStaffPlayingPos_copy = sStaffPlayingPos;
    memcpy(info->sLearnSongPos_copy, sLearnSongPos, sizeof(sLearnSongPos));
    memcpy(info->D_8016BA50_copy, D_8016BA50, sizeof(D_8016BA50));
    memcpy(info->D_8016BA70_copy, D_8016BA70, sizeof(D_8016BA70));
    memcpy(info->sLearnSongExpectedNote_copy, sLearnSongExpectedNote, sizeof(sLearnSongExpectedNote));
    memcpy(&info->D_8016BAA0_copy, &D_8016BAA0, sizeof(D_8016BAA0));
    info->sAudioHasMalonBgm_copy = sAudioHasMalonBgm;
    info->sAudioMalonBgmDist_copy = sAudioMalonBgmDist;
    info->sDisplayedNoteValue_copy = sDisplayedNoteValue;

    info->sOcarinaNoteBufPos_copy = sOcarinaNoteBufPos;
    info->sOcarinaNoteBufLen_copy = sOcarinaNoteBufLen;
    memcpy(info->sOcarinaNoteBuf_copy, sOcarinaNoteBuf, sizeof(sOcarinaNoteBuf));
    info->D_8014B2F4_copy = D_8014B2F4;
    info->sTextboxSkipped_copy = sTextboxSkipped;
    info->sNextTextId_copy = sNextTextId;
    info->sLastPlayedSong_copy = sLastPlayedSong;
    info->sHasSunsSong_copy = sHasSunsSong;
    info->sMessageHasSetSfx_copy = sMessageHasSetSfx;
    info->sOcarinaSongBitFlags_copy = sOcarinaSongBitFlags;
}

void SaveState::LoadMiscCodeData(void) {
    gGameOverTimer = info->gGameOverTimer_copy;
    gTimeIncrement = info->gTimeIncrement_copy;
    sLoadedMarkDataTable = info->sLoadedMarkDataTableCopy;

    sPlayerInitialPosX = info->sPlayerInitialPosX_copy;
    sPlayerInitialPosZ = info->sPlayerInitialPosZ_copy;
    sPlayerInitialDirection = info->sPlayerInitialDirection_copy;

    sOcarinaInpEnabled = info->sOcarinaInpEnabled_copy;
    D_80130F10 = info->D_80130F10_copy;
    sCurOcarinaBtnVal = info->sCurOcarinaBtnVal_copy;
    sPrevOcarinaNoteVal = info->sPrevOcarinaNoteVal_copy;
    sCurOcarinaBtnIdx = info->sCurOcarinaBtnIdx_copy;
    sLearnSongLastBtn = info->sLearnSongLastBtn_copy;
    D_80130F24 = info->D_80130F24_copy;
    D_80130F28 = info->D_80130F28_copy;
    D_80130F2C = info->D_80130F2C_copy;
    D_80130F30 = info->D_80130F30_copy;
    D_80130F34 = info->D_80130F34_copy;
    sPlaybackState = info->sPlaybackState_copy;
    D_80130F3C = info->D_80130F3C_copy;
    sNotePlaybackTimer = info->sNotePlaybackTimer_copy;
    sPlaybackNotePos = info->sPlaybackNotePos_copy;
    sStaffPlaybackPos = info->sStaffPlaybackPos_copy;

    sCurOcarinaBtnPress = info->sCurOcarinaBtnPress_copy;
    D_8016BA10 = info->D_8016BA10_copy;
    sPrevOcarinaBtnPress = info->sPrevOcarinaBtnPress_copy;
    D_8016BA18 = info->D_8016BA18_copy;
    D_8016BA1C = info->D_8016BA1C_copy;
    memcpy(sCurOcarinaSong, info->sCurOcarinaSong_copy, sizeof(sCurOcarinaSong));
    sOcarinaSongAppendPos = info->sOcarinaSongAppendPos_copy;
    sOcarinaHasStartedSong = info->sOcarinaHasStartedSong_copy;
    sOcarinaSongNoteStartIdx = info->sOcarinaSongNoteStartIdx_copy;
    sOcarinaSongCnt = info->sOcarinaSongCnt_copy;
    sOcarinaAvailSongs = info->sOcarinaAvailSongs_copy;
    sStaffPlayingPos = info->sStaffPlayingPos_copy;
    memcpy(info->sLearnSongPos_copy, info->sLearnSongPos_copy, sizeof(sLearnSongPos));
    memcpy(info->D_8016BA50_copy, info->D_8016BA50_copy, sizeof(D_8016BA50));
    memcpy(info->D_8016BA70_copy, info->D_8016BA70_copy, sizeof(D_8016BA70));
    memcpy(info->sLearnSongExpectedNote_copy, info->sLearnSongExpectedNote_copy, sizeof(sLearnSongExpectedNote));
    memcpy(&D_8016BAA0, &info->D_8016BAA0_copy, sizeof(D_8016BAA0));
    sAudioHasMalonBgm = info->sAudioHasMalonBgm_copy;
    sAudioMalonBgmDist = info->sAudioMalonBgmDist_copy;
    sDisplayedNoteValue = info->sDisplayedNoteValue_copy;

    sOcarinaNoteBufPos = info->sOcarinaNoteBufPos_copy;
    sOcarinaNoteBufLen = info->sOcarinaNoteBufLen_copy;
    memcpy(sOcarinaNoteBuf, info->sOcarinaNoteBuf_copy, sizeof(sOcarinaNoteBuf));

    D_8014B2F4 = info->D_8014B2F4_copy;
    sTextboxSkipped = info->sTextboxSkipped_copy;
    sNextTextId = info->sNextTextId_copy;
    sLastPlayedSong = info->sLastPlayedSong_copy;
    sHasSunsSong = info->sHasSunsSong_copy;
    sMessageHasSetSfx = info->sMessageHasSetSfx_copy;
    sOcarinaSongBitFlags = info->sOcarinaSongBitFlags_copy;
}

extern "C" void ProcessSaveStateRequests(void) {
    OTRGlobals::Instance->gSaveStateMgr->ProcessSaveStateRequests();
}

static std::string SaveStateDiskPath(unsigned int slot) {
    std::string dir = Ship::Context::GetPathRelativeToAppDirectory("savestates");
    std::error_code ec;
    std::filesystem::create_directories(dir, ec);
    return dir + "/slot" + std::to_string(slot) + ".savestate";
}

// B1: relocate code/function pointers across a restart. ASLR shifts the whole soh.exe image by one delta, so
// every captured pointer that fell inside the save-time image range is patched by (newBase - oldBase). Scans
// the whole info blob at 8-byte stride (x64 pointers are aligned); audio/heap pointers fall outside the EXE
// range and are untouched. A no-op when the image didn't move (in-session load => delta 0).
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

// B2 save side: for every loaded resource, record its main payload plus each typed sub-allocation
// it declares (subIndex i, from GetSubAllocations -- e.g. a skeleton's limb array). On load these blocks are
// re-resolved from the reloaded resource and every captured heap pointer aimed at them is relocated.
static std::vector<SaveStateResEntry> SaveState_CollectResources(void) {
    std::vector<SaveStateResEntry> table;
    auto rm = Ship::Context::GetRawInstance()->GetResourceManager();
    if (rm == nullptr) {
        return table;
    }
    for (const auto& [path, res] : rm->GetLoadedResourcePointers()) {
        if (res == nullptr) {
            continue;
        }
        if (path.size() >= sizeof(SaveStateResEntry::name)) {
            SPDLOG_WARN("[SaveState][B2] resource path too long, skipping: '{}'", path);
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
            // Null-payload resource (Scene): game code holds the IResource OBJECT pointer directly
            // (play->sceneSegment, roomCtx.curRoom.segment) and even sig-checks its bytes -- capture it so
            // that pointer relocates across a restart.
            addEntry((void*)res.get(), sizeof(void*), kResObjectPtr);
        }
        for (size_t i = 0; i < subs.size(); i++) {
            addEntry(subs[i].first, subs[i].second, (int32_t)i);
        }
    }
    return table;
}

namespace {
struct B2Range {
    uintptr_t oldLo, oldHi;
    intptr_t delta;
};
} // namespace

// B2 load side: relocate captured resource pointers for this run's ASLR. Each saved resource is reloaded by
// name (LoadResource caches it, keeping the payload alive); every captured heap word inside a resource's old
// payload range is shifted to the reloaded payload. FAIL-CLOSED: if any resource can't be reloaded at its saved
// size, refuse the load -- a half-relocated heap is worse than no load. Returns false to abort the load.
static bool SaveState_RelocateResourcePointers(SaveStateInfo* info, const std::vector<SaveStateResEntry>& table) {
    if (table.empty()) {
        return true;
    }
    auto rm = Ship::Context::GetRawInstance()->GetResourceManager();
    if (rm == nullptr) {
        SPDLOG_ERROR("[SaveState][B2] no resource manager -- refusing load");
        return false;
    }

    std::vector<B2Range> ranges;
    ranges.reserve(table.size());
    for (const auto& e : table) {
        if (e.oldBase == 0 || e.oldSize == 0) {
            continue;
        }
        auto res = rm->LoadResource(e.name); // reload (cached) -> keeps the payload + its sub-allocations alive
        if (res == nullptr) {
            SPDLOG_ERROR("[SaveState][B2] resource '{}' unavailable -- refusing load", e.name);
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
            SPDLOG_ERROR("[SaveState][B2] '{}' block sub={} mismatch (newBase={}, size {} vs saved {}) -- refusing load",
                         e.name, e.subIndex, nb, (uint64_t)ns, e.oldSize);
            return false; // fail-closed: a different asset/build, or a sub-allocation that no longer exists
        }
        const uintptr_t newBase = (uintptr_t)nb;
        const intptr_t delta = (intptr_t)(newBase - (uintptr_t)e.oldBase);
        ranges.push_back({ (uintptr_t)e.oldBase, (uintptr_t)e.oldBase + e.oldSize, delta });
    }

    // Relocate: sort by old range start, binary-search each 8-byte-aligned heap word.
    std::sort(ranges.begin(), ranges.end(), [](const B2Range& a, const B2Range& b) { return a.oldLo < b.oldLo; });
    uintptr_t* p = reinterpret_cast<uintptr_t*>(&info->sysHeapCopy);
    const size_t n = SYSTEM_HEAP_SIZE / sizeof(uintptr_t);

    // Old resource-heap band: an 8-byte-aligned heap word inside it that no range covers is an unrelocated
    // resource pointer -- a coverage gap. Counted as a sanity signal; the fail-closed reload above is the real
    // safety net. (A non-zero count after a game-asset update would flag a new resource type needing
    // GetSubAllocations.)
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

bool SaveState::WriteToDisk(void) {
    const std::string path = SaveStateDiskPath(this->slot);
    std::vector<SaveStateResEntry> resTable = SaveState_CollectResources();
    FILE* f = fopen(path.c_str(), "wb");
    if (f == nullptr) {
        SPDLOG_ERROR("[SaveState] could not open '{}' for write", path);
        return false;
    }
    SaveStateHeader header = {};
    header.stateMagic = SAVESTATE_DISK_MAGIC;
    header.stateVersion = SAVESTATE_DISK_VERSION;
    header.infoSize = (uint64_t)sizeof(SaveStateInfo);
    header.exeBase = (uint64_t)gExeBase;
    header.exeSize = gExeSize;
    header.resCount = (uint32_t)resTable.size();
    bool ok = (fwrite(&header, sizeof(header), 1, f) == 1) && (fwrite(this->info.get(), sizeof(SaveStateInfo), 1, f) == 1);
    if (ok && !resTable.empty()) {
        ok = (fwrite(resTable.data(), sizeof(SaveStateResEntry), resTable.size(), f) == resTable.size());
    }
    fclose(f);
    if (!ok) {
        SPDLOG_ERROR("[SaveState] write failed for '{}'", path);
        return false;
    }
    SPDLOG_INFO("[SaveState] wrote slot {} -> '{}' ({} bytes + {} resources)", this->slot, path, sizeof(SaveStateInfo),
                resTable.size());
    return true;
}

bool SaveState::ReadFromDisk(void) {
    const std::string path = SaveStateDiskPath(this->slot);
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
    if (ok) {
        ok = (fread(this->info.get(), sizeof(SaveStateInfo), 1, f) == 1);
        if (!ok) {
            SPDLOG_ERROR("[SaveState] '{}' truncated body", path);
        }
    }
    std::vector<SaveStateResEntry> resTable;
    if (ok && header.resCount > 0) {
        resTable.resize(header.resCount);
        ok = (fread(resTable.data(), sizeof(SaveStateResEntry), header.resCount, f) == header.resCount);
        if (!ok) {
            SPDLOG_ERROR("[SaveState] '{}' truncated resource table", path);
        }
    }
    fclose(f);
    if (ok) {
        // B1: patch code/function pointers for this run's ASLR before the state gets applied.
        SaveState_RelocateExePointers(this->info.get(), header.exeBase, header.exeSize, (uint64_t)gExeBase);
        // B2: reload + relocate resource pointers. Fail-closed -- if a resource can't be reloaded at its saved
        // size, abort the load rather than apply a heap with dangling resource pointers.
        if (!SaveState_RelocateResourcePointers(this->info.get(), resTable)) {
            SPDLOG_ERROR("[SaveState] '{}' resource relocation failed -- not applying", path);
            return false;
        }
        SPDLOG_INFO("[SaveState] read slot {} <- '{}'", this->slot, path);
    }
    return ok;
}

void SaveStateMgr::SetCurrentSlot(unsigned int slot) {
    Ship::Context::GetRawInstance()->GetWindow()->GetGui()->GetGameOverlay()->TextDrawNotification(1.0f, true,
                                                                                                   "slot %u set", slot);
    this->currentSlot = slot;
}

unsigned int SaveStateMgr::GetCurrentSlot(void) {
    return this->currentSlot;
}

// After a cross-restart load we keep this session's live audio, so it's still playing the relaunch scene's music.
// The savestate restored play->sequenceCtx (the destination scene's BGM/ambience); blank gSaveContext's "currently
// playing" markers (0xFF matches no real id) so the game's own scene-sequence player switches to the right track.
static void PlayDestinationSceneAudio(void) {
    if (gPlayState == nullptr) {
        return;
    }
    constexpr uint8_t kNoSequencePlaying = 0xFF;
    gSaveContext.seqId = kNoSequencePlaying;
    gSaveContext.natureAmbienceId = kNoSequencePlaying;
    Environment_PlaySceneSequence(gPlayState);
}

void SaveStateMgr::ProcessSaveStateRequests(void) {
    while (!this->requests.empty()) {
        const auto& request = this->requests.front();

        switch (request.type) {
            case RequestType::SAVE:
                if (!this->states.contains(request.slot)) {
                    this->states[request.slot] =
                        std::make_shared<SaveState>(OTRGlobals::Instance->gSaveStateMgr, request.slot);
                }
                this->states[request.slot]->Save();
                Ship::Context::GetRawInstance()->GetWindow()->GetGui()->GetGameOverlay()->TextDrawNotification(
                    1.0f, true, "saved state %u", request.slot);
                break;
            case RequestType::LOAD:
                if (this->states.contains(request.slot)) {
                    this->states[request.slot]->Load();
                    Ship::Context::GetRawInstance()->GetWindow()->GetGui()->GetGameOverlay()->TextDrawNotification(
                        1.0f, true, "loaded state %u", request.slot);
                } else {
                    SPDLOG_ERROR("Invalid SaveState slot: {}", request.slot);
                }
                break;
            case RequestType::SAVE_TO_DISK:
                if (!this->states.contains(request.slot)) {
                    this->states[request.slot] =
                        std::make_shared<SaveState>(OTRGlobals::Instance->gSaveStateMgr, request.slot);
                }
                this->states[request.slot]->Save(); // capture live state into info, then serialize it
                Ship::Context::GetRawInstance()->GetWindow()->GetGui()->GetGameOverlay()->TextDrawNotification(
                    1.0f, true, this->states[request.slot]->WriteToDisk() ? "saved state %u to disk"
                                                                          : "disk save %u FAILED",
                    request.slot);
                break;
            case RequestType::LOAD_FROM_DISK:
                if (!this->states.contains(request.slot)) {
                    this->states[request.slot] =
                        std::make_shared<SaveState>(OTRGlobals::Instance->gSaveStateMgr, request.slot);
                }
                if (this->states[request.slot]->ReadFromDisk()) {
                    this->states[request.slot]->Load(/*crossRestart=*/true); // keep live audio (saved tables stale)
                    PlayDestinationSceneAudio();                              // then switch to the destination's BGM
                    Ship::Context::GetRawInstance()->GetWindow()->GetGui()->GetGameOverlay()->TextDrawNotification(
                        1.0f, true, "loaded state %u from disk", request.slot);
                } else {
                    Ship::Context::GetRawInstance()->GetWindow()->GetGui()->GetGameOverlay()->TextDrawNotification(
                        1.0f, true, "disk load %u FAILED", request.slot);
                }
                break;
                [[unlikely]] default
                    : SPDLOG_ERROR("Invalid SaveState request type: Unknown ({})", static_cast<int>(request.type));
                break;
        }
        this->requests.pop();
    }
}

SaveStateReturn SaveStateMgr::AddRequest(const SaveStateRequest request) {
    if (gPlayState == nullptr) {
        SPDLOG_ERROR("[SOH] Can not save or load a state outside of \"GamePlay\"");
        Ship::Context::GetRawInstance()->GetWindow()->GetGui()->GetGameOverlay()->TextDrawNotification(
            1.0f, true, "states not available here", request.slot);
        return SaveStateReturn::FAIL_WRONG_GAMESTATE;
    }

    switch (request.type) {
        case RequestType::SAVE:
            requests.push(request);
            return SaveStateReturn::SUCCESS;
        case RequestType::SAVE_TO_DISK:
            requests.push(request);
            return SaveStateReturn::SUCCESS;
        case RequestType::LOAD_FROM_DISK:
            // Allowed even when the slot isn't in memory -- it loads the state from the file.
            requests.push(request);
            return SaveStateReturn::SUCCESS;
        case RequestType::LOAD:
            if (states.contains(request.slot)) {
                requests.push(request);
                return SaveStateReturn::SUCCESS;
            } else {
                SPDLOG_ERROR("Invalid SaveState slot: {}", request.slot);
                Ship::Context::GetRawInstance()->GetWindow()->GetGui()->GetGameOverlay()->TextDrawNotification(
                    1.0f, true, "state slot %u empty", request.slot);
                return SaveStateReturn::FAIL_INVALID_SLOT;
            }
            [[unlikely]] default
                : SPDLOG_ERROR("Invalid SaveState request type: Unknown ({})", static_cast<int>(request.type));
            return SaveStateReturn::FAIL_BAD_REQUEST;
    }
}

void SaveState::Save(void) {
    std::unique_lock<std::mutex> Lock(audio.mutex);
    memcpy(&info->sysHeapCopy, gSystemHeap, SYSTEM_HEAP_SIZE /* sizeof(gSystemHeap) */);
    memcpy(&info->audioHeapCopy, gAudioHeap, AUDIO_HEAP_SIZE /* sizeof(gAudioContext) */);

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
    BackupCameraData();
    SaveOnePointDemoData();
    SaveOverlayStaticData();
    SaveMiscCodeData();
}

void SaveState::Load(bool crossRestart) {
    std::unique_lock<std::mutex> Lock(audio.mutex);
    memcpy(gSystemHeap, &info->sysHeapCopy, SYSTEM_HEAP_SIZE);
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
    LoadCameraData();
    LoadOnePointDemoData();
    LoadOverlayStaticData();
    LoadMiscCodeData();
}
