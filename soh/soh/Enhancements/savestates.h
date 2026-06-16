#ifndef SAVE_STATES_H
#define SAVE_STATES_H

#include <stdint.h>
#include <queue>
#include <unordered_map>
#include <memory>
#include <mutex>
#include <string>

enum class SaveStateReturn {
    SUCCESS,
    FAIL_INVALID_SLOT,
    FAIL_NO_MEMORY,
    FAIL_STATE_EMPTY,
    FAIL_WRONG_GAMESTATE,
    FAIL_BAD_REQUEST,
    FAIL_IO,
    FAIL_BAD_FILE,
    FAIL_BUILD_MISMATCH,
};

// ship-gz: on-disk savestate header. buildKey ties a file to the exact binary
// that wrote it (code base address + struct layout); a mismatch is rejected
// rather than crash-loaded, because cross-session states embed raw pointers that
// are only valid for the same ASLR-disabled build.
#define GZ_SAVESTATE_MAGIC   0x475A5353u /* 'GZSS' */
#define GZ_SAVESTATE_VERSION 2u /* v2: no per-state arena snapshot (arena cache supplies it) */

typedef struct SaveStateHeader {
    uint32_t stateMagic;
    uint32_t stateVersion;
    uint64_t buildKey;
    uint64_t infoSize;
} SaveStateHeader;

enum class RequestType {
    SAVE,
    LOAD,
    EXPORT,
    IMPORT,
};

typedef struct SaveStateRequest {
    unsigned int slot;
    RequestType type;
    // For EXPORT/IMPORT: the user-chosen file path. Empty for SAVE/LOAD.
    std::string path;
} SaveStateRequest;

class SaveState;

class SaveStateMgr {
    friend class SaveState;

  private:
    unsigned int currentSlot;
    std::unordered_map<unsigned int, std::shared_ptr<SaveState>> states;
    std::queue<SaveStateRequest> requests;
    std::mutex mutex;

    // Deferred reload-then-restore for cross-session (disk-imported) loads: the
    // saved scene's resources don't exist on a fresh boot, so we reload the saved
    // entrance first and apply the restore once the reload completes.
    bool deferredRestorePending = false;
    unsigned int deferredSlot = 0;
    int16_t deferredScene = -1;
    int deferredFramesWaited = 0;

  public:
    SaveStateReturn AddRequest(const SaveStateRequest request);
    SaveStateMgr();
    ~SaveStateMgr();

    void SetCurrentSlot(unsigned int slot);
    unsigned int GetCurrentSlot(void);

    // Persist the in-memory state for `slot` to the user-chosen file `path` / read
    // `path` from disk into `slot`. Routed through the request queue so the states
    // map is only touched on the game thread. After ImportState the slot can be
    // applied with LOAD. The file dialog itself is driven by the caller (so the
    // modal dialog runs off the game thread); only the resolved path is passed here.
    SaveStateReturn ExportState(unsigned int slot, const std::string& path);
    SaveStateReturn ImportState(unsigned int slot, const std::string& path);

    // Default directory for exported/imported states ("<exe>/savestates").
    static std::string GetStateDirectory(void);

    SaveStateMgr& operator=(const SaveStateMgr& rhs) = delete;
    SaveStateMgr(const SaveStateMgr& rhs) = delete;

    void ProcessSaveStateRequests(void);
};
extern std::shared_ptr<SaveStateMgr> gSaveStateMgr;

#endif
