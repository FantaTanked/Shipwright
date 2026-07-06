#include <libultraship/bridge/consolevariablebridge.h>
#include "soh/Enhancements/game-interactor/GameInteractor_Hooks.h"
#include "soh/ShipInit.hpp"
#include "WrongWarpData.h"

extern "C" {
#include "variables.h"
#include "macros.h"
}

enum WrongWarpOut { WW_OUT_INVALID = 1, WW_OUT_GARBAGE = 2, WW_OUT_CLEAN = 3, WW_OUT_CUTSCENE = 4 };

static bool WrongWarpIsClean(int scene, int spawn, int cutsceneNum) {
    for (const WrongWarpOutcome& row : sWrongWarpOutcomes) {
        if (row.scene == scene && row.spawn == spawn && (row.cs == cutsceneNum || row.cs == -1) &&
            row.out == WW_OUT_CLEAN) {
            return true;
        }
    }
    return false;
}

static void WrongWarpApply() {
    uint16_t cutsceneIndex = (uint16_t)gSaveContext.cutsceneIndex;
    if (cutsceneIndex < 0xFFF0) {
        return;
    }

    int cutsceneNum = cutsceneIndex & 0xF;

    int lookupIndex = (uint16_t)gSaveContext.entranceIndex + SCENE_LAYER_CUTSCENE_FIRST + cutsceneNum;
    if (lookupIndex >= ENTR_MAX) {
        return;
    }
    const EntranceInfo& dest = gEntranceTable[lookupIndex];
    if (!WrongWarpIsClean(dest.scene, dest.spawn, cutsceneNum)) {
        return;
    }

    int layer;
    if (LINK_IS_ADULT) {
        layer = IS_DAY ? SCENE_LAYER_ADULT_DAY : SCENE_LAYER_ADULT_NIGHT;
    } else {
        layer = IS_DAY ? SCENE_LAYER_CHILD_DAY : SCENE_LAYER_CHILD_NIGHT;
    }

    gSaveContext.entranceIndex = (int16_t)(lookupIndex - layer);
    gSaveContext.cutsceneIndex = 0;
}

void RegisterWrongWarp() {
    REGISTER_VB_SHOULD(VB_PLAY_TRANSITION_CS, {
        if (CVarGetInteger(CVAR_ENHANCEMENT("WrongWarp"), 0)) {
            WrongWarpApply();
        }
    });
}

static RegisterShipInitFunc initFunc(RegisterWrongWarp);
