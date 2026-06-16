// ship-gz: "gz mode" practice features, modelled on the GameCube practice ROM (gz).
// When the gz toggle is on, the player can drive savestates with the controller alone.
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
// navigable list of the .gzs files in the savestates/ folder.
//
// The menu is a gz-style text overlay drawn on the ImGui foreground draw list; we
// own the controller navigation rather than routing through ImGui's widget nav.

#include <atomic>
#include <algorithm>
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

extern "C" {
extern PlayState* gPlayState;
#include "macros.h"
}

#define CVAR_GZ_MODE_NAME CVAR_CHEAT("GzMode")
#define CVAR_GZ_MODE_DEFAULT 0
#define CVAR_GZ_MODE_VALUE CVarGetInteger(CVAR_GZ_MODE_NAME, CVAR_GZ_MODE_DEFAULT)

// The gz-style top-level menu. Everything is greyed out (a stub) except "return"
// (closes) and "macro", which opens our savestate section -- mirroring gz, where
// save/load states live under the macro page.
struct GzRootItem {
    const char* label;
    bool enabled;
};
static const GzRootItem kRootItems[] = {
    { "return", true },     { "warps", false },   { "scene", false },  { "cheats", false },
    { "inventory", false }, { "equips", false },  { "file", false },   { "macro", true },
    { "watches", false },   { "debug", false },   { "settings", false },
};
static const int kRootCount = (int)(sizeof(kRootItems) / sizeof(kRootItems[0]));

// Macro-screen rows (our savestate section), in display order. "return" first, gz-style.
enum GzMenuEntry {
    GZ_MENU_BACK,
    GZ_MENU_SAVE,
    GZ_MENU_LOAD,
    GZ_MENU_EXPORT,
    GZ_MENU_IMPORT,
    GZ_MENU_SLOT,
    GZ_MENU_COUNT,
};

enum GzScreen {
    GZ_SCREEN_ROOT,
    GZ_SCREEN_MACRO,
    GZ_SCREEN_IMPORT,
};

struct GzImportFile {
    std::string label; // filename shown in the list
    std::string path;  // full path passed to ImportState
};

// Shared between the game-thread update hook and the GUI-thread draw. The scalars are
// atomic; the import file list is guarded by a mutex (it's resized on the game thread
// while the draw thread iterates it).
static std::atomic<bool> sMenuOpen{ false };
static std::atomic<int> sScreen{ GZ_SCREEN_ROOT };
static std::atomic<int> sMenuSel{ 0 };
static std::atomic<int> sImportSel{ 0 };
static std::mutex sImportMutex;
static std::vector<GzImportFile> sImportFiles;

// The overlay window. Kept hidden unless the menu is open, so it never participates in
// the draw loop while closed (an always-shown borderless window left a black artifact
// when the OS window was moved/resized).
static std::shared_ptr<Ship::GuiWindow> sOverlay;

static unsigned int GzCurrentSlot() {
    return OTRGlobals::Instance->gSaveStateMgr->GetCurrentSlot();
}

// Default file a slot suggests in the export dialog.
static std::string GzSlotFilePath(unsigned int slot) {
    return (std::filesystem::path(SaveStateMgr::GetStateDirectory()) /
            ("savestate_" + std::to_string(slot) + ".gzs"))
        .string();
}

// While the menu is open only the D-pad is captured for navigation; the rest of the
// controller still drives the game (gz-style, the game keeps running underneath).
static void GzSuppressDpad(Input* input) {
    const uint16_t dpad = BTN_DUP | BTN_DDOWN | BTN_DLEFT | BTN_DRIGHT;
    input->cur.button &= ~dpad;
    input->press.button &= ~dpad;
    input->rel.button &= ~dpad;
}

// (Re)scan the savestates/ folder for *.gzs and enter the import screen.
static void GzEnterImportScreen() {
    std::vector<GzImportFile> files;
    std::error_code ec;
    const std::filesystem::path dir = SaveStateMgr::GetStateDirectory();
    for (auto it = std::filesystem::directory_iterator(dir, ec);
         !ec && it != std::filesystem::directory_iterator(); it.increment(ec)) {
        if (!it->is_regular_file(ec)) {
            continue;
        }
        const std::filesystem::path& p = it->path();
        if (p.extension() == ".gzs") {
            files.push_back({ p.filename().string(), p.string() });
        }
    }
    std::sort(files.begin(), files.end(),
              [](const GzImportFile& a, const GzImportFile& b) { return a.label < b.label; });
    {
        std::lock_guard<std::mutex> lock(sImportMutex);
        sImportFiles = std::move(files);
    }
    sImportSel.store(0);
    sScreen.store(GZ_SCREEN_IMPORT);
}

// Switch screens, resetting the (shared) row cursor.
static void GzGoToScreen(GzScreen screen) {
    sScreen.store(screen);
    sMenuSel.store(0);
}

static void GzConfirmRootSelection() {
    const int sel = sMenuSel.load();
    if (sel < 0 || sel >= kRootCount) {
        return;
    }
    const GzRootItem& item = kRootItems[sel];
    if (!item.enabled) {
        return; // greyed-out stub
    }
    if (strcmp(item.label, "macro") == 0) {
        GzGoToScreen(GZ_SCREEN_MACRO);
    } else if (strcmp(item.label, "return") == 0) {
        sMenuOpen.store(false);
    }
}

static void GzConfirmMacroSelection() {
    const auto mgr = OTRGlobals::Instance->gSaveStateMgr;
    const unsigned int slot = GzCurrentSlot();
    switch (sMenuSel.load()) {
        case GZ_MENU_SAVE:
            mgr->AddRequest({ slot, RequestType::SAVE });
            break;
        case GZ_MENU_LOAD:
            mgr->AddRequest({ slot, RequestType::LOAD });
            break;
        case GZ_MENU_EXPORT:
            // Export keeps the native save dialog (modal; runs here on the game thread).
            mgr->ExportState(slot, GzPromptExportStatePath(GzSlotFilePath(slot)));
            break;
        case GZ_MENU_IMPORT:
            GzEnterImportScreen();
            break;
        case GZ_MENU_SLOT:
            mgr->SetCurrentSlot((slot + 1) % 6);
            break;
        case GZ_MENU_BACK:
            GzGoToScreen(GZ_SCREEN_ROOT);
            break;
        default:
            break;
    }
}

// Import-screen rows are: [0] "return", then one row per .gzs file.
static void GzConfirmImportSelection() {
    const int sel = sImportSel.load();
    if (sel == 0) {
        GzGoToScreen(GZ_SCREEN_MACRO); // "return" row
        return;
    }
    std::string path;
    {
        std::lock_guard<std::mutex> lock(sImportMutex);
        const int idx = sel - 1;
        if (idx >= 0 && idx < (int)sImportFiles.size()) {
            path = sImportFiles[idx].path;
        }
    }
    if (!path.empty()) {
        OTRGlobals::Instance->gSaveStateMgr->ImportState(GzCurrentSlot(), path);
        GzGoToScreen(GZ_SCREEN_MACRO); // back to the macro screen; apply with Load
    }
}

static void GzHandleRootScreen(Input* input) {
    const uint16_t pressed = input->press.button;
    int sel = sMenuSel.load();

    if (CHECK_BTN_ALL(pressed, BTN_DUP)) {
        sel = (sel + kRootCount - 1) % kRootCount;
    } else if (CHECK_BTN_ALL(pressed, BTN_DDOWN)) {
        sel = (sel + 1) % kRootCount;
    }
    sMenuSel.store(sel);

    if (CHECK_BTN_ALL(pressed, BTN_CDOWN)) {
        GzConfirmRootSelection();
    }
}

static void GzHandleMacroScreen(Input* input) {
    const uint16_t pressed = input->press.button;
    int sel = sMenuSel.load();

    if (CHECK_BTN_ALL(pressed, BTN_DUP)) {
        sel = (sel + GZ_MENU_COUNT - 1) % GZ_MENU_COUNT;
    } else if (CHECK_BTN_ALL(pressed, BTN_DDOWN)) {
        sel = (sel + 1) % GZ_MENU_COUNT;
    }
    sMenuSel.store(sel);

    if (sel == GZ_MENU_SLOT) {
        const unsigned int slot = GzCurrentSlot();
        if (CHECK_BTN_ALL(pressed, BTN_DRIGHT)) {
            OTRGlobals::Instance->gSaveStateMgr->SetCurrentSlot((slot + 1) % 6);
        } else if (CHECK_BTN_ALL(pressed, BTN_DLEFT)) {
            OTRGlobals::Instance->gSaveStateMgr->SetCurrentSlot((slot + 5) % 6);
        }
    }

    if (CHECK_BTN_ALL(pressed, BTN_CDOWN)) {
        GzConfirmMacroSelection();
    }
}

static void GzHandleImportScreen(Input* input) {
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
        GzConfirmImportSelection();
    }
}

static void OnGameStateMainStartGzMode() {
    if (!GameInteractor::IsSaveLoaded(true) || gPlayState == nullptr) {
        return;
    }
    if (CVarGetInteger(CVAR_CHEAT("SaveStatesEnabled"), 0) == 0) {
        return;
    }

    // Show the overlay only while the menu is open (avoids a stray black window when
    // closed). One-frame lag on first draw is imperceptible.
    if (sOverlay != nullptr && sOverlay->IsVisible() != sMenuOpen.load()) {
        if (sMenuOpen.load()) {
            sOverlay->Show();
        } else {
            sOverlay->Hide();
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
            GzSuppressDpad(input);
            return;
        }
        // Quick hotkeys while playing.
        if (CHECK_BTN_ALL(input->press.button, BTN_DLEFT)) {
            OTRGlobals::Instance->gSaveStateMgr->AddRequest({ GzCurrentSlot(), RequestType::SAVE });
        } else if (CHECK_BTN_ALL(input->press.button, BTN_DRIGHT)) {
            OTRGlobals::Instance->gSaveStateMgr->AddRequest({ GzCurrentSlot(), RequestType::LOAD });
        }
        return;
    }

    // Menu is open. R + C-Down closes it; a bare C-Down confirms (handled per screen).
    if (rHeld && cDownPressed) {
        sMenuOpen.store(false);
    } else if (sScreen.load() == GZ_SCREEN_IMPORT) {
        GzHandleImportScreen(input);
    } else if (sScreen.load() == GZ_SCREEN_MACRO) {
        GzHandleMacroScreen(input);
    } else {
        GzHandleRootScreen(input);
    }

    GzSuppressDpad(input); // only the D-pad is captured; the game keeps the rest
}

// Borderless, input-less overlay window; we draw through the foreground draw list so
// the window itself is just a hook to get DrawElement() called each frame.
class GzMenuOverlay final : public Ship::GuiWindow {
  public:
    using GuiWindow::GuiWindow;

  protected:
    void InitElement() override {
    }
    void UpdateElement() override {
    }
    void DrawElement() override {
        if (!sMenuOpen.load()) {
            return;
        }
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

        if (sScreen.load() == GZ_SCREEN_IMPORT) {
            DrawImportScreen(overlay);
        } else if (sScreen.load() == GZ_SCREEN_MACRO) {
            DrawMacroScreen(overlay);
        } else {
            DrawRootScreen(overlay);
        }

        ImGui::SetWindowFontScale(1.0f);
    }

  private:
    float mScale = 1.0f; // resolution-based UI scale, set each frame in DrawElement

    // Draws a gz-style list: the overlay's pixel font ("Press Start 2P") with a drop
    // shadow, left-aligned near the left edge and vertically about a fifth down, the
    // selected row tinted gz-blue (no cursor arrow). If `enabled` is supplied, disabled
    // rows are dimmed (a greyed-out gz stub).
    void DrawList(const std::shared_ptr<Ship::GameOverlay>& overlay, const std::vector<std::string>& rows, int sel,
                  const std::vector<bool>* enabled = nullptr) {
        const ImVec4 white(1.0f, 1.0f, 1.0f, 1.0f);
        const ImVec4 blue(0.45f, 0.62f, 1.0f, 1.0f); // gz selection colour
        const ImVec4 dim(0.5f, 0.5f, 0.5f, 1.0f);
        const ImVec4 dimSel(0.4f, 0.5f, 0.7f, 1.0f);

        // CalculateTextSize returns the unscaled glyph height, so scale our spacing to
        // match the SetWindowFontScale applied to the text itself. gz uses tight spacing.
        const float lineH = (overlay->CalculateTextSize("Ag").y + 1.0f) * mScale;

        // gz anchors the menu to the left edge, roughly a fifth of the way down.
        const ImGuiViewport* vp = ImGui::GetMainViewport();
        const float x = vp->Size.x * 0.045f;
        float y = vp->Size.y * 0.22f;

        for (int i = 0; i < (int)rows.size(); i++) {
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
        const unsigned int slot = GzCurrentSlot();
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
            rows.push_back("(no .gzs files)"); // informational, not selectable
        }
        DrawList(overlay, rows, sel);
    }
};

static void GzEnsureOverlayRegistered() {
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
    auto overlay = std::make_shared<GzMenuOverlay>(
        "", false, "gz menu overlay", ImVec2(10, 10),
        ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove |
            ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoBackground | ImGuiWindowFlags_NoSavedSettings |
            ImGuiWindowFlags_NoInputs | ImGuiWindowFlags_NoFocusOnAppearing | ImGuiWindowFlags_NoNav);
    gui->AddGuiWindow(overlay);
    overlay->Hide(); // shown only while the menu is open (see the hook)
    sOverlay = overlay;
    registered = true;
}

void RegisterGzMode() {
    GzEnsureOverlayRegistered();
    COND_HOOK(OnGameStateMainStart, CVAR_GZ_MODE_VALUE, OnGameStateMainStartGzMode);
}

static RegisterShipInitFunc initFunc(RegisterGzMode, { CVAR_GZ_MODE_NAME });
