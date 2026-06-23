// speedrun: "speedrun mode" practice features, modelled on the GameCube practice ROM (speedrun).
// When the speedrun toggle is on, the player can drive savestates with the controller alone.
//
//   In game (menu closed):
//     - D-pad Left  : save the current savestate slot
//     - D-pad Right : load the current savestate slot
//     - R + C-Down  : open the menu
//   Menu (main screen):
//     - D-pad Up/Down : move the cursor
//     - C-Down        : confirm the highlighted entry
//     - D-pad L/R     : change slot (while on the "Slot" row)
//     - R + C-Down    : close the menu
//   Menu (import screen):
//     - D-pad Up/Down : pick a savestate file
//     - C-Down        : import the highlighted file
//     - B             : back to the main screen
//     - R + C-Down    : close the menu
//   While the menu is open the game receives no input (Link stays put, no pause).
//
// Export still uses the native OS save dialog (mouse/keyboard); import is a controller-
// navigable list of the .st files in the savestates/ folder.
//
// The menu is a speedrun-style text overlay drawn on the ImGui foreground draw list; we
// own the controller navigation rather than routing through ImGui's widget nav.

#include <atomic>
#include <algorithm>
#include <cmath>
#include <cstring>
#include <mutex>
#include <string>
#include <vector>
#include <filesystem>

#include <imgui.h>

#include <libultraship/bridge.h>
#include <ship/Context.h>
#include <ship/window/gui/GuiWindow.h>
#include <ship/window/gui/GameOverlay.h>
#include "soh/Enhancements/game-interactor/GameInteractor_Hooks.h"
#include "soh/ShipInit.hpp"
#include "soh/OTRGlobals.h"
#include "soh/Enhancements/savestates.h"
#include "soh/Enhancements/savestate_filedialog.h"
#include "soh/Enhancements/Warping.h"
#include "speedrun_watches.h"

extern "C" {
extern PlayState* gPlayState;
#include "macros.h"
}

#define CVAR_SPEEDRUN_MODE_NAME CVAR_CHEAT("SpeedrunMode")
#define CVAR_SPEEDRUN_MODE_DEFAULT 0
#define CVAR_SPEEDRUN_MODE_VALUE CVarGetInteger(CVAR_SPEEDRUN_MODE_NAME, CVAR_SPEEDRUN_MODE_DEFAULT)

// The speedrun-style top-level menu. Everything is greyed out (a stub) except "return"
// (closes) and "macro", which opens our savestate section -- mirroring speedrun, where
// save/load states live under the macro page.
struct SpeedrunRootItem {
    const char* label;
    bool enabled;
};
static const SpeedrunRootItem kRootItems[] = {
    { "return", true },     { "warps", true },    { "scene", false },  { "cheats", false },
    { "inventory", false }, { "equips", false },  { "file", false },   { "macro", true },
    { "watches", true },    { "debug", false },   { "settings", false },
};
static const int kRootCount = (int)(sizeof(kRootItems) / sizeof(kRootItems[0]));

// Macro-screen rows (our savestate section), in display order. "return" first, speedrun-style.
enum SpeedrunMenuEntry {
    SPEEDRUN_MENU_BACK,
    SPEEDRUN_MENU_SAVE,
    SPEEDRUN_MENU_LOAD,
    SPEEDRUN_MENU_EXPORT,
    SPEEDRUN_MENU_IMPORT,
    SPEEDRUN_MENU_SLOT,
    SPEEDRUN_MENU_COUNT,
};

enum SpeedrunScreen {
    SPEEDRUN_SCREEN_ROOT,
    SPEEDRUN_SCREEN_MACRO,
    SPEEDRUN_SCREEN_IMPORT,
    SPEEDRUN_SCREEN_WARP_CAT,      // pick a category (dungeons, bosses, towns, ...)
    SPEEDRUN_SCREEN_WARP_PLACE,    // pick a place within the category
    SPEEDRUN_SCREEN_WARP_ENTRANCE, // pick an entrance within the place
    SPEEDRUN_SCREEN_WATCHES,       // list active watches (+ "add watch")
    SPEEDRUN_SCREEN_WATCH_ADD,     // pick a variable from the catalog to add
    SPEEDRUN_SCREEN_WATCH_EDIT,    // edit one watch: move / type / remove
};

struct SpeedrunImportFile {
    enum class Kind { File, Dir, Up } kind;
    std::string label; // name shown in the list (files: full name incl. .st; folders: name + "/")
    std::string path;  // File: the file; Dir: the subfolder; Up: the parent folder -- all full paths
};

// Shared between the game-thread update hook and the GUI-thread draw. The scalars are
// atomic; the import file list is guarded by a mutex (it's resized on the game thread
// while the draw thread iterates it).
static std::atomic<bool> sMenuOpen{ false };
static std::atomic<int> sScreen{ SPEEDRUN_SCREEN_ROOT };
static std::atomic<int> sMenuSel{ 0 };
static std::atomic<int> sImportSel{ 0 };
static std::mutex sImportMutex;
static std::vector<SpeedrunImportFile> sImportFiles;

// Warps browser: one cursor per nav level, plus the chosen category/place we descended
// into. The category/place/entrance strings come from read-only static tables (see
// Warping.cpp), so the draw thread can query them directly without a snapshot/mutex.
static std::atomic<int> sWarpCatSel{ 0 };      // cursor on the category screen
static std::atomic<int> sWarpPlaceSel{ 0 };    // cursor on the place screen
static std::atomic<int> sWarpEntranceSel{ 0 }; // cursor on the entrance screen
static std::atomic<int> sWarpCat{ 0 };         // category we descended into
static std::atomic<int> sWarpPlace{ 0 };       // place we descended into
static std::atomic<bool> sWarpSkippedPlace{ false }; // entered entrance screen straight
                                                     // from the category (single-place cat)

// Watches browser: one cursor per screen, the watch being edited, and whether we're in
// positioning mode (D-pad nudges the watch instead of moving the cursor).
static std::atomic<int> sWatchSel{ 0 };           // cursor on the watches list
static std::atomic<int> sWatchAddSel{ 0 };        // cursor on the catalog (add) screen
static std::atomic<int> sWatchEditSel{ 0 };       // cursor on the edit screen
static std::atomic<int> sWatchEditIdx{ 0 };       // active watch being edited
static std::atomic<bool> sWatchPositioning{ false };

// The overlay window. Kept hidden unless the menu is open, so it never participates in
// the draw loop while closed (an always-shown borderless window left a black artifact
// when the OS window was moved/resized).
static std::shared_ptr<Ship::GuiWindow> sOverlay;

static unsigned int SpeedrunCurrentSlot() {
    return OTRGlobals::Instance->gSaveStateMgr->GetCurrentSlot();
}

// Name a slot suggests in the export dialog. Deliberately has NO ".st" extension: the dialog appends it
// (see SpeedrunPromptExportStatePath), so you can just type a name like "shadow_temple" without working around
// the suffix.
static std::string SpeedrunSlotFilePath(unsigned int slot) {
    return (std::filesystem::path(SaveStateMgr::GetStateDirectory()) / ("savestate_" + std::to_string(slot)))
        .string();
}

// D-pad up/down auto-repeat: navigation is edge-triggered (input->press), so holding a
// direction would otherwise move one row and stop. Each frame the menu is open we track
// how long up/down has been held and, past an initial delay, synthesize extra press
// events at a fixed interval so the cursor keeps scrolling. Counts are in game frames
// (~20/s), so ~0.4s before repeat then ~10 rows/s.
static int sDpadRepeatDir = 0;    // -1 = up held, +1 = down held, 0 = neither
static int sDpadHeldFrames = 0;   // frames the current direction has been held
static void SpeedrunApplyDpadRepeat(Input* input) {
    const uint16_t cur = input->cur.button;
    const int dir = CHECK_BTN_ALL(cur, BTN_DUP) ? -1 : (CHECK_BTN_ALL(cur, BTN_DDOWN) ? 1 : 0);
    if (dir == 0 || dir != sDpadRepeatDir) {
        // Released, or a fresh press in a new direction: the real press edge handles the
        // first move; just (re)start the hold timer.
        sDpadRepeatDir = dir;
        sDpadHeldFrames = 0;
        return;
    }
    const int kInitialDelay = 8;  // frames held before auto-repeat begins
    const int kRepeatPeriod = 2;  // frames between synthesized presses
    sDpadHeldFrames++;
    if (sDpadHeldFrames >= kInitialDelay && (sDpadHeldFrames - kInitialDelay) % kRepeatPeriod == 0) {
        input->press.button |= (dir < 0) ? BTN_DUP : BTN_DDOWN;
    }
}

// While the menu is open only the D-pad is captured for navigation; the rest of the
// controller still drives the game (speedrun-style, the game keeps running underneath).
static void SpeedrunSuppressDpad(Input* input) {
    const uint16_t dpad = BTN_DUP | BTN_DDOWN | BTN_DLEFT | BTN_DRIGHT;
    input->cur.button &= ~dpad;
    input->press.button &= ~dpad;
    input->rel.button &= ~dpad;
}

// Populate the import list from `dir`: a ".." entry (unless already at the savestates root) so you can go back
// up, then subfolders, then .st files -- folders and files each sorted alphabetically. This lets you
// organise states into folders (e.g. an "MST" folder) and browse them like gz does.
static void SpeedrunScanImportDir(const std::filesystem::path& dir) {
    const std::filesystem::path root = SaveStateMgr::GetStateDirectory();
    std::vector<SpeedrunImportFile> dirs, files;
    std::error_code ec;
    for (auto it = std::filesystem::directory_iterator(dir, ec);
         !ec && it != std::filesystem::directory_iterator(); it.increment(ec)) {
        std::error_code ec2;
        const std::filesystem::path& p = it->path();
        if (it->is_directory(ec2)) {
            dirs.push_back({ SpeedrunImportFile::Kind::Dir, p.filename().string() + "/", p.string() });
        } else if (it->is_regular_file(ec2) && p.extension() == ".st") {
            files.push_back({ SpeedrunImportFile::Kind::File, p.filename().string(), p.string() }); // show name + .st
        }
    }
    const auto byLabel = [](const SpeedrunImportFile& a, const SpeedrunImportFile& b) { return a.label < b.label; };
    std::sort(dirs.begin(), dirs.end(), byLabel);
    std::sort(files.begin(), files.end(), byLabel);

    std::vector<SpeedrunImportFile> entries;
    // Offer ".." to go up a level, but never above the savestates root.
    if (dir.lexically_normal() != root.lexically_normal() && dir.has_parent_path()) {
        entries.push_back({ SpeedrunImportFile::Kind::Up, "..", dir.parent_path().string() });
    }
    entries.insert(entries.end(), dirs.begin(), dirs.end());
    entries.insert(entries.end(), files.begin(), files.end());

    std::lock_guard<std::mutex> lock(sImportMutex);
    sImportFiles = std::move(entries);
}

// Enter the import screen at the savestates/ root.
static void SpeedrunEnterImportScreen() {
    SpeedrunScanImportDir(SaveStateMgr::GetStateDirectory());
    sImportSel.store(0);
    sScreen.store(SPEEDRUN_SCREEN_IMPORT);
}

// Enter the warps browser at the top (category) level.
static void SpeedrunEnterWarpsScreen() {
    sWarpCatSel.store(0);
    sScreen.store(SPEEDRUN_SCREEN_WARP_CAT);
}

// Switch screens, resetting the (shared) row cursor.
static void SpeedrunGoToScreen(SpeedrunScreen screen) {
    sScreen.store(screen);
    sMenuSel.store(0);
}

static void SpeedrunConfirmRootSelection() {
    const int sel = sMenuSel.load();
    if (sel < 0 || sel >= kRootCount) {
        return;
    }
    const SpeedrunRootItem& item = kRootItems[sel];
    if (!item.enabled) {
        return; // greyed-out stub
    }
    if (strcmp(item.label, "macro") == 0) {
        SpeedrunGoToScreen(SPEEDRUN_SCREEN_MACRO);
    } else if (strcmp(item.label, "warps") == 0) {
        SpeedrunEnterWarpsScreen();
    } else if (strcmp(item.label, "watches") == 0) {
        sWatchSel.store(0);
        sScreen.store(SPEEDRUN_SCREEN_WATCHES);
    } else if (strcmp(item.label, "return") == 0) {
        sMenuOpen.store(false);
    }
}

static void SpeedrunConfirmMacroSelection() {
    const auto mgr = OTRGlobals::Instance->gSaveStateMgr;
    const unsigned int slot = SpeedrunCurrentSlot();
    switch (sMenuSel.load()) {
        case SPEEDRUN_MENU_SAVE:
            mgr->AddRequest({ slot, RequestType::SAVE });
            break;
        case SPEEDRUN_MENU_LOAD:
            mgr->AddRequest({ slot, RequestType::LOAD });
            break;
        case SPEEDRUN_MENU_EXPORT:
            // Export keeps the native save dialog (modal; runs here on the game thread).
            mgr->ExportState(slot, SpeedrunPromptExportStatePath(SpeedrunSlotFilePath(slot)));
            break;
        case SPEEDRUN_MENU_IMPORT:
            SpeedrunEnterImportScreen();
            break;
        case SPEEDRUN_MENU_SLOT:
            mgr->SetCurrentSlot((slot + 1) % 6);
            break;
        case SPEEDRUN_MENU_BACK:
            SpeedrunGoToScreen(SPEEDRUN_SCREEN_ROOT);
            break;
        default:
            break;
    }
}

// Import-screen rows: [0] "return", then ".." (in a subfolder) / subfolders / .st files. A folder or ".."
// navigates and stays on the screen; a file imports it into the current slot.
static void SpeedrunConfirmImportSelection() {
    const int sel = sImportSel.load();
    if (sel == 0) {
        SpeedrunGoToScreen(SPEEDRUN_SCREEN_MACRO); // "return" row
        return;
    }
    SpeedrunImportFile entry{};
    bool valid = false;
    {
        std::lock_guard<std::mutex> lock(sImportMutex);
        const int idx = sel - 1;
        if (idx >= 0 && idx < (int)sImportFiles.size()) {
            entry = sImportFiles[idx];
            valid = true;
        }
    }
    if (!valid) {
        return;
    }
    if (entry.kind == SpeedrunImportFile::Kind::File) {
        OTRGlobals::Instance->gSaveStateMgr->ImportState(SpeedrunCurrentSlot(), entry.path);
        SpeedrunGoToScreen(SPEEDRUN_SCREEN_MACRO); // back to the macro screen; apply with Load
    } else {
        SpeedrunScanImportDir(std::filesystem::path(entry.path)); // ".." or a folder: navigate, stay on screen
        sImportSel.store(0);
    }
}

// Category screen rows: [0] "return", then one row per category.
static void SpeedrunConfirmWarpCatSelection() {
    const int sel = sWarpCatSel.load();
    if (sel == 0) {
        SpeedrunGoToScreen(SPEEDRUN_SCREEN_ROOT); // "return" row
        return;
    }
    const int cat = sel - 1;
    if (cat < 0 || cat >= SpeedrunWarp_CategoryCount()) {
        return;
    }
    sWarpCat.store(cat);
    // A non-flat category with a single place (e.g. Shops) has nothing to choose at the
    // place level, so descend straight to that place's entrances.
    if (!SpeedrunWarp_CategoryIsFlat(cat) && SpeedrunWarp_PlaceCount(cat) == 1) {
        sWarpPlace.store(0);
        sWarpSkippedPlace.store(true);
        sWarpEntranceSel.store(0);
        sScreen.store(SPEEDRUN_SCREEN_WARP_ENTRANCE);
        return;
    }
    sWarpPlaceSel.store(0);
    sScreen.store(SPEEDRUN_SCREEN_WARP_PLACE);
}

// Place screen rows: [0] "return", then one row per place in the chosen category. For a
// flat category (bosses) the place row is the warp leaf; otherwise it descends.
static void SpeedrunConfirmWarpPlaceSelection() {
    const int sel = sWarpPlaceSel.load();
    if (sel == 0) {
        SpeedrunGoToScreen(SPEEDRUN_SCREEN_WARP_CAT);
        sWarpCatSel.store(0);
        return;
    }
    const int cat = sWarpCat.load();
    const int place = sel - 1;
    if (place < 0 || place >= SpeedrunWarp_PlaceCount(cat)) {
        return;
    }
    if (SpeedrunWarp_CategoryIsFlat(cat)) {
        if (SpeedrunWarp_Do(cat, place, 0)) {
            sMenuOpen.store(false); // close so the warp transition is visible
        }
        return;
    }
    sWarpPlace.store(place);
    sWarpSkippedPlace.store(false);
    sWarpEntranceSel.store(0);
    sScreen.store(SPEEDRUN_SCREEN_WARP_ENTRANCE);
}

// Entrance screen rows: [0] "return", then one row per entrance in the chosen place.
static void SpeedrunConfirmWarpEntranceSelection() {
    const int sel = sWarpEntranceSel.load();
    if (sel == 0) {
        // Return one real level up: back to the category screen if we skipped the place
        // level on the way in (single-place category), otherwise to the place screen.
        if (sWarpSkippedPlace.load()) {
            SpeedrunGoToScreen(SPEEDRUN_SCREEN_WARP_CAT);
            sWarpCatSel.store(0);
        } else {
            SpeedrunGoToScreen(SPEEDRUN_SCREEN_WARP_PLACE);
            sWarpPlaceSel.store(0);
        }
        return;
    }
    const int cat = sWarpCat.load();
    const int place = sWarpPlace.load();
    const int entrance = sel - 1;
    if (entrance < 0 || entrance >= SpeedrunWarp_EntranceCount(cat, place)) {
        return;
    }
    if (SpeedrunWarp_Do(cat, place, entrance)) {
        sMenuOpen.store(false); // close so the warp transition is visible
    }
}

static void SpeedrunHandleRootScreen(Input* input) {
    const uint16_t pressed = input->press.button;
    int sel = sMenuSel.load();

    if (CHECK_BTN_ALL(pressed, BTN_DUP)) {
        sel = (sel + kRootCount - 1) % kRootCount;
    } else if (CHECK_BTN_ALL(pressed, BTN_DDOWN)) {
        sel = (sel + 1) % kRootCount;
    }
    sMenuSel.store(sel);

    if (CHECK_BTN_ALL(pressed, BTN_CDOWN)) {
        SpeedrunConfirmRootSelection();
    }
}

static void SpeedrunHandleMacroScreen(Input* input) {
    const uint16_t pressed = input->press.button;
    int sel = sMenuSel.load();

    if (CHECK_BTN_ALL(pressed, BTN_DUP)) {
        sel = (sel + SPEEDRUN_MENU_COUNT - 1) % SPEEDRUN_MENU_COUNT;
    } else if (CHECK_BTN_ALL(pressed, BTN_DDOWN)) {
        sel = (sel + 1) % SPEEDRUN_MENU_COUNT;
    }
    sMenuSel.store(sel);

    if (sel == SPEEDRUN_MENU_SLOT) {
        const unsigned int slot = SpeedrunCurrentSlot();
        if (CHECK_BTN_ALL(pressed, BTN_DRIGHT)) {
            OTRGlobals::Instance->gSaveStateMgr->SetCurrentSlot((slot + 1) % 6);
        } else if (CHECK_BTN_ALL(pressed, BTN_DLEFT)) {
            OTRGlobals::Instance->gSaveStateMgr->SetCurrentSlot((slot + 5) % 6);
        }
    }

    if (CHECK_BTN_ALL(pressed, BTN_CDOWN)) {
        SpeedrunConfirmMacroSelection();
    }
}

static void SpeedrunHandleImportScreen(Input* input) {
    const uint16_t pressed = input->press.button;

    int count;
    {
        std::lock_guard<std::mutex> lock(sImportMutex);
        count = (int)sImportFiles.size();
    }
    const int rowCount = count + 1; // leading "return" row

    int sel = sImportSel.load();
    if (CHECK_BTN_ALL(pressed, BTN_DUP)) {
        sel = (sel + rowCount - 1) % rowCount;
    } else if (CHECK_BTN_ALL(pressed, BTN_DDOWN)) {
        sel = (sel + 1) % rowCount;
    }
    sImportSel.store(sel);

    if (CHECK_BTN_ALL(pressed, BTN_CDOWN)) {
        SpeedrunConfirmImportSelection();
    }
}

// Shared list-navigation helper for the warps screens: move `cursor` over `rowCount` rows
// (wrapping) on D-pad up/down. Returns true if C-Down (confirm) was pressed.
static bool SpeedrunNavList(Input* input, std::atomic<int>& cursor, int rowCount) {
    const uint16_t pressed = input->press.button;
    int sel = cursor.load();
    if (rowCount > 0) {
        if (CHECK_BTN_ALL(pressed, BTN_DUP)) {
            sel = (sel + rowCount - 1) % rowCount;
        } else if (CHECK_BTN_ALL(pressed, BTN_DDOWN)) {
            sel = (sel + 1) % rowCount;
        }
    }
    cursor.store(sel);
    return CHECK_BTN_ALL(pressed, BTN_CDOWN);
}

static void SpeedrunHandleWarpCatScreen(Input* input) {
    if (SpeedrunNavList(input, sWarpCatSel, SpeedrunWarp_CategoryCount() + 1)) {
        SpeedrunConfirmWarpCatSelection();
    }
}

static void SpeedrunHandleWarpPlaceScreen(Input* input) {
    if (SpeedrunNavList(input, sWarpPlaceSel, SpeedrunWarp_PlaceCount(sWarpCat.load()) + 1)) {
        SpeedrunConfirmWarpPlaceSelection();
    }
}

static void SpeedrunHandleWarpEntranceScreen(Input* input) {
    if (SpeedrunNavList(input, sWarpEntranceSel, SpeedrunWarp_EntranceCount(sWarpCat.load(), sWarpPlace.load()) + 1)) {
        SpeedrunConfirmWarpEntranceSelection();
    }
}

// Watches list rows: [0] "return", [1] "add watch", then one row per active watch.
static void SpeedrunHandleWatchesScreen(Input* input) {
    const int rowCount = 2 + SpeedrunWatch_Count();
    if (!SpeedrunNavList(input, sWatchSel, rowCount)) {
        return;
    }
    const int sel = sWatchSel.load();
    if (sel == 0) {
        SpeedrunGoToScreen(SPEEDRUN_SCREEN_ROOT);
    } else if (sel == 1) {
        sWatchAddSel.store(0);
        sScreen.store(SPEEDRUN_SCREEN_WATCH_ADD);
    } else {
        sWatchEditIdx.store(sel - 2);
        sWatchEditSel.store(0);
        sWatchPositioning.store(false);
        sScreen.store(SPEEDRUN_SCREEN_WATCH_EDIT);
    }
}

// Add screen rows: [0] "return", then one row per catalog variable.
static void SpeedrunHandleWatchAddScreen(Input* input) {
    if (!SpeedrunNavList(input, sWatchAddSel, SpeedrunWatch_CatalogCount() + 1)) {
        return;
    }
    const int sel = sWatchAddSel.load();
    if (sel == 0) {
        sWatchSel.store(0);
        sScreen.store(SPEEDRUN_SCREEN_WATCHES);
        return;
    }
    SpeedrunWatch_Add(sel - 1);
    sWatchSel.store(0);
    sScreen.store(SPEEDRUN_SCREEN_WATCHES);
}

// Edit screen rows: [0] "return", [1] "move", [2] "type", [3] "remove".
enum SpeedrunWatchEditEntry { SPEEDRUN_WATCH_EDIT_BACK, SPEEDRUN_WATCH_EDIT_MOVE, SPEEDRUN_WATCH_EDIT_TYPE, SPEEDRUN_WATCH_EDIT_REMOVE,
                        SPEEDRUN_WATCH_EDIT_COUNT };
static void SpeedrunHandleWatchEditScreen(Input* input) {
    const uint16_t pressed = input->press.button;
    const int idx = sWatchEditIdx.load();

    // Positioning mode: the D-pad nudges the watch; C-Down or B leaves the mode.
    if (sWatchPositioning.load()) {
        const float step = 2.0f;
        if (CHECK_BTN_ALL(pressed, BTN_DUP)) {
            SpeedrunWatch_Nudge(idx, 0.0f, -step);
        } else if (CHECK_BTN_ALL(pressed, BTN_DDOWN)) {
            SpeedrunWatch_Nudge(idx, 0.0f, step);
        } else if (CHECK_BTN_ALL(pressed, BTN_DLEFT)) {
            SpeedrunWatch_Nudge(idx, -step, 0.0f);
        } else if (CHECK_BTN_ALL(pressed, BTN_DRIGHT)) {
            SpeedrunWatch_Nudge(idx, step, 0.0f);
        }
        if (CHECK_BTN_ALL(pressed, BTN_CDOWN) || CHECK_BTN_ALL(pressed, BTN_B)) {
            sWatchPositioning.store(false);
        }
        return;
    }

    int sel = sWatchEditSel.load();
    if (CHECK_BTN_ALL(pressed, BTN_DUP)) {
        sel = (sel + SPEEDRUN_WATCH_EDIT_COUNT - 1) % SPEEDRUN_WATCH_EDIT_COUNT;
    } else if (CHECK_BTN_ALL(pressed, BTN_DDOWN)) {
        sel = (sel + 1) % SPEEDRUN_WATCH_EDIT_COUNT;
    }
    sWatchEditSel.store(sel);

    if (sel == SPEEDRUN_WATCH_EDIT_TYPE) {
        if (CHECK_BTN_ALL(pressed, BTN_DRIGHT)) {
            SpeedrunWatch_CycleType(idx, 1);
        } else if (CHECK_BTN_ALL(pressed, BTN_DLEFT)) {
            SpeedrunWatch_CycleType(idx, -1);
        }
    }

    if (CHECK_BTN_ALL(pressed, BTN_CDOWN)) {
        switch (sel) {
            case SPEEDRUN_WATCH_EDIT_BACK:
                sWatchSel.store(0);
                sScreen.store(SPEEDRUN_SCREEN_WATCHES);
                break;
            case SPEEDRUN_WATCH_EDIT_MOVE:
                sWatchPositioning.store(true);
                break;
            case SPEEDRUN_WATCH_EDIT_REMOVE:
                SpeedrunWatch_Remove(idx);
                sWatchSel.store(0);
                sScreen.store(SPEEDRUN_SCREEN_WATCHES);
                break;
            default:
                break;
        }
    }
}

static void OnGameStateMainStartSpeedrunMode() {
    if (!GameInteractor::IsSaveLoaded(true) || gPlayState == nullptr) {
        return;
    }
    if (CVarGetInteger(CVAR_CHEAT("SaveStatesEnabled"), 0) == 0) {
        return;
    }

    // Refresh the watch snapshot from live game state (valid gPlayState here on the game
    // thread); the draw thread renders the cached values.
    SpeedrunWatch_UpdateSnapshot(gPlayState);

    // Keep the overlay shown while the menu is open OR any watch is active (watches draw
    // on screen during play). Otherwise hide it to avoid a stray borderless window.
    if (sOverlay != nullptr) {
        const bool want = sMenuOpen.load() || SpeedrunWatch_Count() > 0;
        if (sOverlay->IsVisible() != want) {
            if (want) {
                sOverlay->Show();
            } else {
                sOverlay->Hide();
            }
        }
    }

    Input* input = &gPlayState->state.input[0];
    const bool rHeld = CHECK_BTN_ALL(input->cur.button, BTN_R);
    const bool cDownPressed = CHECK_BTN_ALL(input->press.button, BTN_CDOWN);

    if (!sMenuOpen.load()) {
        // R + C-Down opens the menu, returning to whatever screen/selection was active
        // when it was last closed.
        if (rHeld && cDownPressed) {
            sMenuOpen.store(true);
            SpeedrunSuppressDpad(input);
            return;
        }
        // Quick hotkeys while playing.
        if (CHECK_BTN_ALL(input->press.button, BTN_DLEFT)) {
            OTRGlobals::Instance->gSaveStateMgr->AddRequest({ SpeedrunCurrentSlot(), RequestType::SAVE });
        } else if (CHECK_BTN_ALL(input->press.button, BTN_DRIGHT)) {
            OTRGlobals::Instance->gSaveStateMgr->AddRequest({ SpeedrunCurrentSlot(), RequestType::LOAD });
        }
        return;
    }

    // Menu is open. Synthesize repeat presses for a held D-pad so lists keep scrolling,
    // then R + C-Down closes it; a bare C-Down confirms (handled per screen).
    SpeedrunApplyDpadRepeat(input);
    if (rHeld && cDownPressed) {
        sMenuOpen.store(false);
    } else if (sScreen.load() == SPEEDRUN_SCREEN_IMPORT) {
        SpeedrunHandleImportScreen(input);
    } else if (sScreen.load() == SPEEDRUN_SCREEN_WARP_CAT) {
        SpeedrunHandleWarpCatScreen(input);
    } else if (sScreen.load() == SPEEDRUN_SCREEN_WARP_PLACE) {
        SpeedrunHandleWarpPlaceScreen(input);
    } else if (sScreen.load() == SPEEDRUN_SCREEN_WARP_ENTRANCE) {
        SpeedrunHandleWarpEntranceScreen(input);
    } else if (sScreen.load() == SPEEDRUN_SCREEN_WATCHES) {
        SpeedrunHandleWatchesScreen(input);
    } else if (sScreen.load() == SPEEDRUN_SCREEN_WATCH_ADD) {
        SpeedrunHandleWatchAddScreen(input);
    } else if (sScreen.load() == SPEEDRUN_SCREEN_WATCH_EDIT) {
        SpeedrunHandleWatchEditScreen(input);
    } else if (sScreen.load() == SPEEDRUN_SCREEN_MACRO) {
        SpeedrunHandleMacroScreen(input);
    } else {
        SpeedrunHandleRootScreen(input);
    }

    SpeedrunSuppressDpad(input); // only the D-pad is captured; the game keeps the rest
}

// Borderless, input-less overlay window; we draw through the foreground draw list so
// the window itself is just a hook to get DrawElement() called each frame.
class SpeedrunMenuOverlay final : public Ship::GuiWindow {
  public:
    using GuiWindow::GuiWindow;

  protected:
    void InitElement() override {
    }
    void UpdateElement() override {
    }
    void DrawElement() override {
        auto overlay = Ship::Context::GetRawInstance()->GetWindow()->GetGui()->GetGameOverlay();
        if (overlay == nullptr) {
            return;
        }
        // Make this (transparent, input-less) window cover the screen so TextDraw's
        // window-relative coordinates land where we expect, matching the notifications.
        const ImGuiViewport* vp = ImGui::GetMainViewport();
        ImGui::SetWindowPos(vp->Pos);
        ImGui::SetWindowSize(vp->Size);

        // The overlay font is loaded at a fixed 12px, so it shrinks on high-res displays.
        // Scale both the glyphs (SetWindowFontScale) and our layout coordinates (mScale)
        // by the viewport height so the menu stays readable at 1080p/1440p/4K. The 1.5
        // floor keeps it legible in small windows.
        mScale = std::max(1.5f, vp->Size.y / 540.0f);
        ImGui::SetWindowFontScale(mScale);

        // Active watches draw on screen whether or not the menu is open.
        DrawWatches(overlay);

        if (sMenuOpen.load()) {
            if (sScreen.load() == SPEEDRUN_SCREEN_IMPORT) {
                DrawImportScreen(overlay);
            } else if (sScreen.load() == SPEEDRUN_SCREEN_WARP_CAT) {
                DrawWarpCatScreen(overlay);
            } else if (sScreen.load() == SPEEDRUN_SCREEN_WARP_PLACE) {
                DrawWarpPlaceScreen(overlay);
            } else if (sScreen.load() == SPEEDRUN_SCREEN_WARP_ENTRANCE) {
                DrawWarpEntranceScreen(overlay);
            } else if (sScreen.load() == SPEEDRUN_SCREEN_WATCHES) {
                DrawWatchesScreen(overlay);
            } else if (sScreen.load() == SPEEDRUN_SCREEN_WATCH_ADD) {
                DrawWatchAddScreen(overlay);
            } else if (sScreen.load() == SPEEDRUN_SCREEN_WATCH_EDIT) {
                DrawWatchEditScreen(overlay);
            } else if (sScreen.load() == SPEEDRUN_SCREEN_MACRO) {
                DrawMacroScreen(overlay);
            } else {
                DrawRootScreen(overlay);
            }
        }

        ImGui::SetWindowFontScale(1.0f);
    }

  private:
    float mScale = 1.0f; // resolution-based UI scale, set each frame in DrawElement

    // Mouse-drag state (Alt + left-drag to move a watch). -1 when not dragging.
    int mDragIndex = -1;
    ImVec2 mDragLastMouse{ 0.0f, 0.0f };
    float mDragX = 0.0f, mDragY = 0.0f; // live unscaled position while dragging

    // Draws a speedrun-style list: the overlay's pixel font ("Press Start 2P") with a drop
    // shadow, left-aligned near the left edge and vertically about a fifth down, the
    // selected row tinted speedrun-blue (no cursor arrow). If `enabled` is supplied, disabled
    // rows are dimmed (a greyed-out speedrun stub).
    void DrawList(const std::shared_ptr<Ship::GameOverlay>& overlay, const std::vector<std::string>& rows, int sel,
                  const std::vector<bool>* enabled = nullptr) {
        const ImVec4 white(1.0f, 1.0f, 1.0f, 1.0f);
        const ImVec4 blue(0.45f, 0.62f, 1.0f, 1.0f); // speedrun selection colour
        const ImVec4 dim(0.5f, 0.5f, 0.5f, 1.0f);
        const ImVec4 dimSel(0.4f, 0.5f, 0.7f, 1.0f);

        // CalculateTextSize returns the unscaled glyph height, so scale our spacing to
        // match the SetWindowFontScale applied to the text itself. speedrun uses tight spacing.
        const float lineH = (overlay->CalculateTextSize("Ag").y + 1.0f) * mScale;

        const ImVec4 grey(0.66f, 0.66f, 0.66f, 1.0f);

        // speedrun anchors the menu to the left edge, roughly a fifth of the way down.
        const ImGuiViewport* vp = ImGui::GetMainViewport();
        const float x = vp->Size.x * 0.045f;
        const float top = vp->Size.y * 0.22f;

        // Long lists (e.g. the warp destinations) scroll: show a window of rows around
        // the selection, leaving room above/below for the "more" indicators.
        const int total = (int)rows.size();
        const int maxRows = std::max(1, (int)((vp->Size.y * 0.7f) / lineH));
        int start = 0;
        if (total > maxRows && sel >= 0) {
            start = std::clamp(sel - maxRows / 2, 0, total - maxRows);
        }
        const int end = std::min(total, start + maxRows);

        float y = top;
        if (start > 0) {
            overlay->TextDraw(x, y, true, grey, "  ^");
            y += lineH;
        }
        for (int i = start; i < end; i++) {
            const bool selected = (i == sel);
            const bool rowEnabled = (enabled == nullptr) || (i < (int)enabled->size() && (*enabled)[i]);
            ImVec4 color;
            if (rowEnabled) {
                color = selected ? blue : white;
            } else {
                color = selected ? dimSel : dim;
            }
            overlay->TextDraw(x, y, true, color, "%s", rows[i].c_str());
            y += lineH;
        }
        if (end < total) {
            overlay->TextDraw(x, y, true, grey, "  v");
        }
    }

    void DrawRootScreen(const std::shared_ptr<Ship::GameOverlay>& overlay) {
        std::vector<std::string> rows;
        std::vector<bool> enabled;
        for (int i = 0; i < kRootCount; i++) {
            rows.push_back(kRootItems[i].label);
            enabled.push_back(kRootItems[i].enabled);
        }
        DrawList(overlay, rows, sMenuSel.load(), &enabled);
    }

    void DrawMacroScreen(const std::shared_ptr<Ship::GameOverlay>& overlay) {
        const unsigned int slot = SpeedrunCurrentSlot();
        std::vector<std::string> rows = {
            "return",
            "save state",
            "load state",
            "export to disk",
            "import from disk",
            "slot < " + std::to_string(slot) + " >",
        };
        DrawList(overlay, rows, sMenuSel.load());
    }

    void DrawImportScreen(const std::shared_ptr<Ship::GameOverlay>& overlay) {
        std::vector<std::string> rows;
        int sel;
        rows.push_back("return"); // row 0
        {
            std::lock_guard<std::mutex> lock(sImportMutex);
            for (const auto& f : sImportFiles) {
                rows.push_back(f.label);
            }
            sel = sImportSel.load();
        }
        if (rows.size() == 1) {
            rows.push_back("(empty)"); // informational, not selectable
        }
        DrawList(overlay, rows, sel);
    }

    void DrawWarpCatScreen(const std::shared_ptr<Ship::GameOverlay>& overlay) {
        std::vector<std::string> rows;
        rows.push_back("return"); // row 0
        for (int i = 0; i < SpeedrunWarp_CategoryCount(); i++) {
            rows.push_back(SpeedrunWarp_CategoryName(i));
        }
        DrawList(overlay, rows, sWarpCatSel.load());
    }

    void DrawWarpPlaceScreen(const std::shared_ptr<Ship::GameOverlay>& overlay) {
        const int cat = sWarpCat.load();
        std::vector<std::string> rows;
        rows.push_back("return"); // row 0
        for (int i = 0; i < SpeedrunWarp_PlaceCount(cat); i++) {
            rows.push_back(SpeedrunWarp_PlaceName(cat, i));
        }
        DrawList(overlay, rows, sWarpPlaceSel.load());
    }

    void DrawWarpEntranceScreen(const std::shared_ptr<Ship::GameOverlay>& overlay) {
        const int cat = sWarpCat.load();
        const int place = sWarpPlace.load();
        std::vector<std::string> rows;
        rows.push_back("return"); // row 0
        for (int i = 0; i < SpeedrunWarp_EntranceCount(cat, place); i++) {
            rows.push_back(SpeedrunWarp_EntranceName(cat, place, i));
        }
        DrawList(overlay, rows, sWarpEntranceSel.load());
    }

    // On-screen watch values, drawn every frame at each watch's position (speedrun-style). The
    // watch being positioned (controller) or dragged (mouse) is tinted speedrun-blue. Positions
    // are stored unscaled, so multiply by the resolution scale to keep placement
    // consistent across displays.
    //
    // Mouse: hold Alt and left-drag a watch to fine-tune its position. The drag runs on
    // this (draw) thread; positions are pushed to the game thread via SpeedrunWatch_RequestMove,
    // and persisted on release.
    void DrawWatches(const std::shared_ptr<Ship::GameOverlay>& overlay) {
        const std::vector<SpeedrunWatchDisplay> snap = SpeedrunWatch_Snapshot();
        const ImVec4 white(1.0f, 1.0f, 1.0f, 1.0f);
        const ImVec4 blue(0.45f, 0.62f, 1.0f, 1.0f);
        const bool positioning = (sScreen.load() == SPEEDRUN_SCREEN_WATCH_EDIT) && sWatchPositioning.load();
        const int editIdx = sWatchEditIdx.load();

        ImGuiIO& io = ImGui::GetIO();
        const bool altDrag = io.KeyAlt && io.MouseDown[0];

        // Drags snap to an 8px grid so watches line up with each other; hold Shift for
        // free (un-snapped) fine positioning. mDragX/Y track the raw accumulated drag;
        // the snapped value is what we draw and store.
        const float kGrid = 8.0f;
        const bool freeMove = io.KeyShift;
        auto gridSnap = [&](float v) { return freeMove ? v : std::round(v / kGrid) * kGrid; };

        // Cancel the drag if it ended, the alt key was released, or the watch vanished;
        // commit the final (snapped) position to disk.
        if (mDragIndex >= 0 && (!altDrag || mDragIndex >= (int)snap.size())) {
            SpeedrunWatch_RequestMove(mDragIndex, gridSnap(mDragX), gridSnap(mDragY), true);
            mDragIndex = -1;
        }

        // Track the live drag position from the mouse delta (unscaled).
        if (mDragIndex >= 0) {
            mDragX += (io.MousePos.x - mDragLastMouse.x) / mScale;
            mDragY += (io.MousePos.y - mDragLastMouse.y) / mScale;
            if (mDragX < 0.0f) mDragX = 0.0f;
            if (mDragY < 0.0f) mDragY = 0.0f;
            mDragLastMouse = io.MousePos;
            SpeedrunWatch_RequestMove(mDragIndex, gridSnap(mDragX), gridSnap(mDragY), false);
        }

        for (int i = 0; i < (int)snap.size(); i++) {
            const SpeedrunWatchDisplay& w = snap[i];
            const bool dragging = (i == mDragIndex);
            const float px = (dragging ? gridSnap(mDragX) : w.x) * mScale;
            const float py = (dragging ? gridSnap(mDragY) : w.y) * mScale;

            const ImVec4 color = (dragging || (positioning && i == editIdx)) ? blue : white;
            const std::string text = w.label + ": " + w.value;

            // Hit rect for starting a drag: where this row lands on screen, plus its size.
            ImGui::SetCursorPos(ImVec2(px, py));
            const ImVec2 screenPos = ImGui::GetCursorScreenPos();
            ImVec2 size = overlay->CalculateTextSize(text.c_str());
            size.x *= mScale;
            size.y *= mScale;

            overlay->TextDraw(px, py, true, color, "%s", text.c_str());

            // Start a drag on an Alt+left-click that lands on this watch.
            if (mDragIndex < 0 && io.KeyAlt && io.MouseClicked[0] &&
                io.MousePos.x >= screenPos.x && io.MousePos.x <= screenPos.x + size.x &&
                io.MousePos.y >= screenPos.y && io.MousePos.y <= screenPos.y + size.y) {
                mDragIndex = i;
                mDragX = w.x;
                mDragY = w.y;
                mDragLastMouse = io.MousePos;
            }
        }
    }

    // Watches list: "return", "add watch", then "label: value" per active watch.
    void DrawWatchesScreen(const std::shared_ptr<Ship::GameOverlay>& overlay) {
        const std::vector<SpeedrunWatchDisplay> snap = SpeedrunWatch_Snapshot();
        std::vector<std::string> rows = { "return", "add watch" };
        for (const auto& w : snap) {
            rows.push_back(w.label + ": " + w.value);
        }
        DrawList(overlay, rows, sWatchSel.load());
    }

    void DrawWatchAddScreen(const std::shared_ptr<Ship::GameOverlay>& overlay) {
        std::vector<std::string> rows;
        rows.push_back("return"); // row 0
        for (int i = 0; i < SpeedrunWatch_CatalogCount(); i++) {
            rows.push_back(SpeedrunWatch_CatalogName(i));
        }
        DrawList(overlay, rows, sWatchAddSel.load());
    }

    void DrawWatchEditScreen(const std::shared_ptr<Ship::GameOverlay>& overlay) {
        const std::vector<SpeedrunWatchDisplay> snap = SpeedrunWatch_Snapshot();
        const int idx = sWatchEditIdx.load();
        std::string title = "(watch)";
        std::string typeName = "?";
        if (idx >= 0 && idx < (int)snap.size()) {
            title = snap[idx].label;
            typeName = snap[idx].typeName;
        }
        const bool positioning = sWatchPositioning.load();
        std::vector<std::string> rows = {
            "return",
            positioning ? "move (D-pad; C-down to set)" : "move",
            "type < " + typeName + " >",
            "remove",
        };
        // While positioning, no list row is "selected"; otherwise show the cursor.
        DrawList(overlay, rows, positioning ? -1 : sWatchEditSel.load());
    }
};

static void SpeedrunEnsureOverlayRegistered() {
    static bool registered = false;
    if (registered) {
        return;
    }
    auto gui = Ship::Context::GetRawInstance()->GetWindow()->GetGui();
    if (gui == nullptr) {
        return;
    }
    // Empty visibility CVar: we drive visibility ourselves from the hook and don't want
    // it persisted across sessions.
    auto overlay = std::make_shared<SpeedrunMenuOverlay>(
        "", false, "speedrun menu overlay", ImVec2(10, 10),
        ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove |
            ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoBackground | ImGuiWindowFlags_NoSavedSettings |
            ImGuiWindowFlags_NoInputs | ImGuiWindowFlags_NoFocusOnAppearing | ImGuiWindowFlags_NoNav);
    gui->AddGuiWindow(overlay);
    overlay->Hide(); // shown only while the menu is open (see the hook)
    sOverlay = overlay;
    registered = true;
}

void RegisterSpeedrunMode() {
    SpeedrunEnsureOverlayRegistered();
    SpeedrunWatch_Load(); // restore persisted watches
    COND_HOOK(OnGameStateMainStart, CVAR_SPEEDRUN_MODE_VALUE, OnGameStateMainStartSpeedrunMode);
}

static RegisterShipInitFunc initFunc(RegisterSpeedrunMode, { CVAR_SPEEDRUN_MODE_NAME });
