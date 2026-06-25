#include <libultraship/bridge/consolevariablebridge.h>
#include "soh/Enhancements/game-interactor/GameInteractor_Hooks.h"
#include "soh/ShipInit.hpp"
#include <spdlog/spdlog.h>
#include "WrongWarpData.h"

extern "C" {
#include "variables.h"
#include "macros.h"
extern PlayState* gPlayState;
}

// Data-driven wrong warps (see tools/wrongwarp): replicate mzxrules' outcome for the cutscene-index entrance
// overshoot; for a clean result (Out=3) land at the destination with control. CVars gWrongWarp/gWrongWarpCapture.

// capture diagnostic (per-frame)
static void WrongWarpCaptureTick() {
    if (gPlayState == NULL || gSaveContext.cutsceneIndex < 0xFFF0) {
        return;
    }
    PlayState* play = gPlayState;
    static uint32_t sLastSig = 0xFFFFFFFFu;
    uint32_t sig = ((uint32_t)(uint16_t)gSaveContext.entranceIndex << 16) | (uint32_t)(uint16_t)gSaveContext.cutsceneIndex;
    sig ^= ((uint32_t)(uint16_t)play->nextEntranceIndex << 7) ^ ((uint32_t)play->transitionTrigger << 3) ^
           (uint32_t)play->sceneNum;
    if (sig == sLastSig) {
        return;
    }
    sLastSig = sig;
    SPDLOG_INFO("[WrongWarp] scene=0x{:X} entr=0x{:X} csIdx=0x{:04X} frames={} -> nextEntr=0x{:X} transTrig={}",
                (uint32_t)play->sceneNum, (uint32_t)(uint16_t)gSaveContext.entranceIndex,
                (uint32_t)(uint16_t)gSaveContext.cutsceneIndex, (int32_t)play->csCtx.frames,
                (uint32_t)(uint16_t)play->nextEntranceIndex, (uint32_t)play->transitionTrigger);
}

// mzxrules outcome lookup: exact cutscene, then the cs=-1 "any" fallback
static int WrongWarpLookupOut(int scene, int spawn, int cs) {
    int anyOut = -1;
    for (const WrongWarpOutcome& r : kWrongWarpOutcomes) {
        if (r.scene != scene || r.spawn != spawn) {
            continue;
        }
        if (r.cs == cs) {
            return r.out;
        }
        if (r.cs == -1) {
            anyOut = r.out;
        }
    }
    return anyOut;
}

// apply: force the destination for clean (Out=3) warps
static void WrongWarpApply() {
    uint16_t csIdx = (uint16_t)gSaveContext.cutsceneIndex;
    if (csIdx < 0xFFF0) {
        return;
    }
    int cs = csIdx & 0xF;
    int startIdx = (uint16_t)gSaveContext.entranceIndex;
    int lookupIndex = startIdx + cs + 4; // +cs+4 entrance overshoot (mirrors gEntranceTable[entr+sceneSetupIndex])
    if (lookupIndex < 0 || lookupIndex >= ENTR_MAX) {
        return;
    }
    int destScene = gEntranceTable[lookupIndex].scene;
    int destSpawn = gEntranceTable[lookupIndex].spawn;
    if (destScene < 0) {
        return;
    }
    int out = WrongWarpLookupOut(destScene, destSpawn, cs);
    if (out == 3) {
        // Clear cutsceneIndex (no cutscene) but pre-subtract the age/time scene layer that the normal load
        // path (z_play.c:467-474) re-adds, so entranceIndex+sceneSetupIndex resolves exactly to lookupIndex.
        int layer = LINK_IS_ADULT ? (IS_DAY ? SCENE_LAYER_ADULT_DAY : SCENE_LAYER_ADULT_NIGHT)
                                  : (IS_DAY ? SCENE_LAYER_CHILD_DAY : SCENE_LAYER_CHILD_NIGHT);
        gSaveContext.entranceIndex = (int16_t)(lookupIndex - layer);
        gSaveContext.cutsceneIndex = 0;
        SPDLOG_INFO("[WrongWarp][apply] start=0x{:X} cs={} -> 0x{:X} scene={} spawn={} layer={} (Out=3 control)",
                    (uint32_t)startIdx, cs, (uint32_t)lookupIndex, destScene, destSpawn, layer);
    } else {
        SPDLOG_INFO("[WrongWarp][apply] start=0x{:X} cs={} -> 0x{:X} scene={} spawn={} Out={} (unmodified)",
                    (uint32_t)startIdx, cs, (uint32_t)lookupIndex, destScene, destSpawn, out);
    }
}

void RegisterWrongWarp() {
    // Per-frame capture diagnostic.
    GameInteractor::Instance->RegisterGameHook<GameInteractor::OnGameFrameUpdate>([]() {
        if (CVarGetInteger(CVAR_ENHANCEMENT("WrongWarpCapture"), 0)) {
            WrongWarpCaptureTick();
        }
    });

    // Runs at the top of Cutscene_HandleConditionalTriggers, before the scene-setup overshoot is taken, so the
    // rewrite lands the destination. Never vetoes -- only rewrites.
    REGISTER_VB_SHOULD(VB_PLAY_TRANSITION_CS, {
        if (CVarGetInteger(CVAR_ENHANCEMENT("WrongWarp"), 0)) {
            WrongWarpApply();
        }
    });
}

static RegisterShipInitFunc initFunc(RegisterWrongWarp);
