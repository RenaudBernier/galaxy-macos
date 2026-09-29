// Save states.
//
// A state is a copy of everything the game can observe:
//   - its global variables (sections __game_data/__game_bss, see prelude.h),
//     plus the initialization flags of its function-local statics, which the
//     compiler keeps in the ordinary data section;
//   - MEM1 and MEM2: its heaps, and the ARAM emulated in MEM2;
//   - the emulated console's state kept by the port (section __port_saved,
//     alarms and the time base, open disc files, bound texture palettes);
//   - the stacks of its threads.
//
// Game threads are host threads, so a thread's stack can only be put back
// where the thread stands in the same place: states are taken and loaded at a
// frame boundary, with the main thread here and every other game thread
// parked in the scheduler, waiting for something (see scheduler.hpp). Only
// the part of each stack above the park point is saved, and a state loads
// only if every thread has the same frames there; otherwise (e.g. while a
// stage loads) the request is retried on the following frames.
//
// States hold absolute pointers, so they only load into the same build at the
// same addresses; main.cpp turns ASLR off so that holds across launches.

#include "port/savestate.hpp"

#include "os/scheduler.hpp"
#include "port/audio.hpp"
#include "port/log.hpp"
#include "port/memory.hpp"

#include <aurora/dvd.h>
#include <compression.h>
#include <dolphin/gx/GXAurora.h>
#include <mach-o/dyld.h>
#include <mach-o/getsect.h>
#include <mach-o/loader.h>
#include <mach-o/nlist.h>
#include <sys/mman.h>
#include <unistd.h>

#include <array>
#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <filesystem>
#include <future>
#include <memory>
#include <string>
#include <string_view>
#include <thread>
#include <type_traits>
#include <unordered_map>
#include <vector>

namespace port::macos {
void showStatus(const std::string& message);  // macos.mm
}

namespace port::savestate {
namespace {

namespace fs = std::filesystem;
using Clock = std::chrono::steady_clock;

constexpr char kMagic[8] = {'S', 'M', 'G', 'S', 'T', 'A', 'T', 'E'};
constexpr uint32_t kVersion = 1;
// A request that can't be served (the game is busy) is retried for this many
// frames.
constexpr int kMaxAttempts = 180;

struct FileHeader {
    char magic[8];
    uint32_t version;
    uint32_t reserved;
    int64_t savedAt;  // time(), for the menu
    uint8_t buildId[16];
    uint64_t payloadSize;
    uint64_t compressedSize;
};

// Where the game lives in this process; a state needs the same.
struct Identity {
    uint8_t buildId[16];
    uint64_t imageStart;
    uint64_t mem1Start, mem1End, mem2Start, mem2End, stackStart, stackEnd;

    bool operator==(const Identity&) const = default;
};

// --- Process layout ---------------------------------------------------------

const mach_header_64* image() { return reinterpret_cast<const mach_header_64*>(_dyld_get_image_header(0)); }

template <class F>
void forEachLoadCommand(F&& f) {
    const auto* cmd = reinterpret_cast<const load_command*>(image() + 1);
    for (uint32_t i = 0; i < image()->ncmds; i++) {
        f(cmd);
        cmd = reinterpret_cast<const load_command*>(reinterpret_cast<const uint8_t*>(cmd) + cmd->cmdsize);
    }
}

Identity currentIdentity() {
    Identity id{};
    forEachLoadCommand([&](const load_command* cmd) {
        if (cmd->cmd == LC_UUID) {
            std::memcpy(id.buildId, reinterpret_cast<const uuid_command*>(cmd)->uuid, sizeof(id.buildId));
        }
    });
    const auto& l = port::mem::layout();
    id.imageStart = l.imageStart;
    id.mem1Start = l.mem1Start;
    id.mem1End = l.mem1End;
    id.mem2Start = l.mem2Start;
    id.mem2End = l.mem2End;
    id.stackStart = l.mainStack;
    id.stackEnd = l.mainStackEnd;
    return id;
}

struct Range {
    uintptr_t start;
    size_t size;
};

// The game's globals and the port's saved state.
std::vector<Range> dataSections() {
    std::vector<Range> ranges;
    for (const char* name : {"__game_data", "__game_bss", "__port_saved"}) {
        unsigned long size = 0;
        if (uint8_t* data = getsectiondata(image(), "__DATA", name, &size); data != nullptr && size != 0) {
            ranges.push_back({reinterpret_cast<uintptr_t>(data), size});
        }
    }
    return ranges;
}

// Initialization flags (__ZGV...) of statics that live in the game's sections,
// found through the symbol table: they are in the ordinary data section.
const std::vector<uintptr_t>& gameGuardVariables() {
    static const std::vector<uintptr_t> guards = [] {
        const intptr_t slide = _dyld_get_image_vmaddr_slide(0);
        const segment_command_64* linkedit = nullptr;
        const symtab_command* symtab = nullptr;
        forEachLoadCommand([&](const load_command* cmd) {
            if (cmd->cmd == LC_SEGMENT_64 &&
                std::strcmp(reinterpret_cast<const segment_command_64*>(cmd)->segname, SEG_LINKEDIT) == 0) {
                linkedit = reinterpret_cast<const segment_command_64*>(cmd);
            } else if (cmd->cmd == LC_SYMTAB) {
                symtab = reinterpret_cast<const symtab_command*>(cmd);
            }
        });
        std::vector<uintptr_t> out;
        if (linkedit == nullptr || symtab == nullptr) {
            PORT_WARN("savestate", "no symbol table; function-local statics won't be saved");
            return out;
        }
        const uintptr_t base = linkedit->vmaddr + slide - linkedit->fileoff;
        const auto* syms = reinterpret_cast<const nlist_64*>(base + symtab->symoff);
        const char* strings = reinterpret_cast<const char*>(base + symtab->stroff);
        std::unordered_map<std::string_view, uintptr_t> defined;
        std::vector<std::pair<std::string_view, uintptr_t>> guardSyms;
        for (uint32_t i = 0; i < symtab->nsyms; i++) {
            if ((syms[i].n_type & N_STAB) != 0 || (syms[i].n_type & N_TYPE) != N_SECT) {
                continue;
            }
            const std::string_view name = strings + syms[i].n_un.n_strx;
            const uintptr_t addr = syms[i].n_value + slide;
            if (name.starts_with("__ZGV")) {
                guardSyms.emplace_back(name, addr);
            } else if (name.starts_with("__Z")) {
                defined.emplace(name, addr);
            }
        }
        const std::vector<Range> sections = dataSections();
        for (const auto& [name, addr] : guardSyms) {
            const std::string variable = "__Z" + std::string(name.substr(5));
            const auto it = defined.find(variable);
            if (it == defined.end()) {
                continue;
            }
            for (const Range& r : sections) {
                if (it->second >= r.start && it->second < r.start + r.size) {
                    out.push_back(addr);
                    break;
                }
            }
        }
        return out;
    }();
    return guards;
}

// The pages of [start, end) the game has written to (resident or paged out),
// as runs; the others are still untouched zero pages.
std::vector<Range> usedPages(uintptr_t start, uintptr_t end) {
    const size_t page = static_cast<size_t>(getpagesize());
    std::vector<char> status((end - start) / page);
    std::vector<Range> runs;
    if (mincore(reinterpret_cast<void*>(start), end - start, status.data()) != 0) {
        runs.push_back({start, end - start});
        return runs;
    }
    for (size_t i = 0; i < status.size();) {
        if ((status[i] & (MINCORE_INCORE | MINCORE_PAGED_OUT)) == 0) {
            i++;
            continue;
        }
        const size_t first = i;
        while (i < status.size() && (status[i] & (MINCORE_INCORE | MINCORE_PAGED_OUT)) != 0) {
            i++;
        }
        runs.push_back({start + first * page, (i - first) * page});
    }
    return runs;
}

// Replaces [start, end) with fresh zero pages.
void zeroPages(uintptr_t start, uintptr_t end) {
    mmap(reinterpret_cast<void*>(start), end - start, PROT_READ | PROT_WRITE, MAP_FIXED | MAP_PRIVATE | MAP_ANON, -1, 0);
}

fs::path stateDir() {
    if (const char* dir = getenv("SMG_STATE_DIR"); dir != nullptr && *dir != '\0') {
        return dir;
    }
    const char* home = getenv("HOME");
    return fs::path(home != nullptr ? home : ".") / "Library/Application Support/SuperMarioGalaxy/states";
}

fs::path slotPath(int slot) { return stateDir() / ("slot" + std::to_string(slot) + ".sav"); }

// --- Serialization ------------------------------------------------------------

class Writer {
public:
    std::vector<uint8_t> data;

    void bytes(const void* p, size_t n) {
        const auto* b = static_cast<const uint8_t*>(p);
        data.insert(data.end(), b, b + n);
    }
    template <class T>
    void pod(const T& v) {
        static_assert(std::is_trivially_copyable_v<T>);
        bytes(&v, sizeof(v));
    }
    template <class T>
    void vec(const std::vector<T>& v) {
        pod<uint64_t>(v.size());
        bytes(v.data(), v.size() * sizeof(T));
    }
    void block(uintptr_t addr, size_t size) {
        pod<uint64_t>(addr);
        pod<uint64_t>(size);
        bytes(reinterpret_cast<const void*>(addr), size);
    }
};

class Reader {
public:
    Reader(const uint8_t* p, size_t n) : mP(p), mEnd(p + n) {}

    bool ok() const { return mOk; }
    const uint8_t* take(size_t n) {
        if (!mOk || static_cast<size_t>(mEnd - mP) < n) {
            mOk = false;
            return nullptr;
        }
        const uint8_t* p = mP;
        mP += n;
        return p;
    }
    template <class T>
    T pod() {
        T v{};
        if (const uint8_t* p = take(sizeof(T))) {
            std::memcpy(&v, p, sizeof(T));
        }
        return v;
    }
    template <class T>
    std::vector<T> vec() {
        const uint64_t n = pod<uint64_t>();
        std::vector<T> v;
        if (n > (1u << 24)) {
            mOk = false;
            return v;
        }
        if (const uint8_t* p = take(n * sizeof(T))) {
            v.resize(n);
            std::memcpy(v.data(), p, n * sizeof(T));
        }
        return v;
    }

private:
    const uint8_t* mP;
    const uint8_t* mEnd;
    bool mOk = true;
};

struct Block {
    uintptr_t addr;
    size_t size;
    const uint8_t* data;
};

// A state read from disk; its blocks point into `payload`.
struct LoadedState {
    std::vector<uint8_t> payload;
    std::vector<Block> blocks;
    port::os::SchedulerState scheduler;
    port::os::AlarmState alarms;
    std::vector<AuroraDvdOpenFile> dvdFiles;
    std::vector<port::tpl::BoundPalette> palettes;
    u32 nextTexObjId = 0;
    u32 nextTlutObjId = 0;
};

void writeScheduler(Writer& w, const port::os::SchedulerState& s) {
    w.pod<uint64_t>(s.seq);
    w.pod<uint64_t>(s.threads.size());
    for (const auto& t : s.threads) {
        w.pod(t.thread);
        w.pod<uint8_t>(t.started);
        w.pod<uint64_t>(t.regionStart);
        w.pod<uint64_t>(t.regionEnd);
        w.vec(t.chain);
        w.pod<uint8_t>(t.interruptsEnabled);
        w.pod<uint8_t>(t.yielding);
        w.pod<uint64_t>(t.readySeq);
    }
}

void readScheduler(Reader& r, port::os::SchedulerState& s) {
    s.seq = r.pod<uint64_t>();
    const uint64_t n = r.pod<uint64_t>();
    for (uint64_t i = 0; i < n && r.ok() && i < 256; i++) {
        port::os::ThreadState t;
        t.thread = r.pod<OSThread*>();
        t.started = r.pod<uint8_t>() != 0;
        t.regionStart = r.pod<uint64_t>();
        t.regionEnd = r.pod<uint64_t>();
        t.chain = r.vec<uintptr_t>();
        t.interruptsEnabled = r.pod<uint8_t>() != 0;
        t.yielding = r.pod<uint8_t>() != 0;
        t.readySeq = r.pod<uint64_t>();
        s.threads.push_back(std::move(t));
    }
}

// Whether [addr, addr+size) is memory a state may write: the game's sections
// and guards, its heaps and its thread stacks.
bool isRestorable(uintptr_t addr, size_t size) {
    const auto& l = port::mem::layout();
    const uintptr_t end = addr + size;
    if (end < addr) {
        return false;
    }
    for (const Range& r : dataSections()) {
        if (addr >= r.start && end <= r.start + r.size) {
            return true;
        }
    }
    for (uintptr_t guard : gameGuardVariables()) {
        if (addr == guard && size == 8) {
            return true;
        }
    }
    return (addr >= l.mem1Start && end <= l.mem1End) || (addr >= l.mem2Start && end <= l.mem2End) ||
           (addr >= l.mainStack && end <= l.mainStackEnd);
}

// --- Taking and loading states ------------------------------------------------

enum class Result { Done, Retry, Failed };

// Lets in-flight work finish (interrupts, DSP, disc) with the audio DMA held.
// On success, returns with the scheduler locked: nothing else can run.
bool settle(std::unique_lock<std::mutex>& lock) {
    const auto deadline = Clock::now() + std::chrono::milliseconds(50);
    for (;;) {
        port::os::preemptPoint();
        if (port::audio::dspIdle() && aurora_dvd_is_idle() && port::nand::idle()) {
            lock = port::os::lockScheduler();
            if (port::os::pendingInterruptCount() == 0) {
                return true;
            }
            lock.unlock();
        }
        if (Clock::now() >= deadline) {
            return false;
        }
        std::this_thread::sleep_for(std::chrono::microseconds(500));
    }
}

// The last state being written (compressed and saved on another thread).
std::future<void> sWrite;

void waitForWrite() {
    if (sWrite.valid()) {
        sWrite.wait();
    }
}

bool writeState(int slot, const std::vector<uint8_t>& payload, const Identity& identity, std::string& error) {
    std::vector<uint8_t> compressed(payload.size() + payload.size() / 16 + 4096);
    const size_t compressedSize = compression_encode_buffer(compressed.data(), compressed.size(), payload.data(),
                                                            payload.size(), nullptr, COMPRESSION_LZ4);
    if (compressedSize == 0) {
        error = "couldn't compress the state";
        return false;
    }
    FileHeader header{};
    std::memcpy(header.magic, kMagic, sizeof(kMagic));
    header.version = kVersion;
    header.savedAt = static_cast<int64_t>(time(nullptr));
    std::memcpy(header.buildId, identity.buildId, sizeof(header.buildId));
    header.payloadSize = payload.size();
    header.compressedSize = compressedSize;

    std::error_code ec;
    fs::create_directories(stateDir(), ec);
    const fs::path path = slotPath(slot);
    const fs::path temp = fs::path(path).concat(".tmp");
    FILE* f = fopen(temp.c_str(), "wb");
    bool ok = f != nullptr && fwrite(&header, sizeof(header), 1, f) == 1 &&
              fwrite(compressed.data(), 1, compressedSize, f) == compressedSize;
    if (f != nullptr) {
        ok = fclose(f) == 0 && ok;
    }
    if (!ok || (fs::rename(temp, path, ec), ec)) {
        fs::remove(temp, ec);
        error = "couldn't write " + path.string();
        return false;
    }
    PORT_INFO("savestate", "wrote slot {} ({} MB compressed)", slot, compressedSize >> 20);
    return true;
}

Result save(int slot, uintptr_t fp, std::string& message) {
    Writer w;
    const Identity identity = currentIdentity();
    {
        port::audio::setAiPaused(true);
        std::unique_lock<std::mutex> lock;
        if (!settle(lock)) {
            port::audio::setAiPaused(false);
            message = "the game is busy";
            return Result::Retry;
        }
        port::os::SchedulerState scheduler;
        if (!port::os::captureSchedulerState(scheduler, fp, message)) {
            lock.unlock();
            port::audio::setAiPaused(false);
            return Result::Retry;
        }
        port::os::AlarmState alarms;
        {
            auto alarmLock = port::os::lockAlarms();
            port::os::captureAlarmState(alarms);
        }

        w.data.reserve(100u << 20);
        w.pod(identity);
        // Memory.
        std::vector<std::pair<uintptr_t, size_t>> blocks;
        for (const Range& r : dataSections()) {
            blocks.emplace_back(r.start, r.size);
        }
        for (uintptr_t guard : gameGuardVariables()) {
            blocks.emplace_back(guard, 8);
        }
        // The heaps: only the pages in use (a load zeroes the rest).
        const auto& l = port::mem::layout();
        for (const auto& [start, end] : {std::pair{l.mem1Start, l.mem1End}, std::pair{l.mem2Start, l.mem2End}}) {
            for (const Range& r : usedPages(start, end)) {
                blocks.emplace_back(r.start, r.size);
            }
        }
        for (const auto& t : scheduler.threads) {
            if (t.regionEnd > t.regionStart) {
                blocks.emplace_back(t.regionStart, t.regionEnd - t.regionStart);
            }
        }
        w.pod<uint64_t>(blocks.size());
        for (const auto& [addr, size] : blocks) {
            w.block(addr, size);
        }
        // Everything else.
        writeScheduler(w, scheduler);
        w.pod(alarms.ticks);
        w.vec(alarms.queue);
        w.vec(alarms.generations);
        std::vector<AuroraDvdOpenFile> files(aurora_dvd_get_open_files(nullptr, 0));
        files.resize(aurora_dvd_get_open_files(files.data(), files.size()));
        w.vec(files);
        w.vec(port::tpl::boundPalettes());
        u32 texIds[2];
        AuroraGetTextureObjectIds(&texIds[0], &texIds[1]);
        w.pod(texIds);

        lock.unlock();
        port::audio::setAiPaused(false);
    }

    // Compress and write in the background; the game goes on meanwhile.
    PORT_INFO("savestate", "captured slot {} ({} MB)", slot, w.data.size() >> 20);
    waitForWrite();
    sWrite = std::async(std::launch::async, [slot, payload = std::move(w.data), identity] {
        std::string error;
        if (writeState(slot, payload, identity, error)) {
            port::macos::showStatus("Saved to slot " + std::to_string(slot));
        } else {
            PORT_WARN("savestate", "{}", error);
            port::macos::showStatus("Can't save: " + error);
        }
    });
    return Result::Done;
}

// Reads and checks a slot's file.
std::unique_ptr<LoadedState> readState(int slot, std::string& message) {
    waitForWrite();
    const fs::path path = slotPath(slot);
    FILE* f = fopen(path.c_str(), "rb");
    if (f == nullptr) {
        message = "slot " + std::to_string(slot) + " is empty";
        return nullptr;
    }
    FileHeader header{};
    std::vector<uint8_t> compressed;
    bool ok = fread(&header, sizeof(header), 1, f) == 1 && std::memcmp(header.magic, kMagic, sizeof(kMagic)) == 0 &&
              header.compressedSize < (1ull << 32) && header.payloadSize < (1ull << 32);
    if (ok) {
        compressed.resize(header.compressedSize);
        ok = fread(compressed.data(), 1, compressed.size(), f) == compressed.size();
    }
    fclose(f);
    if (!ok) {
        message = "slot " + std::to_string(slot) + " isn't a valid state";
        return nullptr;
    }
    const Identity identity = currentIdentity();
    if (header.version != kVersion || std::memcmp(header.buildId, identity.buildId, sizeof(header.buildId)) != 0) {
        message = "slot " + std::to_string(slot) + " was saved by a different build of the game";
        return nullptr;
    }

    auto state = std::make_unique<LoadedState>();
    state->payload.resize(header.payloadSize);
    if (compression_decode_buffer(state->payload.data(), state->payload.size(), compressed.data(), compressed.size(),
                                  nullptr, COMPRESSION_LZ4) != state->payload.size()) {
        message = "slot " + std::to_string(slot) + " is damaged";
        return nullptr;
    }

    Reader r(state->payload.data(), state->payload.size());
    if (r.pod<Identity>() != identity) {
        message = "slot " + std::to_string(slot) + " needs the game at other addresses (relaunch the game)";
        return nullptr;
    }
    const uint64_t blockCount = r.pod<uint64_t>();
    for (uint64_t i = 0; i < blockCount && r.ok(); i++) {
        const auto addr = static_cast<uintptr_t>(r.pod<uint64_t>());
        const auto size = static_cast<size_t>(r.pod<uint64_t>());
        const uint8_t* data = r.take(size);
        if (data == nullptr || !isRestorable(addr, size)) {
            message = "slot " + std::to_string(slot) + " is damaged";
            return nullptr;
        }
        state->blocks.push_back({addr, size, data});
    }
    readScheduler(r, state->scheduler);
    state->alarms.ticks = r.pod<int64_t>();
    state->alarms.queue = r.vec<std::pair<int64_t, OSAlarm*>>();
    state->alarms.generations = r.vec<std::pair<OSAlarm*, uint64_t>>();
    state->dvdFiles = r.vec<AuroraDvdOpenFile>();
    state->palettes = r.vec<port::tpl::BoundPalette>();
    const auto texIds = r.pod<std::array<u32, 2>>();
    state->nextTexObjId = texIds[0];
    state->nextTlutObjId = texIds[1];
    if (!r.ok()) {
        message = "slot " + std::to_string(slot) + " is damaged";
        return nullptr;
    }
    return state;
}

Result load(const LoadedState& state, uintptr_t fp, std::string& message) {
    port::audio::setAiPaused(true);
    std::unique_lock<std::mutex> lock;
    if (!settle(lock)) {
        port::audio::setAiPaused(false);
        message = "the game is busy";
        return Result::Retry;
    }
    if (!port::os::matchesSchedulerState(state.scheduler, fp, message)) {
        lock.unlock();
        port::audio::setAiPaused(false);
        return Result::Retry;
    }
    {
        auto alarmLock = port::os::lockAlarms();
        const auto& l = port::mem::layout();
        zeroPages(l.mem1Start, l.mem1End);
        zeroPages(l.mem2Start, l.mem2End);
        for (const Block& b : state.blocks) {
            std::memcpy(reinterpret_cast<void*>(b.addr), b.data, b.size);
        }
        port::os::restoreSchedulerState(state.scheduler);
        port::os::restoreAlarmState(state.alarms);
        port::os::dropPendingInterrupts();
    }
    lock.unlock();

    aurora_dvd_set_open_files(state.dvdFiles.data(), state.dvdFiles.size());
    port::tpl::setBoundPalettes(state.palettes);
    AuroraResetTextureCaches(state.nextTexObjId, state.nextTlutObjId);
    port::audio::outputFlush();
    port::audio::setAiPaused(false);
    return Result::Done;
}

// --- Requests -----------------------------------------------------------------

std::atomic<int> sRequest{0};  // from the menu: slot to save, or -slot to load

// The request being served (main game thread only).
int sSlot = 0;
bool sSaving = false;
int sAttempts = 0;
std::unique_ptr<LoadedState> sPendingLoad;

void finishRequest(const std::string& message) {
    if (!message.empty()) {
        PORT_INFO("savestate", "{}", message);
        port::macos::showStatus(message);
    }
    sSlot = 0;
    sAttempts = 0;
    sPendingLoad.reset();
}

}  // namespace

void requestSave(int slot) {
    if (slot >= 1 && slot <= kSlotCount) {
        sRequest.store(slot);
    }
}

void requestLoad(int slot) {
    if (slot >= 1 && slot <= kSlotCount) {
        sRequest.store(-slot);
    }
}

// The main game thread's stack is saved from the caller of this function up:
// see finishFrame in vi.cpp.
[[gnu::noinline]] void processRequests() {
    if (const int request = sRequest.exchange(0); request != 0) {
        sSlot = std::abs(request);
        sSaving = request > 0;
        sAttempts = 0;
        sPendingLoad.reset();
    }
    if (sSlot == 0) {
        return;
    }
    const auto fp = reinterpret_cast<uintptr_t>(__builtin_frame_address(0));
    const int slot = sSlot;
    const auto start = Clock::now();
    std::string message;
    Result result;
    if (sSaving) {
        result = save(slot, fp, message);
    } else {
        if (sPendingLoad == nullptr) {
            sPendingLoad = readState(slot, message);
            if (sPendingLoad == nullptr) {
                finishRequest("Can't load: " + message);
                return;
            }
        }
        result = load(*sPendingLoad, fp, message);
    }
    switch (result) {
    case Result::Done:
        PORT_INFO("savestate", "{} slot {} in {} ms", sSaving ? "captured" : "loaded", slot,
                  std::chrono::duration_cast<std::chrono::milliseconds>(Clock::now() - start).count());
        // A save is reported once written (see save()).
        finishRequest(sSaving ? std::string() : "Loaded slot " + std::to_string(slot));
        break;
    case Result::Failed:
        finishRequest((sSaving ? "Can't save: " : "Can't load: ") + message);
        break;
    case Result::Retry:
        if (++sAttempts >= kMaxAttempts) {
            PORT_INFO("savestate", "gave up on slot {}: {}", slot, message);
            finishRequest((sSaving ? "Can't save now: " : "Can't load now: ") + message);
        }
        break;
    }
}

std::string slotDescription(int slot) {
    FILE* f = fopen(slotPath(slot).c_str(), "rb");
    if (f == nullptr) {
        return {};
    }
    FileHeader header{};
    const bool ok = fread(&header, sizeof(header), 1, f) == 1 && std::memcmp(header.magic, kMagic, sizeof(kMagic)) == 0;
    fclose(f);
    if (!ok) {
        return {};
    }
    const time_t t = static_cast<time_t>(header.savedAt);
    tm local{};
    localtime_r(&t, &local);
    char text[64];
    strftime(text, sizeof(text), "%b %e, %H:%M", &local);
    return text;
}

}  // namespace port::savestate
