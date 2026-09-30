// Host entry point for the macOS port.
//
//   "Super Mario Galaxy" <path to RMGK01 disc image (.iso/.rvz/.wbfs/...)>
//
// or set SMG_DISC. Boot order matters:
//   1. reserve the game's memory next to the executable (before anything else
//      can map pages there),
//   2. bring up Aurora (window, GPU) and the disc,
//   3. switch the main thread onto its stack inside the emulated Wii address
//      window and run the game's main().

#include "os/scheduler.hpp"
#include "port/dol_data.hpp"
#include "port/frame.hpp"
#include "port/log.hpp"
#include "port/memory.hpp"
#include "port/savestate.hpp"

#include <aurora/aurora.h>
#include <aurora/dvd.h>
#include <aurora/main.h>
#include <dolphin/gx.h>

#include <crt_externs.h>
#include <mach-o/dyld.h>
#include <spawn.h>
#include <sys/mman.h>

#include <climits>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>

extern "C" void GameMain(void);

namespace port::sc {
// Picks the default layouts (4:3 or 16:9) from the display's shape (sc.cpp).
void detectDisplayShape(SDL_Window* window);
}  // namespace port::sc

namespace port::os {
void initArenas();
}

namespace port::macos {
void configureApplication();
void installSaveStateMenu(SDL_Window* window);
}

namespace {

// Memory sizes. The Wii has 24MB MEM1 and 64MB MEM2; host objects are larger
// (64-bit pointers), so the arenas are generously sized.
constexpr uint32_t kMem1Size = 64u << 20;
constexpr uint32_t kMem2Size = 256u << 20;
constexpr uint32_t kMainStackSize = 8u << 20;

PORT_SAVED OSThread sDefaultThread;  // the game's main thread

void auroraLog(AuroraLogLevel level, const char* module, const char* message, unsigned int len) {
    using port::log::Level;
    static const Level kMap[] = {Level::Debug, Level::Info, Level::Warn, Level::Error, Level::Fatal};
    port::log::write(kMap[level], module, std::string(message, len));
    if (level == LOG_FATAL) {
        abort();
    }
}

void dvdDispatch(void (*fn)(void*), void* arg) {
    port::os::postInterrupt([fn, arg] { fn(arg); });
}

// Pre-built display lists address textures/TLUTs by Wii physical address;
// physical addresses are offsets into the emulated Wii address window.
const void* resolvePhysical(u32 physicalAddress) {
    if (physicalAddress == 0 || physicalAddress >= 0x80000000u) {
        return nullptr;
    }
    return PortU32ToPtr(0x80000000u + physicalAddress);
}

void runGame(void*) {
    const auto& l = port::mem::layout();
    void* stackBase = reinterpret_cast<void*>(l.mainStack + kMainStackSize);
    void* stackEnd = reinterpret_cast<void*>(l.mainStack);
    port::os::initScheduler(&sDefaultThread, stackBase, stackEnd);
    port::frame::init();
    port::frame::beginFirstFrame();
    PORT_INFO("main", "starting game");
    GameMain();
    PORT_INFO("main", "game main returned");
}

}  // namespace

// Runs fn(arg) on another stack (arm64 AAPCS). The frame record chain is
// preserved so debuggers can unwind across the switch.
extern "C" void port_call_on_stack(void (*fn)(void*), void* arg, void* stackTop);
asm(R"(
    .text
    .p2align 2
    .globl _port_call_on_stack
_port_call_on_stack:
    stp x29, x30, [sp, #-16]!
    mov x29, sp
    mov x9, sp
    and x2, x2, #~15
    mov sp, x2
    stp x9, x29, [sp, #-16]!
    mov x10, x0
    mov x0, x1
    blr x10
    ldp x9, x29, [sp], #16
    mov sp, x9
    ldp x29, x30, [sp], #16
    ret
)");

// Save states hold absolute pointers, so they need the game at the same
// addresses on every launch: relaunch once with ASLR off, the way debuggers
// start programs. If that fails, states still work until the game quits.
void relaunchWithoutAslr(char** argv) {
    if (_dyld_get_image_vmaddr_slide(0) == 0 || getenv("SMG_NO_ASLR_RELAUNCH") != nullptr) {
        return;
    }
    setenv("SMG_NO_ASLR_RELAUNCH", "1", 1);
    char path[PATH_MAX];
    uint32_t size = sizeof(path);
    if (_NSGetExecutablePath(path, &size) != 0) {
        return;
    }
    constexpr short kDisableAslr = 0x0100;  // _POSIX_SPAWN_DISABLE_ASLR (xnu)
    posix_spawnattr_t attr;
    posix_spawnattr_init(&attr);
    posix_spawnattr_setflags(&attr, POSIX_SPAWN_SETEXEC | kDisableAslr);
    const int rc = posix_spawn(nullptr, path, nullptr, &attr, argv, *_NSGetEnviron());  // returns only on failure
    posix_spawnattr_destroy(&attr);
    PORT_WARN("main", "couldn't relaunch without ASLR ({}); save states will only load in this session", rc);
}

int main(int argc, char** argv) {
    relaunchWithoutAslr(argv);
    if (getenv("SMG_DEBUG") != nullptr) {
        port::log::setMinLevel(port::log::Level::Debug);
    }

    const char* disc = argc > 1 ? argv[1] : getenv("SMG_DISC");
    if (disc == nullptr || *disc == '\0') {
        fprintf(stderr,
                "usage: %s <disc image>\n"
                "  A disc image of Super Mario Galaxy (Korea, RMGK01) is required (.iso, .rvz, .wbfs, ...).\n"
                "  Alternatively set SMG_DISC.\n",
                argv[0]);
        return 2;
    }

    if (!port::mem::init(kMem1Size, kMem2Size, kMainStackSize)) {
        return 1;
    }
    port::macos::configureApplication();

    AuroraConfig config{};
    config.appName = "Super Mario Galaxy";
    config.desiredBackend = BACKEND_AUTO;
    config.vsync = false;  // the VI retrace timer paces frames
    config.windowWidth = 1280;
    config.windowHeight = 720;
    if (const char* size = getenv("SMG_WINDOW")) {
        // Initial window size, e.g. SMG_WINDOW=1920x800; any shape works.
        int width = 0;
        int height = 0;
        if (sscanf(size, "%dx%d", &width, &height) == 2 && width > 0 && height > 0) {
            config.windowWidth = width;
            config.windowHeight = height;
        } else {
            PORT_WARN("main", "ignoring SMG_WINDOW={} (expected WIDTHxHEIGHT)", size);
        }
    }
    config.logCallback = auroraLog;
    config.logLevel = LOG_INFO;
    config.mem1Size = 0;  // the port manages game memory itself
    config.mem2Size = 0;
    const AuroraInfo info = aurora_initialize(argc, argv, &config);
    port::sc::detectDisplayShape(info.window);
    port::macos::installSaveStateMenu(info.window);

    GXSetAuroraPhysicalResolver(resolvePhysical);
    aurora_dvd_set_callback_dispatcher(dvdDispatch);
    if (!aurora_dvd_open(disc)) {
        PORT_FATAL("main", "could not open disc image {}", disc);
        return 1;
    }
    if (!port::dol::loadEmbeddedData(disc)) {
        return 1;
    }
    port::os::initArenas();

    // Guard page below the main game stack.
    const auto& l = port::mem::layout();
    mprotect(reinterpret_cast<void*>(l.mainStack), 16384, PROT_NONE);
    port_call_on_stack(runGame, nullptr, reinterpret_cast<void*>(l.mainStack + kMainStackSize));

    aurora_shutdown();
    return 0;
}
