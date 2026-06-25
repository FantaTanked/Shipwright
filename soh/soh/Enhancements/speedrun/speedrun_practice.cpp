// Controller-driven speedrun practice overlay modelled on the GameCube practice ROM: savestates, warps, watches.
// The menu is a text overlay drawn on the ImGui foreground draw list with our own controller navigation.

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

// Top-level menu. Most rows are greyed-out stubs; "return" closes, "macro" opens the savestate section.
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

// Macro-screen rows, in display order, modelled on gz's macro menu. "return" first, speedrun-style.
enum SpeedrunMenuEntry {
    SPEEDRUN_MENU_BACK,
    SPEEDRUN_MENU_RECORD,       // toggle recording (input only; no state save)
    SPEEDRUN_MENU_PLAY,         // toggle playback from the current macro frame
    SPEEDRUN_MENU_REWIND,       // seek macro to frame 0
    SPEEDRUN_MENU_TRIM,         // truncate the macro at the current frame
    SPEEDRUN_MENU_MACRO_FRAME,  // show/seek the current macro frame (D-left/right)
    SPEEDRUN_MENU_QUICK_RECORD, // rewind + anchor (slots 0/1) + record
    SPEEDRUN_MENU_QUICK_PLAY,   // load slot-0 anchor + play from frame 0
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

// Shared between the game-thread update hook and the GUI-thread draw: scalars are atomic, and
// the import file list (resized on the game thread, iterated on the draw thread) is mutex-guarded.
static std::atomic<bool> sMenuOpen{ false };
static std::atomic<bool> sPaused{ false }; // game frozen via the native FrameAdvance gate (drives the pause icon)
static std::atomic<int> sScreen{ SPEEDRUN_SCREEN_ROOT };
static std::atomic<int> sMenuSel{ 0 };
static std::atomic<int> sImportSel{ 0 };
static std::mutex sImportMutex;
static std::vector<SpeedrunImportFile> sImportFiles;

// Warps browser: one cursor per nav level plus the chosen category/place. Names come from read-only
// static tables, so the draw thread can query them directly without a snapshot or mutex.
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

// Macro/movie record + playback, modelled on gz: ONE in-memory clip of the per-frame Input the game consumed,
// plus a per-slot "macro frame" stamp so a state saved mid-macro remembers its position and loading it seeks
// the macro there (gz's re-record workflow). The clip is touched only on the game thread; the draw thread
// reads the atomics below, never the vector.
enum SpeedrunMovieState { MOVIE_IDLE, MOVIE_RECORDING, MOVIE_PLAYING };
static std::atomic<int> sMovieState{ MOVIE_IDLE };
static std::atomic<size_t> sMovieFrame{ 0 };  // playhead/record cursor (gz movie_frame)
static std::atomic<size_t> sMovieLength{ 0 }; // sMovieInput.size() mirror for the draw thread
static std::atomic<int> sMovieRerecords{ 0 }; // bumps when recording over an already-recorded frame
static int sMovieLastRecordedFrame = -1;      // game-thread only
static std::vector<Input> sMovieInput;        // game-thread only
static const int kSpeedrunSlotCount = 6;      // savestate slots the UI cycles through (0..5)
static int sSlotMovieFrame[kSpeedrunSlotCount] = { -1, -1, -1, -1, -1, -1 }; // -1 = not movie-linked

// The overlay window. Kept hidden unless the menu is open so it never draws while closed
// (an always-shown borderless window leaves a visual artifact when the OS window is moved/resized).
static std::shared_ptr<Ship::GuiWindow> sOverlay;

static unsigned int SpeedrunCurrentSlot() {
    return OTRGlobals::Instance->gSaveStateMgr->GetCurrentSlot();
}

// Default name suggested in the export dialog, with no ".st" extension: the dialog appends it,
// so you can type a plain name without working around the suffix.
static std::string SpeedrunSlotFilePath(unsigned int slot) {
    return (std::filesystem::path(SaveStateMgr::GetStateDirectory()) / ("savestate_" + std::to_string(slot)))
        .string();
}

// D-pad up/down auto-repeat: navigation is edge-triggered, so after an initial delay we synthesize extra
// press events at a fixed interval while up/down is held, in game frames (~20/s).
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

// --- Macro/movie helpers (gz model); all run on the game thread ---

static void SpeedrunMovieSeek(size_t frame) {
    sMovieFrame.store(frame > sMovieInput.size() ? sMovieInput.size() : frame);
}

static void SpeedrunMovieRewind() {
    sMovieFrame.store(0);
}

// Truncate the clip at the current frame (drops everything after the playhead).
static void SpeedrunMovieTrim() {
    const size_t f = sMovieFrame.load();
    if (f < sMovieInput.size()) {
        sMovieInput.resize(f);
        sMovieLength.store(sMovieInput.size());
    }
}

static void SpeedrunMovieToggleRecord() {
    sMovieState.store(sMovieState.load() == MOVIE_RECORDING ? MOVIE_IDLE : MOVIE_RECORDING);
}

static void SpeedrunMovieTogglePlay() {
    if (sMovieState.load() == MOVIE_PLAYING) {
        sMovieState.store(MOVIE_IDLE);
    } else if (!sMovieInput.empty()) {
        if (sMovieFrame.load() >= sMovieInput.size()) {
            sMovieFrame.store(0); // parked at the end: restart from 0
        }
        sMovieState.store(MOVIE_PLAYING);
    }
}

// Save a state and stamp it with the current macro frame (or -1 when idle), so a later load can seek the
// macro back to this point (gz's re-record workflow).
static void SpeedrunDoSaveState(unsigned int slot) {
    OTRGlobals::Instance->gSaveStateMgr->AddRequest({ slot, RequestType::SAVE });
    if (slot < (unsigned int)kSpeedrunSlotCount) {
        sSlotMovieFrame[slot] = (sMovieState.load() == MOVIE_IDLE) ? -1 : (int)sMovieFrame.load();
    }
}

// Load a state and, if a macro is active and the state is movie-linked, seek the macro to the stamped frame.
static void SpeedrunDoLoadState(unsigned int slot) {
    OTRGlobals::Instance->gSaveStateMgr->AddRequest({ slot, RequestType::LOAD });
    if (sMovieState.load() != MOVIE_IDLE && slot < (unsigned int)kSpeedrunSlotCount && sSlotMovieFrame[slot] >= 0) {
        SpeedrunMovieSeek((size_t)sSlotMovieFrame[slot]);
    }
}

// gz "quick record movie": rewind, start recording, and drop a frame-0 anchor in slot 0 (+ working slot 1).
static void SpeedrunMovieQuickRecord() {
    SpeedrunMovieRewind();
    sMovieState.store(MOVIE_RECORDING);
    SpeedrunDoSaveState(0);
    SpeedrunDoSaveState(1);
}

// gz "quick play movie": if slot 0 holds the frame-0 anchor and a clip exists, load it and play from the start.
static void SpeedrunMovieQuickPlay() {
    if (sSlotMovieFrame[0] == 0 && !sMovieInput.empty()) {
        SpeedrunMovieRewind();
        sMovieState.store(MOVIE_PLAYING);
        SpeedrunDoLoadState(0);
    }
}

// Per-frame record/playback, run after the command layer on every advancing game frame (frozen frames during
// frame-advance don't count, matching gz). Recording overwrites in-range and appends past the end; playback
// feeds the recorded Input verbatim and stops at the end.
static void SpeedrunMovieTick(Input* input, bool advancing) {
    const int state = sMovieState.load();
    if (state == MOVIE_IDLE || !advancing) {
        return;
    }
    if (state == MOVIE_RECORDING) {
        const size_t f = sMovieFrame.load();
        if (f >= sMovieInput.size()) {
            sMovieInput.resize(f + 1);
            sMovieLength.store(sMovieInput.size());
        }
        if (sMovieLastRecordedFrame >= (int)f) {
            sMovieRerecords.fetch_add(1);
        }
        sMovieLastRecordedFrame = (int)f;
        sMovieInput[f] = *input;
        sMovieFrame.store(f + 1);
    } else { // MOVIE_PLAYING
        const size_t f = sMovieFrame.load();
        if (f >= sMovieInput.size()) {
            sMovieState.store(MOVIE_IDLE); // end of clip: play once, then return control (gz loops only on hold)
            return;
        }
        *input = sMovieInput[f];
        sMovieFrame.store(f + 1);
    }
}

// Populate the import list from `dir`: a ".." entry (unless at the savestates root), then subfolders,
// then .st files, with folders and files each sorted alphabetically.
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
        case SPEEDRUN_MENU_RECORD:
            SpeedrunMovieToggleRecord();
            if (sMovieState.load() == MOVIE_RECORDING) {
                sMenuOpen.store(false); // just started: close so you can play the game
            }
            break;
        case SPEEDRUN_MENU_PLAY:
            SpeedrunMovieTogglePlay();
            if (sMovieState.load() == MOVIE_PLAYING) {
                sMenuOpen.store(false); // just started: close so the playback is visible
            }
            break;
        case SPEEDRUN_MENU_REWIND:
            SpeedrunMovieRewind();
            break;
        case SPEEDRUN_MENU_TRIM:
            SpeedrunMovieTrim();
            break;
        case SPEEDRUN_MENU_MACRO_FRAME:
            break; // seek with D-left/right (handled in the screen handler)
        case SPEEDRUN_MENU_QUICK_RECORD:
            SpeedrunMovieQuickRecord();
            sMenuOpen.store(false);
            break;
        case SPEEDRUN_MENU_QUICK_PLAY:
            SpeedrunMovieQuickPlay();
            if (sMovieState.load() == MOVIE_PLAYING) {
                sMenuOpen.store(false);
            }
            break;
        case SPEEDRUN_MENU_SAVE:
            SpeedrunDoSaveState(slot);
            break;
        case SPEEDRUN_MENU_LOAD:
            SpeedrunDoLoadState(slot);
            break;
        case SPEEDRUN_MENU_EXPORT:
            // Export keeps the native save dialog (modal; runs here on the game thread).
            mgr->ExportState(slot, SpeedrunPromptExportStatePath(SpeedrunSlotFilePath(slot)));
            break;
        case SPEEDRUN_MENU_IMPORT:
            SpeedrunEnterImportScreen();
            break;
        case SPEEDRUN_MENU_SLOT:
            mgr->SetCurrentSlot((slot + 1) % kSpeedrunSlotCount);
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
            OTRGlobals::Instance->gSaveStateMgr->SetCurrentSlot((slot + 1) % kSpeedrunSlotCount);
        } else if (CHECK_BTN_ALL(pressed, BTN_DLEFT)) {
            OTRGlobals::Instance->gSaveStateMgr->SetCurrentSlot((slot + kSpeedrunSlotCount - 1) % kSpeedrunSlotCount);
        }
    } else if (sel == SPEEDRUN_MENU_MACRO_FRAME) {
        const size_t frame = sMovieFrame.load();
        if (CHECK_BTN_ALL(pressed, BTN_DRIGHT)) {
            SpeedrunMovieSeek(frame + 1);
        } else if (CHECK_BTN_ALL(pressed, BTN_DLEFT) && frame > 0) {
            SpeedrunMovieSeek(frame - 1);
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
        const bool want =
            sMenuOpen.load() || SpeedrunWatch_Count() > 0 || sPaused.load() || sMovieState.load() != MOVIE_IDLE;
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

    // Drive the native FrameAdvance gate from our own sPaused flag each frame. sPaused isn't part of the
    // savestate, so the freeze state stays independent of save/load.
    gPlayState->frameAdvCtx.enabled = sPaused.load() ? 1 : 0;

    // Command layer: reads the live (physical) pad. It runs BEFORE the macro driver overwrites the pad on
    // playback, so a playing macro never re-triggers these hotkeys and you can still drive the menu mid-playback.
    if (!sMenuOpen.load()) {
        if (rHeld && cDownPressed) {
            // R + C-Down opens the menu, returning to the screen/selection active when last closed.
            sMenuOpen.store(true);
            SpeedrunSuppressDpad(input);
        } else if (CHECK_BTN_ALL(input->press.button, BTN_DLEFT)) {
            SpeedrunDoSaveState(SpeedrunCurrentSlot());
        } else if (CHECK_BTN_ALL(input->press.button, BTN_DRIGHT)) {
            SpeedrunDoLoadState(SpeedrunCurrentSlot());
        } else if (CHECK_BTN_ALL(input->press.button, BTN_DUP)) {
            // Frame advance: first press freezes on the current frame, each further press steps one frame.
            if (sPaused.load()) {
                CVarSetInteger(CVAR_DEVELOPER_TOOLS("FrameAdvanceTick"), 1); // step exactly one frame
            } else {
                sPaused.store(true); // freeze
            }
        } else if (CHECK_BTN_ALL(input->press.button, BTN_DDOWN)) {
            sPaused.store(false); // resume
        }
    } else {
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
        // C-Down drives the menu (confirm / close), so don't let it also use the C-Down item underneath.
        input->cur.button &= ~BTN_CDOWN;
        input->press.button &= ~BTN_CDOWN;
        input->rel.button &= ~BTN_CDOWN;
    }

    // Macro driver (gz-style): runs once per advancing game frame, AFTER the command layer. Frozen frames
    // during frame-advance don't count.
    const bool advancing = !sPaused.load() || CVarGetInteger(CVAR_DEVELOPER_TOOLS("FrameAdvanceTick"), 0) != 0;
    SpeedrunMovieTick(input, advancing);
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

        // The overlay font is a fixed pixel size, so scale both the glyphs and our layout coordinates by
        // viewport height to stay readable on high-res displays. The 1.5 floor keeps it legible when small.
        mScale = std::max(1.5f, vp->Size.y / 540.0f);
        ImGui::SetWindowFontScale(mScale);

        // Active watches draw on screen whether or not the menu is open.
        DrawWatches(overlay);

        // Frame-advance pause indicator: two bars on a dark backdrop, top-centre, shown whenever the game is
        // frozen, so the pause is visible even with the menu closed.
        if (sPaused.load()) {
            ImDrawList* dl = ImGui::GetForegroundDrawList();
            const float cx = vp->Pos.x + vp->Size.x * 0.5f;
            const float cy = vp->Pos.y + vp->Size.y * 0.065f;
            const float h = vp->Size.y * 0.04f; // bar height
            const float w = h * 0.34f;          // bar width
            const float gap = w * 0.8f;         // gap between the two bars
            const float pad = h * 0.45f;
            dl->AddRectFilled(ImVec2(cx - gap * 0.5f - w - pad, cy - h * 0.5f - pad),
                              ImVec2(cx + gap * 0.5f + w + pad, cy + h * 0.5f + pad), IM_COL32(0, 0, 0, 130),
                              h * 0.3f);
            const ImU32 bar = IM_COL32(255, 255, 255, 235);
            dl->AddRectFilled(ImVec2(cx - gap * 0.5f - w, cy - h * 0.5f), ImVec2(cx - gap * 0.5f, cy + h * 0.5f), bar);
            dl->AddRectFilled(ImVec2(cx + gap * 0.5f, cy - h * 0.5f), ImVec2(cx + gap * 0.5f + w, cy + h * 0.5f), bar);
        }

        // Macro record/playback indicator (frame / length), shown with the menu closed, under the pause bar.
        {
            const int mstate = sMovieState.load();
            if (mstate == MOVIE_RECORDING) {
                overlay->TextDraw(vp->Size.x * 0.045f, vp->Size.y * 0.11f, true, ImVec4(1.0f, 0.35f, 0.35f, 1.0f),
                                  "* REC %u / %u", (unsigned)sMovieFrame.load(), (unsigned)sMovieLength.load());
            } else if (mstate == MOVIE_PLAYING) {
                overlay->TextDraw(vp->Size.x * 0.045f, vp->Size.y * 0.11f, true, ImVec4(0.4f, 1.0f, 0.55f, 1.0f),
                                  "> PLAY %u / %u", (unsigned)sMovieFrame.load(), (unsigned)sMovieLength.load());
            }
        }

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

    // Draws a list in the overlay's pixel font, anchored near the left edge about a fifth down, with the
    // selected row tinted blue. If `enabled` is supplied, disabled rows are dimmed.
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
        const int mstate = sMovieState.load();
        const size_t len = sMovieLength.load();
        const size_t frame = sMovieFrame.load();
        const bool hasClip = len > 0;
        std::vector<std::string> rows = {
            "return",
            mstate == MOVIE_RECORDING ? "record macro (recording)" : "record macro",
            mstate == MOVIE_PLAYING ? "play macro (playing)" : "play macro",
            "rewind macro",
            "trim macro",
            "macro frame < " + std::to_string(frame) + " / " + std::to_string(len) + " >",
            "quick record movie",
            "quick play movie",
            "save state",
            "load state",
            "export state to disk",
            "import state from disk",
            "slot < " + std::to_string(slot) + " >",
        };
        const std::vector<bool> enabled = {
            true,                               // return
            true,                               // record macro
            hasClip,                            // play macro
            hasClip,                            // rewind
            hasClip,                            // trim
            hasClip,                            // macro frame
            true,                               // quick record
            sSlotMovieFrame[0] == 0 && hasClip, // quick play (needs the slot-0 frame-0 anchor)
            true, true, true, true, true,       // save / load / export / import / slot
        };
        DrawList(overlay, rows, sMenuSel.load(), &enabled);
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

    // Draws each watch's value at its stored (unscaled) position; hold Alt and left-drag one to reposition it.
    // The active/dragged watch is tinted blue; drag positions are pushed to the game thread and saved on release.
    void DrawWatches(const std::shared_ptr<Ship::GameOverlay>& overlay) {
        const std::vector<SpeedrunWatchDisplay> snap = SpeedrunWatch_Snapshot();
        const ImVec4 white(1.0f, 1.0f, 1.0f, 1.0f);
        const ImVec4 blue(0.45f, 0.62f, 1.0f, 1.0f);
        const bool positioning = (sScreen.load() == SPEEDRUN_SCREEN_WATCH_EDIT) && sWatchPositioning.load();
        const int editIdx = sWatchEditIdx.load();

        ImGuiIO& io = ImGui::GetIO();
        const bool altDrag = io.KeyAlt && io.MouseDown[0];

        // Drags snap to an 8px grid so watches line up; hold Shift for free fine positioning. mDragX/Y track
        // the raw accumulated drag, while the snapped value is what we draw and store.
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
