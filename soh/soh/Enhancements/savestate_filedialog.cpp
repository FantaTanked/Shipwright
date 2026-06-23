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
    // Let the user type just a name (e.g. "shadow_temple") and always end up with ".st", without having
    // to type or preserve the extension. Append only when it's actually missing (case-insensitive), so a name
    // the dialog already completed isn't doubled.
    if (!path.empty() && !EndsWithStateExt(path)) {
        path += kStateExt;
    }
    return path;
}

std::string SpeedrunPromptImportStatePath(const std::string& defaultDir) {
    std::vector<std::string> selection = pfd::open_file("Import savestate", defaultDir, kStateFilters).result();
    return selection.empty() ? std::string() : selection.front();
}
