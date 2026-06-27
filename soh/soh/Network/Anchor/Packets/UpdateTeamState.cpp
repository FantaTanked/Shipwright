#include "soh/Network/Anchor/Anchor.h"
#include "soh/Network/Anchor/JsonConversions.hpp"
#include <nlohmann/json.hpp>
#include <libultraship/libultraship.h>
#include "soh/OTRGlobals.h"
#include "soh/Notification/Notification.h"
#include "soh/Enhancements/randomizer/randomizer.h"

extern "C" {
#include "variables.h"
extern PlayState* gPlayState;
}

/**
 * UPDATE_TEAM_STATE
 *
 * Pushes the current save state to the server for other teammates to use.
 *
 * Fires when the server passes on a REQUEST_TEAM_STATE packet, or when this client saves the game
 *
 * When sending this packet we will assume that the team queue has been emptied for this client, so the queue
 * stored in the server will be cleared.
 *
 * When receiving this packet, if there is items in the team queue, we will play them back in order.
 */

void Anchor::SendPacket_UpdateTeamState() {
    if (!IsSaveLoaded() || !roomState.syncItemsAndFlags) {
        return;
    }

    json payload;
    payload["type"] = UPDATE_TEAM_STATE;
    payload["targetTeamId"] = CVarGetString(CVAR_REMOTE_ANCHOR("TeamId"), "default");

    // Assume the team queue has been emptied, so clear it
    payload["queue"] = json::array();

    payload["state"] = gSaveContext;
    // manually update current scene flags
    payload["state"]["sceneFlags"][gPlayState->sceneNum * 4] = gPlayState->actorCtx.flags.chest;
    payload["state"]["sceneFlags"][gPlayState->sceneNum * 4 + 1] = gPlayState->actorCtx.flags.swch;
    payload["state"]["sceneFlags"][gPlayState->sceneNum * 4 + 2] = gPlayState->actorCtx.flags.clear;
    payload["state"]["sceneFlags"][gPlayState->sceneNum * 4 + 3] = gPlayState->actorCtx.flags.collect;

    // The commented out code below is an attempt at sending the entire randomizer seed over, in hopes that a player
    // doesn't have to generate the seed themselves Currently it doesn't work :)
    if (IS_RANDO) {
        auto randoContext = Rando::Context::GetInstance();

        payload["state"]["rando"] = json::object();
        payload["state"]["rando"]["itemLocations"] = json::array();
        for (int i = 0; i < RC_MAX; i++) {
            payload["state"]["rando"]["itemLocations"][i] = json::array();
            // payload["state"]["rando"]["itemLocations"][i]["rgID"] =
            // randoContext->GetItemLocation(i)->GetPlacedRandomizerGet();
            payload["state"]["rando"]["itemLocations"][i][0] = randoContext->GetItemLocation(i)->GetCheckStatus();
            payload["state"]["rando"]["itemLocations"][i][1] = (u8)randoContext->GetItemLocation(i)->GetIsSkipped();

            // if (randoContext->GetItemLocation(i)->GetPlacedRandomizerGet() == RG_ICE_TRAP) {
            //     payload["state"]["rando"]["itemLocations"][i]["fakeRgID"] =
            //     randoContext->GetItemOverride(i).LooksLike();
            //     payload["state"]["rando"]["itemLocations"][i]["trickName"] = json::object();
            //     payload["state"]["rando"]["itemLocations"][i]["trickName"]["english"] =
            //     randoContext->GetItemOverride(i).GetTrickName().GetEnglish();
            //     payload["state"]["rando"]["itemLocations"][i]["trickName"]["french"] =
            //     randoContext->GetItemOverride(i).GetTrickName().GetFrench();
            // }
            // if (randoContext->GetItemLocation(i)->HasCustomPrice()) {
            //     payload["state"]["rando"]["itemLocations"][i]["price"] =
            //     randoContext->GetItemLocation(i)->GetPrice();
            // }
        }

        // auto entranceCtx = randoContext->GetEntranceShuffler();
        // for (int i = 0; i < ENTRANCE_OVERRIDES_MAX_COUNT; i++) {
        //     payload["state"]["rando"]["entrances"][i] = json::object();
        //     payload["state"]["rando"]["entrances"][i]["type"] = entranceCtx->entranceOverrides[i].type;
        //     payload["state"]["rando"]["entrances"][i]["index"] = entranceCtx->entranceOverrides[i].index;
        //     payload["state"]["rando"]["entrances"][i]["destination"] = entranceCtx->entranceOverrides[i].destination;
        //     payload["state"]["rando"]["entrances"][i]["override"] = entranceCtx->entranceOverrides[i].override;
        //     payload["state"]["rando"]["entrances"][i]["overrideDestination"] =
        //     entranceCtx->entranceOverrides[i].overrideDestination;
        // }

        // payload["state"]["rando"]["seed"] = json::array();
        // for (int i = 0; i < randoContext->hashIconIndexes.size(); i++) {
        //     payload["state"]["rando"]["seed"][i] = randoContext->hashIconIndexes[i];
        // }
        // payload["state"]["rando"]["inputSeed"] = randoContext->GetSeedString();
        // payload["state"]["rando"]["finalSeed"] = randoContext->GetSeed();

        // payload["state"]["rando"]["randoSettings"] = json::array();
        // for (int i = 0; i < RSK_MAX; i++) {
        //     payload["state"]["rando"]["randoSettings"][i] =
        //     randoContext->GetOption((RandomizerSettingKey(i))).GetSelectedOptionIndex();
        // }

        // payload["state"]["rando"]["masterQuestDungeonCount"] = randoContext->GetDungeons()->CountMQ();
        // payload["state"]["rando"]["masterQuestDungeons"] = json::array();
        // for (int i = 0; i < randoContext->GetDungeons()->GetDungeonListSize(); i++) {
        //     payload["state"]["rando"]["masterQuestDungeons"][i] = randoContext->GetDungeon(i)->IsMQ();
        // }
        // for (int i = 0; i < randoContext->GetTrials()->GetTrialListSize(); i++) {
        //     payload["state"]["rando"]["requiredTrials"][i] = randoContext->GetTrial(i)->IsRequired();
        // }
    }

    SendJsonToRemote(payload);
}

void Anchor::SendPacket_ClearTeamState(std::string teamId) {
    json payload;
    payload["type"] = UPDATE_TEAM_STATE;
    payload["targetTeamId"] = teamId;
    payload["queue"] = json::array();
    payload["state"] = json::object();
    SendJsonToRemote(payload);
}

void Anchor::HandlePacket_UpdateTeamState(nlohmann::json payload) {
    if (!roomState.syncItemsAndFlags) {
        return;
    }

    isHandlingUpdateTeamState = true;
    // This can happen in between file select and the game starting, so we can't use this check, but we need to ensure
    // we be careful to wrap PlayState usage in this check
    //
    // if (!IsSaveLoaded()) {
    //     return;
    // }

    if (payload.contains("state")) {
        SaveContext loadedData = payload["state"].get<SaveContext>();

        // Co-op progress must be monotonic: a teammate who is behind on an upgrade must never
        // overwrite a teammate who is ahead. Merge upward (MAX/OR), never assign destructively.
        if (loadedData.healthCapacity > gSaveContext.healthCapacity) {
            gSaveContext.healthCapacity = loadedData.healthCapacity;
        }
        if (loadedData.magicLevel > gSaveContext.magicLevel) {
            gSaveContext.magicLevel = loadedData.magicLevel;
        }
        if (loadedData.magicCapacity > gSaveContext.magicCapacity) {
            gSaveContext.magicCapacity = loadedData.magicCapacity;
        }
        gSaveContext.isMagicAcquired |= loadedData.isMagicAcquired;
        gSaveContext.isDoubleMagicAcquired |= loadedData.isDoubleMagicAcquired;
        gSaveContext.isDoubleDefenseAcquired |= loadedData.isDoubleDefenseAcquired;
        gSaveContext.bgsFlag |= loadedData.bgsFlag;
        // Current magic and Giant's Knife durability are per-player consumables; leave them local.

        // Quest id is shared across a team; only the cumulative rando counters need merging.
        gSaveContext.ship.quest.id = loadedData.ship.quest.id;
        if (IS_RANDO) {
            if (loadedData.ship.quest.data.randomizer.triforcePiecesCollected >
                gSaveContext.ship.quest.data.randomizer.triforcePiecesCollected) {
                gSaveContext.ship.quest.data.randomizer.triforcePiecesCollected =
                    loadedData.ship.quest.data.randomizer.triforcePiecesCollected;
            }
            if (loadedData.ship.quest.data.randomizer.bombchuUpgradeLevel >
                gSaveContext.ship.quest.data.randomizer.bombchuUpgradeLevel) {
                gSaveContext.ship.quest.data.randomizer.bombchuUpgradeLevel =
                    loadedData.ship.quest.data.randomizer.bombchuUpgradeLevel;
            }
        }

        for (int i = 0; i < 124; i++) {
            // Switch flags hold a couple of non-monotonic state bits (water temple water level,
            // forest elevator) that must stay local; union every other switch bit. The collapse
            // timer flag (0x36) is a temp switch outside this 32-bit field, so nothing to keep here.
            u32 swchKeepLocal = (i == SCENE_WATER_TEMPLE)    ? ((1u << 0x1C) | (1u << 0x1D) | (1u << 0x1E))
                                : (i == SCENE_FOREST_TEMPLE) ? (1u << 0x1B)
                                                             : 0u;
            u32 remoteSwch = loadedData.sceneFlags[i].swch & ~swchKeepLocal;
            // The Treasure Box Shop re-locks its chests on every play, so its chest flags stay local.
            u32 remoteChest = (i == SCENE_TREASURE_BOX_SHOP) ? 0u : loadedData.sceneFlags[i].chest;

            gSaveContext.sceneFlags[i].chest |= remoteChest;
            gSaveContext.sceneFlags[i].swch |= remoteSwch;
            gSaveContext.sceneFlags[i].clear |= loadedData.sceneFlags[i].clear;
            gSaveContext.sceneFlags[i].collect |= loadedData.sceneFlags[i].collect;
            // rooms/floors/unk are map-reveal state not carried in the payload; leave them local.

            if (IsSaveLoaded() && gPlayState->sceneNum == i) {
                gPlayState->actorCtx.flags.chest |= remoteChest;
                gPlayState->actorCtx.flags.swch |= remoteSwch;
                gPlayState->actorCtx.flags.clear |= loadedData.sceneFlags[i].clear;
                gPlayState->actorCtx.flags.collect |= loadedData.sceneFlags[i].collect;
            }
        }

        for (int i = 0; i < 14; i++) {
            gSaveContext.eventChkInf[i] |= loadedData.eventChkInf[i];
        }

        for (int i = 0; i < 4; i++) {
            gSaveContext.itemGetInf[i] |= loadedData.itemGetInf[i];
        }

        // Skip last row of infTable, don't want to sync swordless flag
        for (int i = 0; i < 29; i++) {
            gSaveContext.infTable[i] |= loadedData.infTable[i];
        }

        for (int i = 0; i < ceil((RAND_INF_MAX + 15) / 16); i++) {
            gSaveContext.ship.randomizerInf[i] |= loadedData.ship.randomizerInf[i];
        }

        for (int i = 0; i < 6; i++) {
            gSaveContext.gsFlags[i] |= loadedData.gsFlags[i];
        }

        gSaveContext.ship.stats.firstInput = loadedData.ship.stats.firstInput;
        gSaveContext.ship.stats.fileCreatedAt = loadedData.ship.stats.fileCreatedAt;

        // Inventory must merge as a union of progress, not a wholesale copy (which lets a teammate who
        // is behind erase items/upgrades). Consumables and non-monotonic slots stay local.

        // items[]: gain anything we lack; keep the higher tier in the two upgrade-in-place slots
        // (ocarina, hookshot); the adult/child trade slots are an in-place swap and stay local.
        for (int i = 0; i < ARRAY_COUNT(gSaveContext.inventory.items); i++) {
            if (i == SLOT_TRADE_ADULT || i == SLOT_TRADE_CHILD) {
                continue;
            }
            u8 mine = gSaveContext.inventory.items[i];
            u8 theirs = loadedData.inventory.items[i];
            if (mine == ITEM_NONE) {
                gSaveContext.inventory.items[i] = theirs;
            } else if (theirs != ITEM_NONE && theirs > mine && (i == SLOT_OCARINA || i == SLOT_HOOKSHOT)) {
                gSaveContext.inventory.items[i] = theirs;
            }
        }

        // ammo[]: consumable; only fill our empty slots from the team (beans propagate as before).
        for (int i = 0; i < ARRAY_COUNT(gSaveContext.inventory.ammo); i++) {
            if (gSaveContext.inventory.ammo[i] == 0 || i == SLOT(ITEM_BEAN) || i == SLOT(ITEM_BEAN + 1)) {
                gSaveContext.inventory.ammo[i] = loadedData.inventory.ammo[i];
            }
        }

        // equipment: tunics/boots (high byte) are never lost, so union them; swords/shields (low byte)
        // have legitimate loss paths (swordless, Giant's Knife break, Like-Like, MS removal) so keep local.
        gSaveContext.inventory.equipment = (gSaveContext.inventory.equipment & 0x00FF) |
                                           ((gSaveContext.inventory.equipment | loadedData.inventory.equipment) & 0xFF00);

        // upgrades: packed multi-bit levels (quiver/bomb bag/strength/...); take the higher level per
        // field. A bitwise OR would corrupt them (e.g. level 1 | level 2 = level 3).
        for (int i = 0; i < 8; i++) {
            u32 mineLvl = (gSaveContext.inventory.upgrades & gUpgradeMasks[i]) >> gUpgradeShifts[i];
            u32 theirLvl = (loadedData.inventory.upgrades & gUpgradeMasks[i]) >> gUpgradeShifts[i];
            if (theirLvl > mineLvl) {
                gSaveContext.inventory.upgrades =
                    (gSaveContext.inventory.upgrades & ~gUpgradeMasks[i]) | (theirLvl << gUpgradeShifts[i]);
            }
        }

        // questItems: union the medallion/song/stone flag bits; the top nibble is a heart-piece count,
        // so take the higher (never OR a count).
        {
            u32 mineHp = gSaveContext.inventory.questItems & 0xF0000000;
            u32 theirHp = loadedData.inventory.questItems & 0xF0000000;
            gSaveContext.inventory.questItems =
                ((gSaveContext.inventory.questItems | loadedData.inventory.questItems) & 0x0FFFFFFF) |
                (theirHp > mineHp ? theirHp : mineHp);
        }

        // dungeonItems[]: boss key / compass / map are never lost (union). Small keys are consumable
        // (dungeonKeys[]) and stay local.
        for (int i = 0; i < ARRAY_COUNT(gSaveContext.inventory.dungeonItems); i++) {
            gSaveContext.inventory.dungeonItems[i] |= loadedData.inventory.dungeonItems[i];
        }

        // defenseHearts is the double-defense count (0/20), monotonic.
        if (loadedData.inventory.defenseHearts > gSaveContext.inventory.defenseHearts) {
            gSaveContext.inventory.defenseHearts = loadedData.inventory.defenseHearts;
        }

        // gsTokens is the collected-skulltula count; derive it from the already-unioned gsFlags so the
        // count and the collected-set never disagree.
        {
            s16 tokens = 0;
            for (int i = 0; i < 6; i++) {
                u32 bits = (u32)gSaveContext.gsFlags[i];
                while (bits) {
                    tokens += bits & 1;
                    bits >>= 1;
                }
            }
            if (tokens > gSaveContext.inventory.gsTokens) {
                gSaveContext.inventory.gsTokens = tokens;
            }
        }

        // The commented out code below is an attempt at sending the entire randomizer seed over, in hopes that a player
        // doesn't have to generate the seed themselves Currently it doesn't work :)
        if (IS_RANDO && payload["state"].contains("rando")) {
            auto randoContext = Rando::Context::GetInstance();

            for (int i = 0; i < RC_MAX; i++) {
                auto itemLocation = payload["state"]["rando"].at("itemLocations").at(i);
                // randoContext->GetItemLocation(i)->RefPlacedItem() =
                // itemLocation.at("rgID").get<RandomizerGet>();
                OTRGlobals::Instance->gRandoContext->GetItemLocation(i)->SetCheckStatus(
                    itemLocation.at(0).get<RandomizerCheckStatus>());
                OTRGlobals::Instance->gRandoContext->GetItemLocation(i)->SetIsSkipped(itemLocation.at(1).get<u8>());

                // if (itemLocation.contains("fakeRgID")) {
                //     randoContext->overrides.emplace(static_cast<RandomizerCheck>(i),
                //     Rando::ItemOverride(static_cast<RandomizerCheck>(i),
                //     itemLocation.at("fakeRgID").get<RandomizerGet>()));
                //     randoContext->GetItemOverride(i).GetTrickName().english =
                //     itemLocation.at("trickName").at("english").get<std::string>();
                //     randoContext->GetItemOverride(i).GetTrickName().french =
                //     itemLocation.at("trickName").at("french").get<std::string>();
                // }
                // if (itemLocation.contains("price")) {
                //     u16 price = itemLocation.at("price"].get<u16>();
                //     if (price > 0) {
                //         randoContext->GetItemLocation(i)->SetCustomPrice(price);
                //     }
                // }
            }

            // auto entranceCtx = randoContext->GetEntranceShuffler();
            // for (int i = 0; i < ENTRANCE_OVERRIDES_MAX_COUNT; i++) {
            //     entranceCtx->entranceOverrides[i].type =
            //     payload["state"]["rando"]["entrances"][i]["type"].get<u16>(); entranceCtx->entranceOverrides[i].index
            //     = payload["state"]["rando"]["entrances"][i]["index"].get<s16>();
            //     entranceCtx->entranceOverrides[i].destination =
            //     payload["state"]["rando"]["entrances"][i]["destination"].get<s16>();
            //     entranceCtx->entranceOverrides[i].override =
            //     payload["state"]["rando"]["entrances"][i]["override"].get<s16>();
            //     entranceCtx->entranceOverrides[i].overrideDestination =
            //     payload["state"]["rando"]["entrances"][i]["overrideDestination"].get<s16>();
            // }

            // for (int i = 0; i < randoContext->hashIconIndexes.size(); i++) {
            //     randoContext->hashIconIndexes[i] = payload["state"]["rando"]["seed"][i].get<u8>();
            // }
            // randoContext->GetSettings()->SetSeedString(payload["state"]["rando"]["inputSeed"].get<std::string>());
            // randoContext->GetSettings()->SetSeed(payload["state"]["rando"]["finalSeed"].get<u32>());

            // for (int i = 0; i < RSK_MAX; i++) {
            //     randoContext->GetOption(RandomizerSettingKey(i)).SetSelectedIndex(payload["state"]["rando"]["randoSettings"][i].get<u8>());
            // }

            // randoContext->GetDungeons()->ClearAllMQ();
            // for (int i = 0; i < randoContext->GetDungeons()->GetDungeonListSize(); i++) {
            //     if (payload["state"]["rando"]["masterQuestDungeons"][i].get<bool>()) {
            //         randoContext->GetDungeon(i)->SetMQ();
            //     }
            // }

            // randoContext->GetTrials()->SkipAll();
            // for (int i = 0; i < randoContext->GetTrials()->GetTrialListSize(); i++) {
            //     if (payload["state"]["rando"]["requiredTrials"][i].get<bool>()) {
            //         randoContext->GetTrial(i)->SetAsRequired();
            //     }
            // }
        }

        Notification::Emit({
            .message = "Save updated from team",
        });
    }

    if (payload.contains("queue")) {
        std::lock_guard<std::mutex> lock(incomingPacketQueueMutex);
        for (auto& item : payload["queue"]) {
            nlohmann::json itemPayload = nlohmann::json::parse(item.get<std::string>());
            incomingPacketQueue.push(itemPayload);
        }
    }
    isHandlingUpdateTeamState = false;
}
