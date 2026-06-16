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

// Main-screen rows, in display order.
enum GzMenuEntry {
    GZ_MENU_SAVE,
    GZ_MENU_LOAD,
    GZ_MENU_EXPORT,
    GZ_MENU_IMPORT,
    GZ_MENU_SLOT,
    GZ_MENU_CLOSE,
    GZ_MENU_COUNT,
};

enum GzScreen {
    GZ_SCREEN_MAIN,
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
static std::atomic<int> sScreen{ GZ_SCREEN_MAIN };
static std::atomic<int> sMenuSel{ 0 };
static std::atomic<int> sImportSel{ 0 };
static std::mutex sImportMutex;
static std::vector<GzImportFile> sImportFiles;

static unsigned int GzCurrentSlot() {
    return OTRGlobals::Instance->gSaveStateMgr->GetCurrentSlot();
}

// Default file a slot suggests in the export dialog.
static std::string GzSlotFilePath(unsigned int slot) {
    return (std::filesystem::path(SaveStateMgr::GetStateDirectory()) /
            ("savestate_" + std::to_string(slot) + ".gzs"))
        .string();
}

static void GzSuppressGameInput(Input* input) {
    input->cur.button = 0;
    input->press.button = 0;
    input->rel.button = 0;
    input->cur.stick_x = 0;
    input->cur.stick_y = 0;
    input->rel.stick_x = 0;
    input->rel.stick_y = 0;
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

static void GzConfirmMainSelection() {
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
        case GZ_MENU_CLOSE:
            sMenuOpen.store(false);
            break;
        default:
            break;
    }
}

static void GzConfirmImportSelection() {
    std::string path;
    {
        std::lock_guard<std::mutex> lock(sImportMutex);
        const int sel = sImportSel.load();
        if (sel >= 0 && sel < (int)sImportFiles.size()) {
            path = sImportFiles[sel].path;
        }
    }
    if (!path.empty()) {
        OTRGlobals::Instance->gSaveStateMgr->ImportState(GzCurrentSlot(), path);
        sScreen.store(GZ_SCREEN_MAIN); // back to the main screen; apply with Load
    }
}

static void GzHandleMainScreen(Input* input) {
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
        GzConfirmMainSelection();
    }
}

static void GzHandleImportScreen(Input* input) {
    const uint16_t pressed = input->press.button;

    int count;
    {
        std::lock_guard<std::mutex> lock(sImportMutex);
        count = (int)sImportFiles.size();
    }

    if (count > 0) {
        int sel = sImportSel.load();
        if (CHECK_BTN_ALL(pressed, BTN_DUP)) {
            sel = (sel + count - 1) % count;
        } else if (CHECK_BTN_ALL(pressed, BTN_DDOWN)) {
            sel = (sel + 1) % count;
        }
        sImportSel.store(sel);
        if (CHECK_BTN_ALL(pressed, BTN_CDOWN)) {
            GzConfirmImportSelection();
            return;
        }
    }

    if (CHECK_BTN_ALL(pressed, BTN_B)) {
        sScreen.store(GZ_SCREEN_MAIN);
    }
}

static void OnGameStateMainStartGzMode() {
    if (!GameInteractor::IsSaveLoaded(true) || gPlayState == nullptr) {
        return;
    }
    if (CVarGetInteger(CVAR_CHEAT("SaveStatesEnabled"), 0) == 0) {
        return;
    }

    Input* input = &gPlayState->state.input[0];
    const bool rHeld = CHECK_BTN_ALL(input->cur.button, BTN_R);
    const bool cDownPressed = CHECK_BTN_ALL(input->press.button, BTN_CDOWN);

    if (!sMenuOpen.load()) {
        // R + C-Down opens the menu.
        if (rHeld && cDownPressed) {
            sMenuOpen.store(true);
            sScreen.store(GZ_SCREEN_MAIN);
            GzSuppressGameInput(input); // don't let the opening combo reach the game
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
    } else {
        GzHandleMainScreen(input);
    }

    GzSuppressGameInput(input); // menu owns all input while open
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

        if (sScreen.load() == GZ_SCREEN_IMPORT) {
            DrawImportScreen(overlay);
        } else {
            DrawMainScreen(overlay);
        }
    }

  private:
    // Draws the gz-style list: the overlay's pixel font ("Press Start 2P") with a drop
    // shadow, a title, a '>' cursor + highlight colour on the selected row, and a footer.
    void DrawList(const std::shared_ptr<Ship::GameOverlay>& overlay, const char* title,
                  const std::vector<std::string>& rows, int sel, const char* footer) {
        const ImVec4 white(1.0f, 1.0f, 1.0f, 1.0f);
        const ImVec4 yellow(1.0f, 0.85f, 0.0f, 1.0f);
        const ImVec4 grey(0.66f, 0.66f, 0.66f, 1.0f);

        const float lineH = overlay->CalculateTextSize("Ag").y + 4.0f;
        const float x = 16.0f;
        float y = 16.0f;

        overlay->TextDraw(x, y, true, yellow, "%s", title);
        y += lineH * 1.5f;

        for (int i = 0; i < (int)rows.size(); i++) {
            const bool selected = (i == sel);
            overlay->TextDraw(x, y, true, selected ? yellow : white, "%s%s", selected ? "> " : "  ",
                              rows[i].c_str());
            y += lineH;
        }

        if (footer) {
            y += lineH * 0.5f;
            overlay->TextDraw(x, y, true, grey, "%s", footer);
        }
    }

    void DrawMainScreen(const std::shared_ptr<Ship::GameOverlay>& overlay) {
        const unsigned int slot = GzCurrentSlot();
        std::vector<std::string> rows = {
            "Save state",
            "Load state",
            "Export slot to disk...",
            "Import slot from disk",
            "Slot: < " + std::to_string(slot) + " >",
            "Close",
        };
        DrawList(overlay, "-gz practice-", rows, sMenuSel.load(), "C-Down: select   R+C-Down: close");
    }

    void DrawImportScreen(const std::shared_ptr<Ship::GameOverlay>& overlay) {
        std::vector<std::string> rows;
        int sel;
        {
            std::lock_guard<std::mutex> lock(sImportMutex);
            for (const auto& f : sImportFiles) {
                rows.push_back(f.label);
            }
            sel = sImportSel.load();
        }
        if (rows.empty()) {
            rows.push_back("(no .gzs files in savestates/)");
            sel = -1;
        }
        DrawList(overlay, "-import savestate-", rows, sel, "C-Down: import   B: back");
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
    auto overlay = std::make_shared<GzMenuOverlay>(
        CVAR_WINDOW("GzMenuOverlay"), true, "gz menu overlay", ImVec2(10, 10),
        ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove |
            ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoBackground | ImGuiWindowFlags_NoSavedSettings |
            ImGuiWindowFlags_NoInputs | ImGuiWindowFlags_NoFocusOnAppearing | ImGuiWindowFlags_NoNav);
    gui->AddGuiWindow(overlay);
    overlay->Show(); // always "shown"; DrawElement no-ops unless the menu is open
    registered = true;
}

void RegisterGzMode() {
    GzEnsureOverlayRegistered();
    COND_HOOK(OnGameStateMainStart, CVAR_GZ_MODE_VALUE, OnGameStateMainStartGzMode);
}

static RegisterShipInitFunc initFunc(RegisterGzMode, { CVAR_GZ_MODE_NAME });
