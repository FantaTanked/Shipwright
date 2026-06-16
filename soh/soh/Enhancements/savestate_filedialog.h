#ifndef SAVESTATE_FILEDIALOG_H
#define SAVESTATE_FILEDIALOG_H

#include <string>

// ship-gz: thin wrappers around the native file dialog (portable-file-dialogs).
// Kept in their own translation unit because pfd pulls in the Windows shell
// headers, which define symbols (e.g. PS_NONE) that collide with the game's
// enums. Callers only see plain std::string and avoid that pollution.

// Open a native "save" dialog for exporting a savestate, defaulting to
// `defaultPath`. Returns the chosen path, or "" if the user cancelled.
std::string GzPromptExportStatePath(const std::string& defaultPath);

// Open a native "open" dialog for importing a savestate, starting in
// `defaultDir`. Returns the chosen path, or "" if the user cancelled.
std::string GzPromptImportStatePath(const std::string& defaultDir);

#endif
