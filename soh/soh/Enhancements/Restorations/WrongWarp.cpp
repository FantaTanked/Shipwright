#include <libultraship/bridge/consolevariablebridge.h>
#include "soh/Enhancements/game-interactor/GameInteractor_Hooks.h"
#include "soh/ShipInit.hpp"
#include <spdlog/spdlog.h>
#include "WrongWarpData.h"

extern "C" {
#include "variables.h"
extern PlayState* gPlayState;
}

/*
 * Faithful wrong warps (develop-speedrun, 64-bit Ship). See wrongwarp_dictionary.md.
 *
 * GENERAL, data-driven, no per-warp table, no playtesting. The wrong-warp result is computed exactly the
 * way mzxrules' tool (https://mzxrules.github.io/zelda64/ocarina/ww/) computes it from the real ROM:
 *
 *   cs          = cutsceneIndex & 0xF                       (the cutscene number, when cutsceneIndex>=0xFFF0)
 *   lookupIndex = entranceIndex + cs + 4                    (the "+cs+4" entrance overshoot; the decomp itself
 *                                                            indexes gEntranceTable[entranceIndex+sceneSetupIndex]
 *                                                            with sceneSetupIndex = 4+cs -- z_play.c:494/466)
 *   (destScene, destSpawn) = gEntranceTable[lookupIndex]
 *   Out         = kWrongWarpOutcomes[(destScene,destSpawn,cs)]   (mzxrules SpawnResults: 3=clean control,
 *                                                            4=cutscene plays, 1/2=invalid/garbage crash)
 *
 * For Out==3 (clean gameplay control -- ganondoor, jabu->DC, 1080, deku->bongo, and ~1158 others) we land
 * the player at the destination with control by setting entranceIndex=lookupIndex, cutsceneIndex=0 (the
 * scene then loads at setup 0 with no cutscene -- Garrett PR#37's idiom, but every row computed, not typed).
 * Validated: ganondoor 0x252 + cs1 + 4 = 0x257 (Tower Collapse Interior, spawn 3) -> Out=3.
 *
 * Out==4 (a real cutscene plays) and Out==1/2 (console crash/garbage) are left to mainline behaviour for now
 * and only logged; a future faithful-crash layer can own them.
 *
 * Cvars: gWrongWarp = apply; gWrongWarpCapture = diagnostic logging (pure observe).
 */
#define CVAR_WRONG_WARP_NAME CVAR_ENHANCEMENT("WrongWarp")
#define CVAR_WRONG_WARP_CAPTURE_NAME CVAR_ENHANCEMENT("WrongWarpCapture")

// ---- capture diagnostic (per-frame) ---------------------------------------------------------------------
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

// ---- mzxrules outcome lookup (exact cutscene first, then the cs=-1 "any" fallback) ----------------------
static int WrongWarpLookupOut(int scene, int spawn, int cs) {
    int anyOut = -1;
    for (const WrongWarpOutcome& r : kWrongWarpOutcomes) {
        if (r.scene == scene && r.spawn == spawn) {
            if (r.cs == cs) {
                return r.out;
            }
            if (r.cs == -1) {
                anyOut = r.out;
            }
        }
    }
    return anyOut;
}

// ---- apply: compute the mzxrules result and force the destination for clean (Out=3) warps ----------------
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
        // Clean gameplay control: land at the destination at setup 0, no cutscene.
        gSaveContext.entranceIndex = (int16_t)lookupIndex;
        gSaveContext.cutsceneIndex = 0;
        SPDLOG_INFO("[WrongWarp][apply] start=0x{:X} cs={} -> 0x{:X} scene={} spawn={} (Out=3 control)",
                    (uint32_t)startIdx, cs, (uint32_t)lookupIndex, destScene, destSpawn);
    } else {
        SPDLOG_INFO("[WrongWarp][apply] start=0x{:X} cs={} -> 0x{:X} scene={} spawn={} Out={} (unmodified)",
                    (uint32_t)startIdx, cs, (uint32_t)lookupIndex, destScene, destSpawn, out);
    }
}

void RegisterWrongWarp() {
    // Per-frame capture diagnostic.
    GameInteractor::Instance->RegisterGameHook<GameInteractor::OnGameFrameUpdate>([]() {
        if (CVarGetInteger(CVAR_WRONG_WARP_CAPTURE_NAME, 0)) {
            WrongWarpCaptureTick();
        }
    });

    // Apply at the top of Cutscene_HandleConditionalTriggers (VB_PLAY_TRANSITION_CS) -- runs before the
    // scene-setup overshoot is taken, so rewriting entranceIndex/cutsceneIndex here lands the destination.
    // We never veto: only rewrite, then let the function proceed.
    REGISTER_VB_SHOULD(VB_PLAY_TRANSITION_CS, {
        if (CVarGetInteger(CVAR_WRONG_WARP_NAME, 0)) {
            WrongWarpApply();
        }
    });
}

static RegisterShipInitFunc initFunc(RegisterWrongWarp);
