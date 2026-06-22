#include "z64.h"
#include <assert.h>
#if !defined(__APPLE__) && !defined(__OpenBSD__)
#include <malloc.h>
#endif
#include <stdlib.h>

#ifndef _MSC_VER
#include <unistd.h>
#endif

#ifdef _WIN64
// 64-bit Windows only: pin the game heaps at fixed virtual addresses. Allocating them at the same address
// every launch is what lets a savestate's intra-heap pointers stay valid across closing and reopening the app
// (the cross-session savestate feature). The two bases are deep in canonical x64 user space (32 TiB), well
// clear of the image, the CRT heap, stacks and DLLs, and are 64-bit-only addresses by construction. If a base
// is ever unavailable we fall back to the normal aligned allocator and clear gHeapsArePinned (cross-restart
// states for that session are then treated as unsafe).
//
// VirtualAlloc/VirtualFree are forward-declared rather than pulling in <windows.h>, which clobbers z64.h.
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
