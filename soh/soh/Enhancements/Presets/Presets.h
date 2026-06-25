#pragma once

#include <string>
#include <vector>
#include <nlohmann/json_fwd.hpp>

enum PresetSection {
    PRESET_SECTION_SETTINGS,
    PRESET_SECTION_ENHANCEMENTS,
    PRESET_SECTION_AUDIO,
    PRESET_SECTION_COSMETICS,
    PRESET_SECTION_RANDOMIZER,
    PRESET_SECTION_TRACKERS,
    PRESET_SECTION_NETWORK,
    PRESET_SECTION_MAX,
};

void DrawPresetSelector(std::vector<PresetSection> includeSections, std::string currentIndex, bool disabled);
void applyPreset(std::string presetName, std::vector<PresetSection> includeSections = {});
// Apply a preset straight from its parsed JSON (ignores the UI per-section toggles).
void ApplyPresetJson(const nlohmann::json& presetValues, std::vector<PresetSection> includeSections = {});
// Look up a loaded preset by its source filename stem (e.g. "speedrun" for speedrun.json). Returns nullptr if absent.
const nlohmann::json* FindPresetByFileName(const std::string& fileName);
