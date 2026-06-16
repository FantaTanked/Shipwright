#include "z64.h"
#include <assert.h>
#if !defined(__APPLE__) && !defined(__OpenBSD__)
#include <malloc.h>
#endif
#include <stdlib.h>

#ifndef _MSC_VER
#include <unistd.h>
#endif

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#elif defined(_POSIX_VERSION) && (_POSIX_VERSION >= 200112L)
#include <sys/mman.h>
#endif

u8* gAudioHeap;
u8* gSystemHeap;

// ship-gz: for cross-session savestates the engine heaps must live at the SAME
// virtual address every launch, otherwise every internal pointer baked into a
// saved heap (actor links, audio seq pointers, etc.) dangles on reload. We pin
// them at fixed bases via VirtualAlloc / MAP_FIXED instead of the allocator's
// arbitrary placement. Paired with ASLR disabled on the exe (so code addresses
// are stable too) and a build-hash gate on the state file. See savestates.cpp.
#define GZ_AUDIO_HEAP_BASE  ((void*)0x0000026000000000ULL)
#define GZ_SYSTEM_HEAP_BASE ((void*)0x0000027000000000ULL)

void Heaps_Alloc(void) {
#ifdef _WIN32
    gAudioHeap = (u8*)VirtualAlloc(GZ_AUDIO_HEAP_BASE, AUDIO_HEAP_SIZE, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE);
    gSystemHeap = (u8*)VirtualAlloc(GZ_SYSTEM_HEAP_BASE, SYSTEM_HEAP_SIZE, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE);
#elif defined(_POSIX_VERSION) && (_POSIX_VERSION >= 200112L)
    gAudioHeap = (u8*)mmap(GZ_AUDIO_HEAP_BASE, AUDIO_HEAP_SIZE, PROT_READ | PROT_WRITE,
                           MAP_PRIVATE | MAP_ANONYMOUS | MAP_FIXED, -1, 0);
    if (gAudioHeap == MAP_FAILED)
        gAudioHeap = NULL;
    gSystemHeap = (u8*)mmap(GZ_SYSTEM_HEAP_BASE, SYSTEM_HEAP_SIZE, PROT_READ | PROT_WRITE,
                            MAP_PRIVATE | MAP_ANONYMOUS | MAP_FIXED, -1, 0);
    if (gSystemHeap == MAP_FAILED)
        gSystemHeap = NULL;
#else
    gAudioHeap = (u8*)memalign(0x10, AUDIO_HEAP_SIZE);
    gSystemHeap = (u8*)memalign(0x10, SYSTEM_HEAP_SIZE);
#endif

    // If the fixed bases are unavailable we have no fallback that preserves
    // cross-session validity, so fail loudly rather than silently corrupting.
    assert(gAudioHeap != NULL);
    assert(gSystemHeap != NULL);
    assert(gAudioHeap == (u8*)GZ_AUDIO_HEAP_BASE);
    assert(gSystemHeap == (u8*)GZ_SYSTEM_HEAP_BASE);
}

void Heaps_Free(void) {
#ifdef _WIN32
    if (gAudioHeap != NULL) {
        VirtualFree(gAudioHeap, 0, MEM_RELEASE);
    }
    if (gSystemHeap != NULL) {
        VirtualFree(gSystemHeap, 0, MEM_RELEASE);
    }
#elif defined(_POSIX_VERSION) && (_POSIX_VERSION >= 200112L)
    if (gAudioHeap != NULL) {
        munmap(gAudioHeap, AUDIO_HEAP_SIZE);
    }
    if (gSystemHeap != NULL) {
        munmap(gSystemHeap, SYSTEM_HEAP_SIZE);
    }
#else
    free(gAudioHeap);
    free(gSystemHeap);
#endif
}
