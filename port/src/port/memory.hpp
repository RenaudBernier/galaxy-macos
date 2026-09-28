// Emulated Wii memory map (see memory.cpp).
#pragma once

#include <port/wii_addr.h>

#include <cstddef>
#include <cstdint>

namespace port::mem {

struct Layout {
    uintptr_t imageStart;
    uintptr_t imageEnd;
    uintptr_t mem1Start;
    uintptr_t mem1End;
    uintptr_t mem2Start;
    uintptr_t mem2End;
    uintptr_t mainStack;     // main thread stack at the bottom, thread slots above
    uintptr_t mainStackEnd;  // end of the whole stack region
};

// Reserves the game's memory next to the executable. Call once, first thing.
bool init(uint32_t mem1Size, uint32_t mem2Size, uint32_t mainStackSize);
const Layout& layout();

// Host stacks for game threads (inside the window). `size` receives the slot size.
void* allocThreadStack(size_t& size);
void freeThreadStack(void* stack);

// True for pointers into the MEM1/MEM2 arenas (i.e. JKRHeap memory).
bool isGameArena(const void* p);
// True for return addresses inside the decompiled game's code.
bool isGameCode(const void* returnAddress);

}  // namespace port::mem
