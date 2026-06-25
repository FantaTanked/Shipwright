#ifndef SAVESTATE_FILEDIALOG_H
#define SAVESTATE_FILEDIALOG_H

#include <string>

// Thin wrappers around the native file dialog, in their own translation unit because the dialog library pulls
// in Windows shell headers whose symbols collide with the game's enums. Callers only see plain std::string.

// Open a native "save" dialog for exporting a savestate, defaulting to
// `defaultPath`. Returns the chosen path, or "" if the user cancelled.
std::string SpeedrunPromptExportStatePath(const std::string& defaultPath);

// Open a native "open" dialog for importing a savestate, starting in
// `defaultDir`. Returns the chosen path, or "" if the user cancelled.
std::string SpeedrunPromptImportStatePath(const std::string& defaultDir);

#endif
