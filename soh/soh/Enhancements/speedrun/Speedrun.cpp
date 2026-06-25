#include "Speedrun.h"

#include "soh/ShipInit.hpp"
#include "soh/OTRGlobals.h"
#include "soh/Enhancements/Presets/Presets.h"
#include "soh/Enhancements/game-interactor/GameInteractor.h"
#include "soh/SohGui/MenuTypes.h"

#include <libultraship/libultraship.h>
#include <libultraship/bridge.h>
#include <ship/config/Config.h>
#include <nlohmann/json.hpp>
#include <spdlog/spdlog.h>

#include <filesystem>
#include <fstream>
#include <string>

extern "C" {
#include "functions.h"
#include "macros.h"
#include "variables.h"
}

// The speedrun preset is loaded as an ordinary preset from speedrun.json. We key off the source
// filename (not the presetName) so the file can be renamed/edited freely and still be found.
static const char* kSpeedrunPresetFileName = "speedrun";

// Backup file holding the player's personal config blocks captured the moment they entered speedrun
// mode, so it can be restored when they leave. Persisted so it also survives a crash mid-run.
static const char* kSpeedrunBackupFileName = "speedrun_config_backup.json";

// Gameplay cvar blocks speedrun mode owns: cleared to vanilla, then the preset layered on top, and
// backed up/restored. Apply is restricted to these too, so nothing it sets escapes restore.
static const char* kSpeedrunManagedBlocks[] = { "gEnhancements", "gCheats", "gRandoEnhancements" };

static bool Speedrun_IsManagedBlock(const std::string& block) {
    for (const char* managed : kSpeedrunManagedBlocks) {
        if (block == managed) {
            return true;
        }
    }
    return false;
}

static std::string Speedrun_BackupPath() {
    return Ship::Context::GetRawInstance()->GetPathRelativeToAppDirectory(kSpeedrunBackupFileName);
}

// Recursively set every leaf of `obj` as a cvar under `prefix`. This mutates the cvar store one key
// at a time (like every other in-game setting change). It deliberately avoids the preset Load()
// path, which clears and rebuilds the ENTIRE cvar map and would race the audio thread reading cvars.
static void Speedrun_SetCVarLeaves(const std::string& prefix, const nlohmann::json& obj) {
    for (auto it = obj.begin(); it != obj.end(); ++it) {
        std::string path = prefix.empty() ? it.key() : prefix + "." + it.key();
        const nlohmann::json& val = it.value();
        if (val.is_object()) {
            Speedrun_SetCVarLeaves(path, val);
        } else if (val.is_boolean()) {
            CVarSetInteger(path.c_str(), val.get<bool>() ? 1 : 0);
        } else if (val.is_number_float()) {
            CVarSetFloat(path.c_str(), val.get<float>());
        } else if (val.is_number_integer() || val.is_number_unsigned()) {
            CVarSetInteger(path.c_str(), val.get<int>());
        } else if (val.is_string()) {
            CVarSetString(path.c_str(), val.get<std::string>().c_str());
        }
    }
}

// Recursively clear every leaf cvar under `prefix` described by `obj` (one key at a time).
static void Speedrun_ClearCVarLeaves(const std::string& prefix, const nlohmann::json& obj) {
    for (auto it = obj.begin(); it != obj.end(); ++it) {
        std::string path = prefix.empty() ? it.key() : prefix + "." + it.key();
        const nlohmann::json& val = it.value();
        if (val.is_object()) {
            Speedrun_ClearCVarLeaves(path, val);
        } else {
            CVarClear(path.c_str());
        }
    }
}

// The live "CVars" config object (empty if absent). Deep-copies the config, so fetch once per op.
static nlohmann::json Speedrun_LiveCVars() {
    auto nested = Ship::Context::GetRawInstance()->GetConfig()->GetNestedJson();
    return nested.contains("CVars") ? nested["CVars"] : nlohmann::json::object();
}

// Invoke fn(blockName, blockJson) for each managed block present in `cvars`. Drives clear/snapshot
// off one definition so the blocks they touch stay symmetric.
template <typename Fn>
static void Speedrun_ForEachPresentManagedBlock(const nlohmann::json& cvars, Fn fn) {
    for (const char* block : kSpeedrunManagedBlocks) {
        if (cvars.contains(block)) {
            fn(block, cvars[block]);
        }
    }
}

// Reset the managed gameplay blocks to vanilla by clearing each of their currently-set keys.
static void Speedrun_ClearManagedBlocks(const nlohmann::json& cvars) {
    Speedrun_ForEachPresentManagedBlock(cvars, [](const char* block, const nlohmann::json& blockJson) {
        Speedrun_ClearCVarLeaves(block, blockJson);
    });
}

// Snapshot the player's current managed blocks to disk, unless a snapshot already exists (i.e. we
// are already inside a speedrun session and must not clobber the real backup).
static void Speedrun_SnapshotConfigIfNeeded(const nlohmann::json& cvars) {
    std::string path = Speedrun_BackupPath();
    if (std::filesystem::exists(path)) {
        return;
    }

    nlohmann::json backup = nlohmann::json::object();
    Speedrun_ForEachPresentManagedBlock(cvars, [&backup](const char* block, const nlohmann::json& blockJson) {
        backup[block] = blockJson;
    });

    std::ofstream ofs(path);
    if (ofs.is_open()) {
        ofs << backup.dump(4);
        ofs.close();
    } else {
        SPDLOG_ERROR("Speedrun: could not write config backup to {}", path);
    }
}

// Reset gameplay settings to vanilla and apply the locked speedrun preset on top.
static void Speedrun_ApplyConfig() {
    nlohmann::json cvars = Speedrun_LiveCVars();
    Speedrun_SnapshotConfigIfNeeded(cvars);

    // Anything not listed in the preset reverts to its vanilla default.
    Speedrun_ClearManagedBlocks(cvars);

    const nlohmann::json* preset = FindPresetByFileName(kSpeedrunPresetFileName);
    if (preset != nullptr && preset->contains("blocks")) {
        // Only apply managed blocks; anything the preset lists outside them (e.g. gGameplayStats) is
        // skipped, since restore only undoes managed blocks and it would otherwise leak past exit.
        for (auto sectionIt = (*preset)["blocks"].begin(); sectionIt != (*preset)["blocks"].end(); ++sectionIt) {
            const nlohmann::json& section = sectionIt.value();
            for (auto blockIt = section.begin(); blockIt != section.end(); ++blockIt) {
                if (Speedrun_IsManagedBlock(blockIt.key())) {
                    Speedrun_SetCVarLeaves(blockIt.key(), blockIt.value());
                }
            }
        }
    } else {
        SPDLOG_WARN("Speedrun: speedrun.json preset not found; applied a vanilla baseline only.");
    }

    CVarSave();
    ShipInit::InitAll();
    OTRGlobals::Instance->ScaleImGui();
}

// Restore the player's pre-speedrun config from the backup (if any) and delete the backup.
static void Speedrun_RestoreConfig() {
    std::string path = Speedrun_BackupPath();
    if (!std::filesystem::exists(path)) {
        return;
    }

    try {
        std::ifstream ifs(path);
        nlohmann::json backup = nlohmann::json::parse(ifs);
        ifs.close();

        Speedrun_ClearManagedBlocks(Speedrun_LiveCVars());
        for (const char* block : kSpeedrunManagedBlocks) {
            if (backup.contains(block) && backup[block].is_object()) {
                Speedrun_SetCVarLeaves(block, backup[block]);
            }
        }

        CVarSave();
        ShipInit::InitAll();
        OTRGlobals::Instance->ScaleImGui();
    } catch (const std::exception& e) {
        SPDLOG_ERROR("Speedrun: failed to restore pre-speedrun config: {}", e.what());
    }

    std::filesystem::remove(path);
}

extern "C" bool Speedrun_IsLockActive(void) {
    // Derive from live state, not a cached flag: active only while a speedrun save is loaded, so the
    // menu unlocks as soon as you leave the run without depending on a specific exit hook firing.
    return GameInteractor::IsSaveLoaded(true) && IS_SPEEDRUN;
}

static void Speedrun_RegisterHooks() {
    GameInteractor::Instance->RegisterGameHook<GameInteractor::OnLoadGame>([](int32_t fileNum) {
        if (IS_SPEEDRUN) {
            Speedrun_ApplyConfig();
        } else {
            // Loaded a non-speedrun file: restore the player's config if a backup is pending.
            Speedrun_RestoreConfig();
        }
    });

    GameInteractor::Instance->RegisterGameHook<GameInteractor::OnExitGame>([](int32_t fileNum) {
        Speedrun_RestoreConfig();
    });
}

// Runs at startup once the config/context is ready: recovers the player's config if a speedrun
// session was interrupted by a crash (a leftover backup file).
static void Speedrun_CrashRecovery() {
    Speedrun_RestoreConfig();
}

static RegisterShipInitFunc speedrunInitFunc(Speedrun_RegisterHooks);
static RegisterMenuInitFunc speedrunMenuInitFunc(Speedrun_CrashRecovery);
