#include "savestate_filedialog.h"

#include <vector>

#include "../Extractor/portable-file-dialogs.h"

static const std::vector<std::string> kStateFilters = { "GZ savestate (*.gzs)", "*.gzs" };

std::string GzPromptExportStatePath(const std::string& defaultPath) {
    return pfd::save_file("Export savestate", defaultPath, kStateFilters).result();
}

std::string GzPromptImportStatePath(const std::string& defaultDir) {
    std::vector<std::string> selection = pfd::open_file("Import savestate", defaultDir, kStateFilters).result();
    return selection.empty() ? std::string() : selection.front();
}
