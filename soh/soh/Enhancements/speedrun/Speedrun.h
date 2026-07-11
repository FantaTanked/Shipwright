#pragma once

#include <libultraship/libultra/types.h>

#ifdef __cplusplus

#include <array>

namespace Speedrun {
extern std::array<uint8_t, 5> hashIconIndexes;
}

extern "C" {
#endif

struct GameState;
struct FileChooseContext;

void FileChoose_UpdateSpeedrunMenu(struct GameState* gameState);
void FileChoose_DrawSpeedrunMenuWindowContents(struct FileChooseContext* fileChooseContext);
const char* Speedrun_GetSettingName(u8 optionIndex, u8 language);
const char* Speedrun_GetSettingChoiceName(u8 optionIndex, u8 choiceIndex, u8 language);
u8 Speedrun_GetSettingOptionsAmount(u8 optionIndex);
void Speedrun_ApplyRuleset();
void Speedrun_RestoreSettings();
void Speedrun_GenerateSettingsHash();

#ifdef __cplusplus
}
#endif

#define SPEEDRUN_MAX_OPTIONS_ON_SCREEN 1

typedef enum {
    SR_OPTIONS_CATEGORY,
    SR_OPTIONS_MAX,
} SpeedrunOptionEnums;