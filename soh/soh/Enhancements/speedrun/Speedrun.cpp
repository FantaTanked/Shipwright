#include <array>
#include <vector>
#include <string>
#include <z64.h>
#include "Speedrun.h"
#include <macros.h>
#include "soh/Enhancements/game-interactor/GameInteractor_Hooks.h"
#include "soh/frame_interpolation.h"
#include "soh_assets.h"
#include "soh/SaveManager.h"
#include <soh/ResourceManagerHelpers.h>
#include <ship/Context.h>
#include <ship/resource/ResourceManager.h>
#include <ship/resource/type/Json.h>
#include <nlohmann/json.hpp>
#include <ship/config/Config.h>
#include <ship/config/ConsoleVariable.h>
#include <soh/ShipInit.hpp>
#include <soh/util.h>

extern "C" {
#include "functions.h"
#include "variables.h"
#include "src/overlays/gamestates/ovl_file_choose/file_choose.h"
#include "objects/gameplay_keep/gameplay_keep.h"

SaveFileMetaInfo* Save_GetSaveMetaInfo(int fileNum);

void FileChoose_UpdateStickDirectionPromptAnim(GameState* thisx);
void FileChoose_DrawTextRec(GraphicsContext* gfxCtx, s32 r, s32 g, s32 b, s32 a, f32 x, f32 y, f32 z, s32 s, s32 t,
                            f32 dx, f32 dy);
}

typedef enum {
    SR_CHOICE_CATEGORIES,
} SpeedrunChoices;

typedef struct SpeedrunSetting {
    std::array<std::string, LANGUAGE_MAX> name;
    std::vector<std::array<std::string, LANGUAGE_MAX>> choices;
} SpeedrunSetting;

SpeedrunSetting SpeedrunOptions[SR_OPTIONS_MAX] = { { { "Category:", "Kategorie:", "Catégorie:" },
                                                      {
                                                          { "Any%" },
                                                          { "100%" },
                                                          { "All Dungeons" },
                                                          { "GSR" },
                                                          { "MST" },
                                                          { "Glitchless" },
                                                          { "Defeat Ganon" },
                                                          { "No Wrong Warp" },
                                                          { "Nocturne RTA" },
                                                      } } };

static const std::array<const char*, 9> categoryIds = {
    "any_percent", "one_hundred_percent", "all_dungeons",  "gsr",          "mst",
    "glitchless",  "defeat_ganon",        "no_wrong_warp", "nocturne_rta",
};

namespace Speedrun {
std::array<uint8_t, 5> hashIconIndexes = {};
}

static constexpr const char* SPEEDRUN_BACKUP_BLOCK = "SpeedrunSettingsBackup";

const char* Speedrun_GetSettingName(u8 optionIndex, u8 language) {
    return SpeedrunOptions[optionIndex].name[language].c_str();
}

const char* Speedrun_GetSettingChoiceName(u8 optionIndex, u8 choiceIndex, u8 language) {
    return SpeedrunOptions[optionIndex].choices[choiceIndex][language].c_str();
}

u8 Speedrun_GetSettingOptionsAmount(u8 optionIndex) {
    return static_cast<u8>(SpeedrunOptions[optionIndex].choices.size());
}

void FileChoose_UpdateSpeedrunMenu(GameState* gameState) {
    static s8 sLastSpeedrunOptionIndex = -1;
    static s8 sLastSpeedrunOptionValue = -1;

    FileChoose_UpdateStickDirectionPromptAnim(gameState);
    FileChooseContext* fileChooseContext = (FileChooseContext*)gameState;
    Input* input = &fileChooseContext->state.input[0];
    bool dpad = CVarGetInteger(CVAR_SETTING("DpadInText"), 0);

    // Fade in elements after opening Speedrun options menu
    fileChooseContext->speedrunUIAlpha += 25;
    if (fileChooseContext->speedrunUIAlpha > 255) {
        fileChooseContext->speedrunUIAlpha = 255;
    }

    // Animate up/down arrows.
    fileChooseContext->speedrunArrowOffset += 1;
    if (fileChooseContext->speedrunArrowOffset >= 30) {
        fileChooseContext->speedrunArrowOffset = 0;
    }

    // Move menu selection up or down.
    if (ABS(fileChooseContext->stickRelY) > 30 || (dpad && CHECK_BTN_ANY(input->press.button, BTN_DDOWN | BTN_DUP))) {
        // Move down
        if (fileChooseContext->stickRelY < -30 || (dpad && CHECK_BTN_ANY(input->press.button, BTN_DDOWN))) {
            // When selecting past the last option, cycle back to the first option.
            if ((fileChooseContext->speedrunIndex + 1) > SR_OPTIONS_MAX - 1) {
                fileChooseContext->speedrunIndex = 0;
                fileChooseContext->speedrunOffset = 0;
            } else {
                fileChooseContext->speedrunIndex++;
                // When last visible option is selected when moving down, offset the list down by one.
                if (fileChooseContext->speedrunIndex - fileChooseContext->speedrunOffset >
                    SPEEDRUN_MAX_OPTIONS_ON_SCREEN - 1) {
                    fileChooseContext->speedrunOffset++;
                }
            }
        } else if (fileChooseContext->stickRelY > 30 || (dpad && CHECK_BTN_ANY(input->press.button, BTN_DUP))) {
            // When selecting past the first option, cycle back to the last option and offset the list to view it
            // properly.
            if ((fileChooseContext->speedrunIndex - 1) < 0) {
                fileChooseContext->speedrunIndex = SR_OPTIONS_MAX - 1;
                fileChooseContext->speedrunOffset =
                    fileChooseContext->speedrunIndex - SPEEDRUN_MAX_OPTIONS_ON_SCREEN + 1;
            } else {
                // When first visible option is selected when moving up, offset the list up by one.
                if (fileChooseContext->speedrunIndex - fileChooseContext->speedrunOffset == 0) {
                    fileChooseContext->speedrunOffset--;
                }
                fileChooseContext->speedrunIndex--;
            }
        }

        Audio_PlaySoundGeneral(NA_SE_SY_FSEL_CURSOR, &gSfxDefaultPos, 4, &gSfxDefaultFreqAndVolScale,
                               &gSfxDefaultFreqAndVolScale, &gSfxDefaultReverb);
    }

    // Cycle through choices for currently selected option.
    if (ABS(fileChooseContext->stickRelX) > 30 ||
        (dpad && CHECK_BTN_ANY(input->press.button, BTN_DLEFT | BTN_DRIGHT))) {
        if (fileChooseContext->stickRelX > 30 || (dpad && CHECK_BTN_ANY(input->press.button, BTN_DRIGHT))) {
            // If exceeding the amount of choices for the selected option, cycle back to the first.
            if ((gSaveContext.ship.quest.data.speedrun.options[fileChooseContext->speedrunIndex] + 1) ==
                Speedrun_GetSettingOptionsAmount(fileChooseContext->speedrunIndex)) {
                gSaveContext.ship.quest.data.speedrun.options[fileChooseContext->speedrunIndex] = 0;
            } else {
                gSaveContext.ship.quest.data.speedrun.options[fileChooseContext->speedrunIndex]++;
            }
        } else if (fileChooseContext->stickRelX < -30 || (dpad && CHECK_BTN_ANY(input->press.button, BTN_DLEFT))) {
            // If cycling back when already at the first choice for the selected option, cycle back to the last choice.
            if ((gSaveContext.ship.quest.data.speedrun.options[fileChooseContext->speedrunIndex] - 1) < 0) {
                gSaveContext.ship.quest.data.speedrun.options[fileChooseContext->speedrunIndex] =
                    Speedrun_GetSettingOptionsAmount(fileChooseContext->speedrunIndex) - 1;
            } else {
                gSaveContext.ship.quest.data.speedrun.options[fileChooseContext->speedrunIndex]--;
            }
        }

        Audio_PlaySoundGeneral(NA_SE_SY_FSEL_CURSOR, &gSfxDefaultPos, 4, &gSfxDefaultFreqAndVolScale,
                               &gSfxDefaultFreqAndVolScale, &gSfxDefaultReverb);
    }

    if (sLastSpeedrunOptionIndex != fileChooseContext->speedrunIndex ||
        sLastSpeedrunOptionValue != gSaveContext.ship.quest.data.speedrun.options[fileChooseContext->speedrunIndex]) {
        GameInteractor_ExecuteOnUpdateFileSpeedrunOptionSelection(
            fileChooseContext->speedrunIndex,
            gSaveContext.ship.quest.data.speedrun.options[fileChooseContext->speedrunIndex]);
        sLastSpeedrunOptionIndex = fileChooseContext->speedrunIndex;
        sLastSpeedrunOptionValue = gSaveContext.ship.quest.data.speedrun.options[fileChooseContext->speedrunIndex];
    }

    if (CHECK_BTN_ALL(input->press.button, BTN_B)) {
        fileChooseContext->configMode = CM_SPEEDRUN_TO_QUEST;
        return;
    }

    if (CHECK_BTN_ALL(input->press.button, BTN_A)) {
        Audio_PlaySoundGeneral(NA_SE_SY_FSEL_DECIDE_L, &gSfxDefaultPos, 4, &gSfxDefaultFreqAndVolScale,
                               &gSfxDefaultFreqAndVolScale, &gSfxDefaultReverb);
        static u8 emptyName[] = { 0x3E, 0x3E, 0x3E, 0x3E, 0x3E, 0x3E, 0x3E, 0x3E };
        static u8 emptyNameNES[] = { 0xDF, 0xDF, 0xDF, 0xDF, 0xDF, 0xDF, 0xDF, 0xDF };
        static u8 linkName[] = { 0x15, 0x2C, 0x31, 0x2E, 0x3E, 0x3E, 0x3E, 0x3E };
        static u8 linkNameNES[] = { 0xB6, 0xCD, 0xD2, 0xCF, 0xDF, 0xDF, 0xDF, 0xDF };
        static u8 linkNameJP[] = { 0x81, 0x87, 0x61, 0xDF, 0xDF, 0xDF, 0xDF, 0xDF };
        u8* defaultName;

        fileChooseContext->prevConfigMode = fileChooseContext->configMode;
        fileChooseContext->configMode = CM_ROTATE_TO_NAME_ENTRY;
        fileChooseContext->logoAlpha = 0;
        CVarSetInteger(CVAR_GENERAL("OnFileSelectNameEntry"), 1);
        fileChooseContext->kbdButton = FS_KBD_BTN_NONE;
        fileChooseContext->charPage = FS_CHAR_PAGE_ENG;
        fileChooseContext->kbdX = 0;
        fileChooseContext->kbdY = 0;
        fileChooseContext->charIndex = 0;
        fileChooseContext->charBgAlpha = 0;
        fileChooseContext->newFileNameCharCount = CVarGetInteger(CVAR_ENHANCEMENT("LinkDefaultName"), 0) ? 4 : 0;
        fileChooseContext->nameEntryBoxPosX = 120;
        fileChooseContext->nameEntryBoxAlpha = 0;
        if (ResourceMgr_GetGameRegion(0) == GAME_REGION_PAL && gSaveContext.language != LANGUAGE_JPN) {
            defaultName = CVarGetInteger(CVAR_ENHANCEMENT("LinkDefaultName"), 0) ? linkName : emptyName;
        } else if (gSaveContext.language == LANGUAGE_JPN) { // Japanese
            if (CVarGetInteger(CVAR_ENHANCEMENT("LinkDefaultName"), 0) != 0) {
                // Set player name to "リンク" ("Link" in Katakana, 3 characters long) when playing in Japanese.
                defaultName = linkNameJP;
                fileChooseContext->newFileNameCharCount = 3;
            } else {
                defaultName = emptyNameNES;
            }
            fileChooseContext->charPage = FS_CHAR_PAGE_HIRA; // Default to Hiragana Keyboard
        } else {                                             // GAME_REGION_NTSC
            defaultName = CVarGetInteger(CVAR_ENHANCEMENT("LinkDefaultName"), 0) ? linkNameNES : emptyNameNES;
        }
        memcpy(Save_GetSaveMetaInfo(fileChooseContext->buttonIndex)->playerName, defaultName, 8);
    }
}

void FileChoose_DrawSpeedrunMenuWindowContents(FileChooseContext* fileChooseContext) {
    OPEN_DISPS(fileChooseContext->state.gfxCtx);

    uint8_t language = (gSaveContext.language == LANGUAGE_JPN) ? LANGUAGE_ENG : gSaveContext.language;
    uint8_t listOffset = fileChooseContext->speedrunOffset;
    int16_t textAlpha = fileChooseContext->speedrunUIAlpha;

    // Draw arrows to indicate that the list can scroll up or down.
    // Arrow up
    if (listOffset > 0) {
        uint16_t arrowUpX = 140;
        uint16_t arrowUpY = 76 - (fileChooseContext->speedrunArrowOffset / 10);
        gDPLoadTextureBlock(POLY_OPA_DISP++, gArrowUpTex, G_IM_FMT_IA, G_IM_SIZ_16b, 16, 16, 0,
                            G_TX_NOMIRROR | G_TX_WRAP, G_TX_NOMIRROR | G_TX_WRAP, G_TX_NOMASK, G_TX_NOMASK, G_TX_NOLOD,
                            G_TX_NOLOD);
        gSPWideTextureRectangle(POLY_OPA_DISP++, arrowUpX << 2, arrowUpY << 2, (arrowUpX + 8) << 2, (arrowUpY + 8) << 2,
                                G_TX_RENDERTILE, 0, 0, (1 << 11), (1 << 11));
    }
    // Arrow down
    if (SR_OPTIONS_MAX - listOffset > SPEEDRUN_MAX_OPTIONS_ON_SCREEN) {
        uint16_t arrowDownX = 140;
        uint16_t arrowDownY = 181 + (fileChooseContext->speedrunArrowOffset / 10);
        gDPLoadTextureBlock(POLY_OPA_DISP++, gArrowDownTex, G_IM_FMT_IA, G_IM_SIZ_16b, 16, 16, 0,
                            G_TX_NOMIRROR | G_TX_WRAP, G_TX_NOMIRROR | G_TX_WRAP, G_TX_NOMASK, G_TX_NOMASK, G_TX_NOLOD,
                            G_TX_NOLOD);
        gSPWideTextureRectangle(POLY_OPA_DISP++, arrowDownX << 2, arrowDownY << 2, (arrowDownX + 8) << 2,
                                (arrowDownY + 8) << 2, G_TX_RENDERTILE, 0, 0, (1 << 11), (1 << 11));
    }

    // Draw options. There's more options than what fits on the screen, so the visible options
    // depend on the current offset of the list. Currently selected option pulses in
    // color and has arrows surrounding the option.
    for (uint8_t i = listOffset; i - listOffset < SPEEDRUN_MAX_OPTIONS_ON_SCREEN; i++) {
        uint16_t textYOffset = (i - listOffset) * 16;

        // Option name.
        Interface_DrawTextLine(fileChooseContext->state.gfxCtx, (char*)Speedrun_GetSettingName(i, language), 65,
                               (87 + textYOffset), 255, 255, 80, textAlpha, 0.8f, true);

        // Selected choice for option.
        uint16_t finalKerning = Interface_DrawTextLine(
            fileChooseContext->state.gfxCtx,
            (char*)Speedrun_GetSettingChoiceName(i, gSaveContext.ship.quest.data.speedrun.options[i], language), 165,
            (87 + textYOffset), 255, 255, 255, textAlpha, 0.8f, true);

        // Draw arrows around selected option.
        if (fileChooseContext->speedrunIndex == i) {
            Gfx_SetupDL_39Opa(fileChooseContext->state.gfxCtx);
            gDPSetCombineMode(POLY_OPA_DISP++, G_CC_MODULATEIA_PRIM, G_CC_MODULATEIA_PRIM);
            gDPLoadTextureBlock(POLY_OPA_DISP++, gArrowCursorTex, G_IM_FMT_IA, G_IM_SIZ_8b, 16, 24, 0,
                                G_TX_NOMIRROR | G_TX_WRAP, G_TX_NOMIRROR | G_TX_WRAP, 4, G_TX_NOMASK, G_TX_NOLOD,
                                G_TX_NOLOD);
            FileChoose_DrawTextRec(fileChooseContext->state.gfxCtx, fileChooseContext->stickLeftPrompt.arrowColorR,
                                   fileChooseContext->stickLeftPrompt.arrowColorG,
                                   fileChooseContext->stickLeftPrompt.arrowColorB, textAlpha, 160.0f,
                                   static_cast<f32>(92 + textYOffset), 0.42f, 0, 0, -1.0f, 1.0f);
            FileChoose_DrawTextRec(fileChooseContext->state.gfxCtx, fileChooseContext->stickRightPrompt.arrowColorR,
                                   fileChooseContext->stickRightPrompt.arrowColorG,
                                   fileChooseContext->stickRightPrompt.arrowColorB, textAlpha,
                                   static_cast<f32>(171 + finalKerning), static_cast<f32>(92 + textYOffset), 0.42f, 0,
                                   0, 1.0f, 1.0f);
        }
    }

    CLOSE_DISPS(fileChooseContext->state.gfxCtx);
}

static void Speedrun_ApplyCVarValues(const nlohmann::json& values) {
    for (const auto& [cvarName, value] : values.items()) {
        if (value.is_boolean()) {
            CVarSetInteger(cvarName.c_str(), value.get<bool>() ? 1 : 0);
        } else if (value.is_number_integer()) {
            CVarSetInteger(cvarName.c_str(), value.get<int32_t>());
        } else if (value.is_number_float()) {
            CVarSetFloat(cvarName.c_str(), value.get<float>());
        } else if (value.is_string()) {
            CVarSetString(cvarName.c_str(), value.get<std::string>().c_str());
        }
    }
}

static nlohmann::json Speedrun_SnapshotCVar(const std::string& name) {
    auto cvar = CVarGet(name.c_str());

    if (cvar == nullptr) {
        return nullptr;
    }

    nlohmann::json result;

    switch (cvar->Type) {
        case Ship::ConsoleVariableType::Integer:
            result["type"] = "integer";
            result["value"] = cvar->Integer;
            break;
        case Ship::ConsoleVariableType::Float:
            result["type"] = "float";
            result["value"] = cvar->Float;
            break;
        case Ship::ConsoleVariableType::String:
            result["type"] = "string";
            result["value"] = cvar->String;
            break;
        default:
            return nullptr;
    }

    return result;
}

static void Speedrun_BackupSettings(const nlohmann::json& base) {
    auto config = Ship::Context::GetRawInstance()->GetConfig();
    auto currentConfig = config->GetNestedJson();

    if (currentConfig.contains(SPEEDRUN_BACKUP_BLOCK)) {
        return;
    }

    nlohmann::json backup = nlohmann::json::object();

    for (const auto& [name, value] : base.items()) {
        backup[name] = Speedrun_SnapshotCVar(name);
    }

    config->SetBlock(SPEEDRUN_BACKUP_BLOCK, backup);
}

void Speedrun_GenerateSettingsHash() {
    auto initData = std::make_shared<Ship::ResourceInitData>();
    initData->Format = RESOURCE_FORMAT_BINARY;
    initData->Type = static_cast<uint32_t>(Ship::ResourceType::Json);
    initData->ResourceVersion = 0;

    auto resource =
        std::static_pointer_cast<Ship::Json>(Ship::Context::GetRawInstance()->GetResourceManager()->LoadResource(
            "speedrun/SpeedrunRulesets.json", true, initData));

    if (resource == nullptr) {
        return;
    }
    const nlohmann::json& rulesets = resource->Data;

    uint8_t category = gSaveContext.ship.quest.data.speedrun.options[SR_OPTIONS_CATEGORY];

    nlohmann::json effectiveSettings = rulesets["base"];
    const std::string categoryId = categoryIds[category];

    for (const auto& categoryRules : rulesets["categories"]) {
        if (categoryRules.value("id", "") == categoryId) {
            effectiveSettings.update(categoryRules["overrides"]);
            break;
        }
    }

    nlohmann::json hashInput = {
        { "category", categoryId },
        { "settings", effectiveSettings },
    };

    uint32_t hash = SohUtils::Hash(hashInput.dump());

    for (int i = 4; i >= 0; i--) {
        Speedrun::hashIconIndexes[i] = hash % 100;
        hash /= 100;
    }
}

void Speedrun_ApplyRuleset() {
    auto initData = std::make_shared<Ship::ResourceInitData>();
    initData->Format = RESOURCE_FORMAT_BINARY;
    initData->Type = static_cast<uint32_t>(Ship::ResourceType::Json);
    initData->ResourceVersion = 0;

    auto resource =
        std::static_pointer_cast<Ship::Json>(Ship::Context::GetRawInstance()->GetResourceManager()->LoadResource(
            "speedrun/SpeedrunRulesets.json", true, initData));

    if (resource == nullptr) {
        return;
    }

    const nlohmann::json& rulesets = resource->Data;

    Speedrun_BackupSettings(rulesets["base"]);
    Speedrun_ApplyCVarValues(rulesets["base"]);

    uint8_t category = gSaveContext.ship.quest.data.speedrun.options[SR_OPTIONS_CATEGORY];

    if (category < categoryIds.size() && rulesets.contains("categories")) {
        for (const auto& categoryRules : rulesets["categories"]) {
            if (categoryRules.value("id", "") == categoryIds[category]) {
                if (categoryRules.contains("overrides")) {
                    Speedrun_ApplyCVarValues(categoryRules["overrides"]);
                }

                break;
            }
        }
    }

    ShipInit::InitAll();

    CVarSetInteger(CVAR_SETTING("DisableChanges"), 1);
}

void Speedrun_RestoreSettings() {
    auto config = Ship::Context::GetRawInstance()->GetConfig();
    auto currentConfig = config->GetNestedJson();

    if (!currentConfig.contains(SPEEDRUN_BACKUP_BLOCK)) {
        return;
    }

    const auto backup = currentConfig[SPEEDRUN_BACKUP_BLOCK];

    for (const auto& [name, saved] : backup.items()) {
        if (saved.is_null()) {
            CVarClear(name.c_str());
            continue;
        }

        const std::string type = saved.value("type", "");

        if (type == "integer") {
            CVarSetInteger(name.c_str(), saved["value"].get<int32_t>());
        } else if (type == "float") {
            CVarSetFloat(name.c_str(), saved["value"].get<float>());
        } else if (type == "string") {
            CVarSetString(name.c_str(), saved["value"].get<std::string>().c_str());
        }
    }

    config->EraseBlock(SPEEDRUN_BACKUP_BLOCK);

    CVarSetInteger(CVAR_SETTING("DisableChanges"), 0);
    CVarSave();

    ShipInit::InitAll();
}

void RegisterSpeedrunHooks() {
    COND_HOOK(OnExitGame, true, [](int32_t fileNum) {
        if (gSaveContext.ship.quest.id == QUEST_SPEEDRUN) {
            Speedrun_RestoreSettings();
        }
    });

    COND_HOOK(OnZTitleInit, true, [](void* gameState) { Speedrun_RestoreSettings(); });
}

static RegisterShipInitFunc initFunc(RegisterSpeedrunHooks);

extern "C" void Speedrun_InitSave() {
    gSaveContext.ship.quest.id = QUEST_SPEEDRUN;
    Speedrun_ApplyRuleset();
}