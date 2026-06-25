#include "savestate_filedialog.h"

#include <cctype>
#include <string>
#include <vector>

#include "../Extractor/portable-file-dialogs.h"

static const std::vector<std::string> kStateFilters = { "Savestate (*.st)", "*.st" };
static const std::string kStateExt = ".st";

static bool EndsWithStateExt(const std::string& s) {
    if (s.size() < kStateExt.size()) {
        return false;
    }
    for (size_t i = 0; i < kStateExt.size(); i++) {
        if (std::tolower((unsigned char)s[s.size() - kStateExt.size() + i]) != kStateExt[i]) {
            return false;
        }
    }
    return true;
}

std::string SpeedrunPromptExportStatePath(const std::string& defaultPath) {
    std::string path = pfd::save_file("Export savestate", defaultPath, kStateFilters).result();
    // Let the user type just a name and still get a ".st" file. Append the extension only when it's missing
    // (case-insensitive) so we don't double it.
    if (!path.empty() && !EndsWithStateExt(path)) {
        path += kStateExt;
    }
    return path;
}

std::string SpeedrunPromptImportStatePath(const std::string& defaultDir) {
    std::vector<std::string> selection = pfd::open_file("Import savestate", defaultDir, kStateFilters).result();
    return selection.empty() ? std::string() : selection.front();
}
