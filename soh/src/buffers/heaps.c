#include "z64.h"
#include <assert.h>
#if !defined(__APPLE__) && !defined(__OpenBSD__)
#include <malloc.h>
#endif
#include <stdlib.h>
#include <stdint.h>
#include <stddef.h>

#ifndef _MSC_VER
#include <unistd.h>
#endif

#ifdef _WIN64
// 64-bit Windows only: pin the game heaps at fixed virtual addresses so a savestate's intra-heap pointers stay
// valid across a restart. Bases sit high in x64 space (32 TiB); on failure we fall back, clearing gHeapsArePinned.
extern void* VirtualAlloc(void* lpAddress, size_t dwSize, unsigned long flAllocationType, unsigned long flProtect);
extern int VirtualFree(void* lpAddress, size_t dwSize, unsigned long dwFreeType);
#define SOH_MEM_COMMIT 0x00001000ul
#define SOH_MEM_RESERVE 0x00002000ul
#define SOH_MEM_RELEASE 0x00008000ul
#define SOH_PAGE_READWRITE 0x04ul
#define SYSTEM_HEAP_FIXED_BASE ((void*)(uintptr_t)0x0000200000000000ull)
#define AUDIO_HEAP_FIXED_BASE ((void*)(uintptr_t)0x0000201000000000ull)

static u8 sAudioHeapPinned = 0;
static u8 sSystemHeapPinned = 0;

// Reserve+commit exactly at `base`; if the OS hands back a different address, release it and report failure so
// the caller can fall back. VirtualAlloc zero-fills committed pages (matching a fresh heap).
static u8* Heaps_ReserveFixed(void* base, size_t size) {
    void* p = VirtualAlloc(base, size, SOH_MEM_RESERVE | SOH_MEM_COMMIT, SOH_PAGE_READWRITE);
    if (p != NULL && p != base) {
        VirtualFree(p, 0, SOH_MEM_RELEASE);
        p = NULL;
    }
    return (u8*)p;
}
#endif

u8* gAudioHeap;
u8* gSystemHeap;

// Non-zero only when BOTH heaps landed at their fixed bases this launch. The savestate layer reads this to
// decide whether a state captured this session is safe to persist/restore across a restart.
u8 gHeapsArePinned = 0;

// soh.exe image base + size, captured at boot (Windows only; zero elsewhere). The savestate layer relocates
// code/function pointers across a restart by this base, since ASLR shifts the whole image by a single delta.
uintptr_t gExeBase = 0;
uint32_t gExeSize = 0;

#ifdef _WIN32
// Forward-declared like VirtualAlloc above, to avoid <windows.h> (it clobbers z64.h). We model only the PE
// fields we need; SizeOfImage sits at optional-header offset 0x38 in both PE32 and PE32+, so one layout serves.
extern void* GetModuleHandleW(const void* lpModuleName);
extern void lusprintf(const char* file, int line, int logLevel, const char* fmt, ...);

typedef struct {
    u8 pad[0x3C];
    s32 e_lfanew; // file offset of the NT headers
} SohPeDosHeader;

typedef struct {
    u32 signature;           // "PE\0\0"
    u8 fileHeader[0x14];     // IMAGE_FILE_HEADER
    u8 optionalHeader[0x38]; // IMAGE_OPTIONAL_HEADER, up to SizeOfImage:
    u32 sizeOfImage;
} SohPeNtHeaders;
_Static_assert(offsetof(SohPeNtHeaders, sizeOfImage) == 0x50, "unexpected PE header layout");

static void Heaps_CaptureImageBase(void) {
    void* base = GetModuleHandleW(NULL);
    SohPeDosHeader* dos = (SohPeDosHeader*)base;
    SohPeNtHeaders* nt = (SohPeNtHeaders*)((u8*)base + dos->e_lfanew);
    gExeBase = (uintptr_t)base;
    gExeSize = nt->sizeOfImage;
}
#endif

void Heaps_Alloc(void) {
#ifdef _WIN32
    gAudioHeap = NULL;
    gSystemHeap = NULL;
#ifdef _WIN64
    gAudioHeap = Heaps_ReserveFixed(AUDIO_HEAP_FIXED_BASE, AUDIO_HEAP_SIZE);
    sAudioHeapPinned = (gAudioHeap != NULL);
    gSystemHeap = Heaps_ReserveFixed(SYSTEM_HEAP_FIXED_BASE, SYSTEM_HEAP_SIZE);
    sSystemHeapPinned = (gSystemHeap != NULL);
    gHeapsArePinned = (sAudioHeapPinned && sSystemHeapPinned) ? 1 : 0;
#endif
    // 32-bit Windows, or the fixed-address fallback on 64-bit.
    if (gAudioHeap == NULL) {
        gAudioHeap = (u8*)_aligned_malloc(AUDIO_HEAP_SIZE, 0x10);
    }
    if (gSystemHeap == NULL) {
        gSystemHeap = (u8*)_aligned_malloc(SYSTEM_HEAP_SIZE, 0x10);
    }
#elif defined(_POSIX_VERSION) && (_POSIX_VERSION >= 200112L)
    // TODO(cross-restart savestates on Linux/macOS): pin at the fixed bases via mmap (MAP_FIXED_NOREPLACE on
    // Linux; plain mmap + verify addr on macOS), else fall back to posix_memalign. Win64-only for now.
    if (posix_memalign((void**)&gAudioHeap, 0x10, AUDIO_HEAP_SIZE) != 0)
        gAudioHeap = NULL;
    if (posix_memalign((void**)&gSystemHeap, 0x10, SYSTEM_HEAP_SIZE) != 0)
        gSystemHeap = NULL;
#else
    gAudioHeap = (u8*)memalign(0x10, AUDIO_HEAP_SIZE);
    gSystemHeap = (u8*)memalign(0x10, SYSTEM_HEAP_SIZE);
#endif

    assert(gAudioHeap != NULL);
    assert(gSystemHeap != NULL);

#ifdef _WIN32
    // Capture the image base for the savestate layer, and confirm where the heaps landed (the cross-session
    // savestate prerequisite). lusprintf at info level (2) so it reaches the log even when game-prints are off.
    Heaps_CaptureImageBase();
    lusprintf(__FILE__, __LINE__, 2, "[Heaps] gSystemHeap=%p gAudioHeap=%p pinned=%d exeBase=%p exeSize=0x%x",
              (void*)gSystemHeap, (void*)gAudioHeap, (int)gHeapsArePinned, (void*)gExeBase, gExeSize);
#endif
}

void Heaps_Free(void) {
#ifdef _WIN32
#ifdef _WIN64
    if (sAudioHeapPinned) {
        VirtualFree(gAudioHeap, 0, SOH_MEM_RELEASE);
        gAudioHeap = NULL;
    }
    if (sSystemHeapPinned) {
        VirtualFree(gSystemHeap, 0, SOH_MEM_RELEASE);
        gSystemHeap = NULL;
    }
    sAudioHeapPinned = 0;
    sSystemHeapPinned = 0;
    gHeapsArePinned = 0;
#endif
    // Anything not pinned (32-bit, or the fallback) was _aligned_malloc'd; _aligned_free(NULL) is a documented
    // no-op, so this frees only the malloc'd heaps and skips the VirtualFree'd ones.
    _aligned_free(gAudioHeap);
    _aligned_free(gSystemHeap);
#else
    free(gAudioHeap);
    free(gSystemHeap);
#endif
}
