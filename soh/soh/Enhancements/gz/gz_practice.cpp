// ship-gz: "gz mode" practice features, modelled on the GameCube practice ROM (gz).
// When the gz toggle is on, the player can drive savestates with the controller alone.
//
// This file is phase 1+2: the toggle CVar and the in-game controller hotkeys
//   - D-pad Left  : save the current savestate slot
//   - D-pad Right : load the current savestate slot
// The navigable on-screen menu (R + C-Down) is a later phase.

#include <libultraship/bridge.h>
#include "soh/Enhancements/game-interactor/GameInteractor_Hooks.h"
#include "soh/ShipInit.hpp"
#include "soh/OTRGlobals.h"
#include "soh/Enhancements/savestates.h"

extern "C" {
extern PlayState* gPlayState;
#include "macros.h"
}

// gz mode is gated behind savestates being enabled, since every hotkey it adds
// operates on a savestate slot.
#define CVAR_GZ_MODE_NAME CVAR_CHEAT("GzMode")
#define CVAR_GZ_MODE_DEFAULT 0
#define CVAR_GZ_MODE_VALUE CVarGetInteger(CVAR_GZ_MODE_NAME, CVAR_GZ_MODE_DEFAULT)

static void OnGameFrameUpdateGzMode() {
    if (!GameInteractor::IsSaveLoaded(true) || gPlayState == nullptr) {
        return;
    }
    if (CVarGetInteger(CVAR_CHEAT("SaveStatesEnabled"), 0) == 0) {
        return;
    }

    const Input* input = &gPlayState->state.input[0];
    const auto mgr = OTRGlobals::Instance->gSaveStateMgr;
    const unsigned int slot = mgr->GetCurrentSlot();

    // press.button is edge-triggered (set only on the frame the button goes down),
    // so a single tap fires exactly one request.
    if (CHECK_BTN_ALL(input->press.button, BTN_DLEFT)) {
        mgr->AddRequest({ slot, RequestType::SAVE });
    } else if (CHECK_BTN_ALL(input->press.button, BTN_DRIGHT)) {
        mgr->AddRequest({ slot, RequestType::LOAD });
    }
}

void RegisterGzMode() {
    COND_HOOK(OnGameFrameUpdate, CVAR_GZ_MODE_VALUE, OnGameFrameUpdateGzMode);
}

static RegisterShipInitFunc initFunc(RegisterGzMode, { CVAR_GZ_MODE_NAME });
