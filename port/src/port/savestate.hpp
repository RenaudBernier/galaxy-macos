// Save states (see savestate.cpp).
#pragma once

#include <cstdint>
#include <string>
#include <vector>

// Marks port state that belongs to the emulated console (hardware registers,
// allocators, callbacks the game installed): save states capture it along
// with the game's own data. Plain data only; host objects (threads, locks,
// containers) are handled by the module that owns them.
#define PORT_SAVED __attribute__((section("__DATA,__port_saved")))

namespace port::savestate {

constexpr int kSlotCount = 5;

// Queue a save or load of `slot` (1-5); it happens at the next frame boundary.
void requestSave(int slot);
void requestLoad(int slot);
// At the frame boundary, on the main game thread (vi.cpp).
void processRequests();
// When the slot was saved (e.g. "Sep 29, 14:05"), or empty if it is unused.
std::string slotDescription(int slot);

}  // namespace port::savestate

// Host-side state of other modules that save states capture or check.
namespace port::tpl {
struct BoundPalette {
    const void* palette;
    void* descriptors;
    uint32_t count;
};
std::vector<BoundPalette> boundPalettes();
void setBoundPalettes(const std::vector<BoundPalette>& palettes);
}  // namespace port::tpl

namespace port::nand {
bool idle();  // no save file open
}  // namespace port::nand
