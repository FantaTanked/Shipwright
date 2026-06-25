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
};

typedef struct SaveStateHeader {
    uint32_t stateMagic;
    uint32_t stateVersion;
    uint64_t infoSize; // sizeof(SaveStateInfo) sentinel; rejects struct-layout drift across builds
    uint64_t exeBase;  // soh.exe image base at save time; the load-time delta relocates code/function pointers
    uint32_t exeSize;  // soh.exe image size at save time (the relocatable range)
    uint32_t resCount; // number of SaveStateResEntry records that make up the relocation table
    uint32_t compression;          // body codec: 0 = stored raw, 1 = zlib (StormLib SComp). Added in disk v6.
    uint32_t pad;                  // alignment for the u64s below
    uint64_t bodyUncompressedSize; // = sizeof(SaveStateInfo) + resCount*sizeof(SaveStateResEntry)
    uint64_t bodyCompressedSize;   // bytes of the body region on disk (== bodyUncompressedSize when stored raw)
} SaveStateHeader;

// One relocatable block saved for a resource: subIndex == -1 is its main payload, subIndex >= 0 is the i-th
// sub-allocation. On load the resource is reloaded by name and captured pointers within the block are relocated.
typedef struct SaveStateResEntry {
    uint64_t oldBase;  // host VA of this block at save time
    uint64_t oldSize;  // byte size of this block at save time
    int32_t subIndex;  // -1 = main payload; >= 0 = the i-th GetSubAllocations() block
    uint32_t pad;      // keep 8-byte alignment
    char name[256];    // OTR virtual path, NUL-terminated
} SaveStateResEntry;

enum class RequestType {
    SAVE,
    LOAD,
    SAVE_TO_DISK,
    LOAD_FROM_DISK,
};

typedef struct SaveStateRequest {
    unsigned int slot;
    RequestType type;
    std::string path; // SAVE_TO_DISK/LOAD_FROM_DISK only: an explicit export/import file; empty => the per-slot file
} SaveStateRequest;

class SaveState;

class SaveStateMgr {
    friend class SaveState;

  private:
    unsigned int currentSlot;
    std::unordered_map<unsigned int, std::shared_ptr<SaveState>> states;
    std::queue<SaveStateRequest> requests;
    std::mutex mutex;

    // Return slot `slot`'s state, lazily creating an empty one if it doesn't exist yet.
    std::shared_ptr<SaveState>& EnsureSlot(unsigned int slot);

  public:
    SaveStateReturn AddRequest(const SaveStateRequest request);
    SaveStateMgr();
    ~SaveStateMgr();

    void SetCurrentSlot(unsigned int slot);
    unsigned int GetCurrentSlot(void);

    // Export/import a slot to/from an arbitrary file, routed through the same disk path as the per-slot saves
    // so an imported state is pointer-relocated on load. Both enqueue a request that runs on the graph thread.
    static std::string GetStateDirectory(void);
    SaveStateReturn ExportState(unsigned int slot, const std::string& path);
    SaveStateReturn ImportState(unsigned int slot, const std::string& path);

    SaveStateMgr& operator=(const SaveStateMgr& rhs) = delete;
    SaveStateMgr(const SaveStateMgr& rhs) = delete;

    void ProcessSaveStateRequests(void);
};
extern std::shared_ptr<SaveStateMgr> gSaveStateMgr;

#endif
