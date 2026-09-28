// NAND flash file system on the host file system.
//
// NAND paths are mapped under ~/Library/Application Support/SuperMarioGalaxy/nand
// (or $SMG_NAND_DIR).
// Relative paths resolve against the title's data directory (the "home dir"),
// as on the Wii. Asynchronous calls do their I/O immediately and deliver the
// completion callback as an interrupt, like the IOS reply would.

#include "os/scheduler.hpp"

#include <revolution/nand.h>

#include <cerrno>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <mutex>
#include <string>
#include <system_error>
#include <unordered_map>

namespace fs = std::filesystem;

namespace {

// "RMGK" as the title ID's lower half, under the disc-title group 00010000.
constexpr const char* kHomeDir = "/title/00010000/524d474b/data";

std::mutex sMutex;
std::unordered_map<NANDFileInfo*, FILE*> sFiles;
s32 sNextFd = 1;

const fs::path& nandRoot() {
    static const fs::path root = [] {
        // SMG_NAND_DIR overrides the location (e.g. a scratch NAND for tests).
        const char* override = getenv("SMG_NAND_DIR");
        const char* home = getenv("HOME");
        fs::path p = override != nullptr && *override != '\0'
                         ? fs::path(override)
                         : fs::path(home ? home : ".") / "Library/Application Support/SuperMarioGalaxy/nand";
        std::error_code ec;
        fs::create_directories(p / "title/00010000/524d474b/data", ec);
        fs::create_directories(p / "tmp", ec);
        fs::create_directories(p / "shared2", ec);
        return p;
    }();
    return root;
}

// Resolves a NAND path ("GameData.bin", "/tmp/banner.bin", "..") to an
// absolute NAND path, then to a host path.
std::string nandAbsPath(const char* path) {
    std::string base = (path && path[0] == '/') ? "" : kHomeDir;
    std::string in = path ? path : "";
    std::string out;
    std::vector<std::string> parts;
    auto split = [&](const std::string& s) {
        size_t i = 0;
        while (i <= s.size()) {
            size_t j = s.find('/', i);
            if (j == std::string::npos) {
                j = s.size();
            }
            std::string seg = s.substr(i, j - i);
            if (seg.empty() || seg == ".") {
            } else if (seg == "..") {
                if (!parts.empty()) {
                    parts.pop_back();
                }
            } else {
                parts.push_back(seg);
            }
            i = j + 1;
        }
    };
    split(base);
    split(in);
    for (auto& p : parts) {
        out += "/" + p;
    }
    return out.empty() ? "/" : out;
}

fs::path hostPath(const char* nandPath) {
    return nandRoot() / nandAbsPath(nandPath).substr(1);
}

s32 errnoToNand(int e) {
    switch (e) {
    case ENOENT:
    case ENOTDIR:
        return NAND_RESULT_NOEXISTS;
    case EEXIST:
        return NAND_RESULT_EXISTS;
    case ENOSPC:
        return NAND_RESULT_MAXBLOCKS;
    case EACCES:
    case EPERM:
        return NAND_RESULT_ACCESS;
    case ENOTEMPTY:
        return NAND_RESULT_NOTEMPTY;
    case EMFILE:
    case ENFILE:
        return NAND_RESULT_MAXFD;
    default:
        return NAND_RESULT_UNKNOWN;
    }
}

FILE* fileFor(NANDFileInfo* info) {
    std::lock_guard lock(sMutex);
    auto it = sFiles.find(info);
    return it == sFiles.end() ? nullptr : it->second;
}

void complete(NANDCallback cb, s32 result, NANDCommandBlock* block) {
    if (block != nullptr) {
        block->callback = reinterpret_cast<void*>(cb);
    }
    if (cb != nullptr) {
        port::os::postInterrupt([cb, result, block] { cb(result, block); });
    }
}

s32 doCreate(const char* path, bool dir) {
    fs::path hp = hostPath(path);
    std::error_code ec;
    if (fs::exists(hp, ec)) {
        return NAND_RESULT_EXISTS;
    }
    if (!fs::exists(hp.parent_path(), ec)) {
        return NAND_RESULT_NOEXISTS;
    }
    if (dir) {
        return fs::create_directory(hp, ec) ? NAND_RESULT_OK : errnoToNand(ec.value());
    }
    FILE* f = fopen(hp.c_str(), "wb");
    if (f == nullptr) {
        return errnoToNand(errno);
    }
    fclose(f);
    return NAND_RESULT_OK;
}

s32 doOpen(const char* path, NANDFileInfo* info, u8 accType) {
    fs::path hp = hostPath(path);
    std::error_code ec;
    if (!fs::is_regular_file(hp, ec)) {
        return fs::exists(hp, ec) ? NAND_RESULT_ACCESS : NAND_RESULT_NOEXISTS;
    }
    const char* mode = "rb";
    if (accType == NAND_ACCESS_WRITE || accType == NAND_ACCESS_RW) {
        mode = "r+b";
    }
    FILE* f = fopen(hp.c_str(), mode);
    if (f == nullptr) {
        return errnoToNand(errno);
    }
    std::lock_guard lock(sMutex);
    auto it = sFiles.find(info);
    if (it != sFiles.end() && it->second != nullptr) {
        fclose(it->second);
    }
    sFiles[info] = f;
    info->fileDescriptor = sNextFd++;
    info->origFd = info->fileDescriptor;
    snprintf(info->origPath, sizeof(info->origPath), "%s", nandAbsPath(path).c_str());
    info->tmpPath[0] = '\0';
    info->accType = accType;
    info->stage = 0;
    info->mark = 1;
    return NAND_RESULT_OK;
}

s32 doClose(NANDFileInfo* info) {
    std::lock_guard lock(sMutex);
    auto it = sFiles.find(info);
    if (it == sFiles.end()) {
        return NAND_RESULT_INVALID;
    }
    s32 result = NAND_RESULT_OK;
    if (it->second != nullptr && fclose(it->second) != 0) {
        result = errnoToNand(errno);
    }
    sFiles.erase(it);
    info->mark = 0;
    return result;
}

s32 doRead(NANDFileInfo* info, void* buf, u32 len) {
    FILE* f = fileFor(info);
    if (f == nullptr) {
        return NAND_RESULT_INVALID;
    }
    size_t n = fread(buf, 1, len, f);
    if (n < len && ferror(f)) {
        return errnoToNand(errno);
    }
    return static_cast<s32>(n);
}

s32 doWrite(NANDFileInfo* info, const void* buf, u32 len) {
    FILE* f = fileFor(info);
    if (f == nullptr) {
        return NAND_RESULT_INVALID;
    }
    size_t n = fwrite(buf, 1, len, f);
    fflush(f);
    if (n < len) {
        return errnoToNand(errno);
    }
    return static_cast<s32>(n);
}

s32 doSeek(NANDFileInfo* info, s32 offset, s32 whence) {
    FILE* f = fileFor(info);
    if (f == nullptr) {
        return NAND_RESULT_INVALID;
    }
    int w = whence == 1 ? SEEK_CUR : whence == 2 ? SEEK_END : SEEK_SET;
    if (fseek(f, offset, w) != 0) {
        return NAND_RESULT_INVALID;
    }
    return static_cast<s32>(ftell(f));
}

s32 doGetLength(NANDFileInfo* info, u32* length) {
    FILE* f = fileFor(info);
    if (f == nullptr) {
        return NAND_RESULT_INVALID;
    }
    long pos = ftell(f);
    fseek(f, 0, SEEK_END);
    long end = ftell(f);
    fseek(f, pos, SEEK_SET);
    *length = static_cast<u32>(end);
    return NAND_RESULT_OK;
}

s32 doDelete(const char* path) {
    fs::path hp = hostPath(path);
    std::error_code ec;
    if (!fs::exists(hp, ec)) {
        return NAND_RESULT_NOEXISTS;
    }
    fs::remove_all(hp, ec);
    return ec ? errnoToNand(ec.value()) : NAND_RESULT_OK;
}

}  // namespace

extern "C" {

s32 NANDInit(void) {
    (void)nandRoot();
    return NAND_RESULT_OK;
}

s32 NANDGetHomeDir(char path[NAND_MAX_PATH]) {
    snprintf(path, NAND_MAX_PATH, "%s", kHomeDir);
    return NAND_RESULT_OK;
}

s32 NANDCreate(const char* path, u8 perm, u8 attr) {
    (void)perm;
    (void)attr;
    return doCreate(path, false);
}

s32 NANDPrivateCreate(const char* path, u8 perm, u8 attr) {
    (void)perm;
    (void)attr;
    return doCreate(path, false);
}

s32 NANDPrivateCreateAsync(const char* path, u8 perm, u8 attr, NANDCallback cb, NANDCommandBlock* block) {
    (void)perm;
    (void)attr;
    complete(cb, doCreate(path, false), block);
    return NAND_RESULT_OK;
}

s32 NANDPrivateCreateDirAsync(const char* path, u8 perm, u8 attr, NANDCallback cb, NANDCommandBlock* block) {
    (void)perm;
    (void)attr;
    complete(cb, doCreate(path, true), block);
    return NAND_RESULT_OK;
}

s32 NANDOpen(const char* path, NANDFileInfo* info, u8 accType) { return doOpen(path, info, accType); }

s32 NANDPrivateOpen(const char* path, NANDFileInfo* info, u8 accType) { return doOpen(path, info, accType); }

s32 NANDOpenAsync(const char* path, NANDFileInfo* info, u8 accType, NANDCallback cb, NANDCommandBlock* block) {
    if (block != nullptr) {
        block->fileInfo = info;
    }
    complete(cb, doOpen(path, info, accType), block);
    return NAND_RESULT_OK;
}

s32 NANDPrivateOpenAsync(const char* path, NANDFileInfo* info, const u8 accType, NANDCallback cb,
                         NANDCommandBlock* block) {
    return NANDOpenAsync(path, info, accType, cb, block);
}

// "Safe" opens work on a temporary copy that replaces the original on close.
// Host writes are already durable enough; open the file directly.
s32 NANDPrivateSafeOpenAsync(const char* path, NANDFileInfo* info, u8 accType, void* buf, u32 len, NANDCallback cb,
                             NANDCommandBlock* block) {
    (void)buf;
    (void)len;
    return NANDOpenAsync(path, info, accType, cb, block);
}

s32 NANDSafeCloseAsync(NANDFileInfo* info, NANDCallback cb, NANDCommandBlock* block) {
    complete(cb, doClose(info), block);
    return NAND_RESULT_OK;
}

s32 NANDClose(NANDFileInfo* info) { return doClose(info); }

s32 NANDCloseAsync(NANDFileInfo* info, NANDCallback cb, NANDCommandBlock* block) {
    complete(cb, doClose(info), block);
    return NAND_RESULT_OK;
}

s32 NANDRead(NANDFileInfo* info, void* buf, u32 len) { return doRead(info, buf, len); }

s32 NANDReadAsync(NANDFileInfo* info, void* buf, u32 len, NANDCallback cb, NANDCommandBlock* block) {
    complete(cb, doRead(info, buf, len), block);
    return NAND_RESULT_OK;
}

s32 NANDWrite(NANDFileInfo* info, const void* buf, u32 len) { return doWrite(info, buf, len); }

s32 NANDWriteAsync(NANDFileInfo* info, const void* buf, u32 len, NANDCallback cb, NANDCommandBlock* block) {
    complete(cb, doWrite(info, buf, len), block);
    return NAND_RESULT_OK;
}

s32 NANDSeek(NANDFileInfo* info, s32 offset, s32 whence) { return doSeek(info, offset, whence); }

s32 NANDSeekAsync(NANDFileInfo* info, s32 offset, s32 whence, NANDCallback cb, NANDCommandBlock* block) {
    complete(cb, doSeek(info, offset, whence), block);
    return NAND_RESULT_OK;
}

s32 NANDGetLength(NANDFileInfo* info, u32* length) { return doGetLength(info, length); }

s32 NANDGetLengthAsync(NANDFileInfo* info, u32* length, NANDCallback cb, NANDCommandBlock* block) {
    complete(cb, doGetLength(info, length), block);
    return NAND_RESULT_OK;
}

s32 NANDDelete(const char* path) { return doDelete(path); }

s32 NANDPrivateDelete(const char* path) { return doDelete(path); }

s32 NANDPrivateDeleteAsync(const char* path, NANDCallback cb, NANDCommandBlock* block) {
    complete(cb, doDelete(path), block);
    return NAND_RESULT_OK;
}

// Moves `path` into directory `destDir`, keeping its name. An existing file at
// the destination is replaced.
s32 NANDMove(const char* path, const char* destDir) {
    fs::path src = hostPath(path);
    fs::path dir = hostPath(destDir);
    std::error_code ec;
    if (!fs::exists(src, ec)) {
        return NAND_RESULT_NOEXISTS;
    }
    if (!fs::is_directory(dir, ec)) {
        return NAND_RESULT_NOEXISTS;
    }
    fs::rename(src, dir / src.filename(), ec);
    return ec ? errnoToNand(ec.value()) : NAND_RESULT_OK;
}

// Free-space check: the host always has room for the game's save data.
s32 NANDCheck(u32 fsBlock, u32 inode, u32* answer) {
    (void)fsBlock;
    (void)inode;
    if (answer != nullptr) {
        *answer = 0;
    }
    return NAND_RESULT_OK;
}

s32 NANDGetStatus(const char* path, NANDStatus* stat) {
    std::error_code ec;
    if (!fs::exists(hostPath(path), ec)) {
        return NAND_RESULT_NOEXISTS;
    }
    stat->ownerId = 0;
    stat->groupId = 0;
    stat->attribute = 0;
    stat->permission = NAND_PERM_RUSR | NAND_PERM_WUSR | NAND_PERM_RGRP | NAND_PERM_WGRP;
    return NAND_RESULT_OK;
}

s32 NANDPrivateGetStatus(const char* path, NANDStatus* stat) { return NANDGetStatus(path, stat); }

void NANDSetUserData(NANDCommandBlock* block, void* data) { block->userData = data; }

void* NANDGetUserData(const NANDCommandBlock* block) { return block->userData; }

// Fills the save banner the Wii menu shows. Only the Wii menu reads it, so the
// fields are kept in host byte order.
void NANDInitBanner(NANDBanner* banner, u32 flag, const u16* title, const u16* comment) {
    memset(banner, 0, sizeof(*banner));
    banner->signature = NAND_BANNER_SIGNATURE;
    banner->flag = flag;
    for (int i = 0; i < NAND_BANNER_COMMENT_SIZE && title != nullptr && title[i] != 0; i++) {
        banner->comment[0][i] = title[i];
    }
    for (int i = 0; i < NAND_BANNER_COMMENT_SIZE && comment != nullptr && comment[i] != 0; i++) {
        banner->comment[1][i] = comment[i];
    }
}

}  // extern "C"
