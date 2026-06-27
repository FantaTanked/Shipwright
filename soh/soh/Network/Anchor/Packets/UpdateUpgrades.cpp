#include "soh/Network/Anchor/Anchor.h"
#include <nlohmann/json.hpp>
#include <libultraship/libultraship.h>
#include "soh/OTRGlobals.h"

extern "C" {
#include "variables.h"
}

/**
 * UPDATE_UPGRADES
 *
 * Syncs the Great Fairy magic/defense upgrades. These are written directly to the save context (no
 * Item_Give), so they have no GIVE_ITEM delta; this pushes them to teammates the moment they're acquired.
 */

void Anchor::SendPacket_UpdateUpgrades() {
    if (!IsSaveLoaded() || !roomState.syncItemsAndFlags) {
        return;
    }

    nlohmann::json payload;
    payload["type"] = UPDATE_UPGRADES;
    payload["targetTeamId"] = CVarGetString(CVAR_REMOTE_ANCHOR("TeamId"), "default");
    payload["addToQueue"] = true;
    payload["isMagicAcquired"] = (bool)gSaveContext.isMagicAcquired;
    payload["isDoubleMagicAcquired"] = (bool)gSaveContext.isDoubleMagicAcquired;
    payload["isDoubleDefenseAcquired"] = (bool)gSaveContext.isDoubleDefenseAcquired;

    SendJsonToRemote(payload);
}

void Anchor::HandlePacket_UpdateUpgrades(nlohmann::json payload) {
    if (!IsSaveLoaded() || !roomState.syncItemsAndFlags) {
        return;
    }

    // Apply the full, consistent magic state directly (level, capacity AND current value). If we only
    // set isMagicAcquired and let the meter re-derive, gSaveContext.magic stays 0 and saving persists an
    // empty meter (which then loads back as 0 / regresses the team).
    if (payload.value("isDoubleMagicAcquired", false) && !gSaveContext.isDoubleMagicAcquired) {
        gSaveContext.isMagicAcquired = true;
        gSaveContext.isDoubleMagicAcquired = true;
        gSaveContext.magicLevel = 2;
        gSaveContext.magicCapacity = MAGIC_DOUBLE_METER;
        gSaveContext.magic = MAGIC_DOUBLE_METER;
    } else if (payload.value("isMagicAcquired", false) && !gSaveContext.isMagicAcquired) {
        gSaveContext.isMagicAcquired = true;
        gSaveContext.magicLevel = 1;
        gSaveContext.magicCapacity = MAGIC_NORMAL_METER;
        gSaveContext.magic = MAGIC_NORMAL_METER;
    }

    if (payload.value("isDoubleDefenseAcquired", false) && !gSaveContext.isDoubleDefenseAcquired) {
        gSaveContext.isDoubleDefenseAcquired = true;
        gSaveContext.inventory.defenseHearts = 20;
    }
}
