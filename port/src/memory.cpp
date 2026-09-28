// Emulated Wii memory map and allocator routing.
//
// Game code treats pointers as 32-bit Wii addresses (see
// port/include/port/wii_addr.h). This file places all game memory in one
// window starting at the executable image, presented as Wii addresses
// 0x80000000-0xFFFFFFFF:
//
//   0x80000000  executable image (static data, string literals)
//   ...         MEM1 arena (after the image, 16MB aligned)
//   0x90000000  MEM2 arena (moved up if the image + MEM1 don't fit below)
//   ...         game main-thread stack
//
// Global operator new/delete: allocations made by game code (functions in the
// __TEXT,__game section, see prelude.h) go to the game's JKRHeap; everything
// else (SDL, Aurora, the C++ runtime) uses the host allocator.

#include "port/memory.hpp"
#include "port/log.hpp"

#include <mach-o/dyld.h>
#include <mach-o/getsect.h>
#include <mach-o/loader.h>
#include <mach/mach.h>
#include <mach/mach_vm.h>

#include <cstdlib>
#include <cstring>
#include <new>

extern "C" {
uintptr_t gPortWiiBase = 0;

// Provided by the game (src/JSystem/JKernel/JKRHeap.cpp).
void* JKRHeap_PortNew(size_t size);
void JKRHeap_PortFree(void* ptr);
int JKRHeap_PortIsReady(void);
}

// Bounds of the game's code, synthesized by the linker.
extern "C" char sGameTextStart __asm("section$start$__TEXT$__game");
extern "C" char sGameTextEnd __asm("section$end$__TEXT$__game");

namespace port::mem {
namespace {

constexpr uintptr_t kWindowSize = 0x80000000ull;
constexpr uintptr_t k16MB = 16ull << 20;

Layout sLayout;

uintptr_t alignUp(uintptr_t v, uintptr_t a) { return (v + a - 1) & ~(a - 1); }

// Reserve [addr, addr+size) exactly; never clobbers existing mappings.
bool mapFixed(uintptr_t addr, size_t size) {
    mach_vm_address_t a = addr;
    kern_return_t kr = mach_vm_allocate(mach_task_self(), &a, size, VM_FLAGS_FIXED);
    return kr == KERN_SUCCESS && a == addr;
}

void imageBounds(uintptr_t& start, uintptr_t& end) {
    const auto* mh = reinterpret_cast<const mach_header_64*>(_dyld_get_image_header(0));
    const intptr_t slide = _dyld_get_image_vmaddr_slide(0);
    start = reinterpret_cast<uintptr_t>(mh);
    end = start;
    const auto* lc = reinterpret_cast<const load_command*>(mh + 1);
    for (uint32_t i = 0; i < mh->ncmds; i++) {
        if (lc->cmd == LC_SEGMENT_64) {
            const auto* seg = reinterpret_cast<const segment_command_64*>(lc);
            if (seg->vmsize != 0 && strcmp(seg->segname, SEG_PAGEZERO) != 0) {
                uintptr_t segEnd = seg->vmaddr + slide + seg->vmsize;
                if (segEnd > end) {
                    end = segEnd;
                }
            }
        }
        lc = reinterpret_cast<const load_command*>(reinterpret_cast<const uint8_t*>(lc) + lc->cmdsize);
    }
}

}  // namespace

const Layout& layout() { return sLayout; }

namespace {
// Game thread stacks: fixed-size slots carved from the stack region above the
// main stack. Slots of exited threads are recycled by the scheduler.
constexpr size_t kThreadStackSlot = 1u << 20;
constexpr size_t kThreadStackSlots = 56;
uintptr_t sSlotsBase = 0;
bool sSlotUsed[kThreadStackSlots];
}  // namespace

void* allocThreadStack(size_t& size) {
    for (size_t i = 0; i < kThreadStackSlots; i++) {
        if (!sSlotUsed[i]) {
            sSlotUsed[i] = true;
            size = kThreadStackSlot;
            return reinterpret_cast<void*>(sSlotsBase + i * kThreadStackSlot);
        }
    }
    return nullptr;
}

void freeThreadStack(void* stack) {
    const uintptr_t v = reinterpret_cast<uintptr_t>(stack);
    if (v >= sSlotsBase && v < sSlotsBase + kThreadStackSlots * kThreadStackSlot) {
        sSlotUsed[(v - sSlotsBase) / kThreadStackSlot] = false;
    }
}

// Finds the lowest free range of `size` bytes at or above `from` (aligned to
// `align`) that ends below `limit`.
uintptr_t findFreeRange(uintptr_t from, size_t size, uintptr_t align, uintptr_t limit) {
    uintptr_t candidate = alignUp(from, align);
    while (candidate + size <= limit) {
        mach_vm_address_t addr = candidate;
        mach_vm_size_t regionSize = 0;
        vm_region_basic_info_data_64_t info;
        mach_msg_type_number_t count = VM_REGION_BASIC_INFO_COUNT_64;
        mach_port_t object = MACH_PORT_NULL;
        const kern_return_t kr = mach_vm_region(mach_task_self(), &addr, &regionSize, VM_REGION_BASIC_INFO_64,
                                                reinterpret_cast<vm_region_info_t>(&info), &count, &object);
        if (kr != KERN_SUCCESS || addr >= candidate + size) {
            return candidate;  // nothing mapped in [candidate, candidate + size)
        }
        candidate = alignUp(addr + regionSize, align);
    }
    return 0;
}

bool init(uint32_t mem1Size, uint32_t mem2Size, uint32_t mainStackSize) {
    uintptr_t imgStart, imgEnd;
    imageBounds(imgStart, imgEnd);
    gPortWiiBase = imgStart;

    // One contiguous reservation: MEM1, MEM2, then the stack region (main
    // thread stack followed by the thread-stack slots).
    const uintptr_t stacksSize = mainStackSize + kThreadStackSlots * kThreadStackSlot;
    const uintptr_t mem2Offset = alignUp(mem1Size, k16MB);
    const uintptr_t stackOffset = alignUp(mem2Offset + mem2Size, k16MB);
    const uintptr_t total = stackOffset + stacksSize;

    const uintptr_t base = findFreeRange(imgEnd, total, k16MB, gPortWiiBase + kWindowSize);
    if (base == 0 || !mapFixed(base, total)) {
        PORT_FATAL("mem", "could not reserve {} MB of game memory within 2 GB of the executable", total >> 20);
        return false;
    }

    const uintptr_t mem1 = base;
    const uintptr_t mem2 = base + mem2Offset;
    const uintptr_t stack = base + stackOffset;
    sLayout = Layout{
        .imageStart = imgStart,
        .imageEnd = imgEnd,
        .mem1Start = mem1,
        .mem1End = mem1 + mem1Size,
        .mem2Start = mem2,
        .mem2End = mem2 + mem2Size,
        .mainStack = stack,
        .mainStackEnd = stack + stacksSize,
    };
    sSlotsBase = stack + mainStackSize;
    PORT_INFO("mem", "Wii window base {:#x}; MEM1 at 0x{:08X} ({} MB), MEM2 at 0x{:08X} ({} MB)", gPortWiiBase,
              (unsigned)PortPtrToU32((void*)mem1), mem1Size >> 20, (unsigned)PortPtrToU32((void*)mem2), mem2Size >> 20);
    return true;
}

bool isGameArena(const void* p) {
    const auto v = reinterpret_cast<uintptr_t>(p);
    return (v >= sLayout.mem1Start && v < sLayout.mem1End) || (v >= sLayout.mem2Start && v < sLayout.mem2End);
}

bool isGameCode(const void* returnAddress) {
    const auto v = reinterpret_cast<uintptr_t>(returnAddress);
    return v >= reinterpret_cast<uintptr_t>(&sGameTextStart) && v < reinterpret_cast<uintptr_t>(&sGameTextEnd);
}

}  // namespace port::mem

extern "C" void PortWiiAddrFault(uintptr_t hostAddr) {
    PORT_FATAL("mem", "pointer {:#x} is outside the emulated Wii address window (base {:#x})", hostAddr,
               gPortWiiBase);
    __builtin_trap();
}

// ---------------------------------------------------------------------------
// Global allocation routing
// ---------------------------------------------------------------------------

namespace {

[[gnu::always_inline]] inline void* routedNew(size_t size, void* caller) {
    if (port::mem::isGameCode(caller) && JKRHeap_PortIsReady()) {
        return JKRHeap_PortNew(size);
    }
    void* p = malloc(size ? size : 1);
    if (p == nullptr) {
        abort();
    }
    return p;
}

inline void routedDelete(void* p) {
    if (p == nullptr) {
        return;
    }
    if (port::mem::isGameArena(p)) {
        JKRHeap_PortFree(p);
    } else {
        free(p);
    }
}

}  // namespace

void* operator new(size_t size) { return routedNew(size, __builtin_return_address(0)); }
void* operator new[](size_t size) { return routedNew(size, __builtin_return_address(0)); }
void* operator new(size_t size, const std::nothrow_t&) noexcept { return routedNew(size, __builtin_return_address(0)); }
void* operator new[](size_t size, const std::nothrow_t&) noexcept {
    return routedNew(size, __builtin_return_address(0));
}
void operator delete(void* p) noexcept { routedDelete(p); }
void operator delete[](void* p) noexcept { routedDelete(p); }
void operator delete(void* p, size_t) noexcept { routedDelete(p); }
void operator delete[](void* p, size_t) noexcept { routedDelete(p); }
void operator delete(void* p, const std::nothrow_t&) noexcept { routedDelete(p); }
void operator delete[](void* p, const std::nothrow_t&) noexcept { routedDelete(p); }

// Over-aligned allocations only come from host code.
void* operator new(size_t size, std::align_val_t al) {
    void* p = nullptr;
    if (posix_memalign(&p, static_cast<size_t>(al), size ? size : 1) != 0) {
        abort();
    }
    return p;
}
void* operator new[](size_t size, std::align_val_t al) { return operator new(size, al); }
void operator delete(void* p, std::align_val_t) noexcept { free(p); }
void operator delete[](void* p, std::align_val_t) noexcept { free(p); }
void operator delete(void* p, size_t, std::align_val_t) noexcept { free(p); }
void operator delete[](void* p, size_t, std::align_val_t) noexcept { free(p); }
