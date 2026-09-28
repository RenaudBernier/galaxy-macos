// ARC: the RVL SDK's reader for U8 archives held in memory (used by nw4r
// layouts). Same algorithm as the SDK, with explicit big-endian reads of the
// archive header and file table; the archive itself is left untouched.

#include "port/log.hpp"

#include <revolution/arc.h>

#include <cstring>

namespace {

constexpr u32 kArcMagic = 0x55AA382D;

inline u32 be32(const void* p) {
    const u8* b = static_cast<const u8*>(p);
    return (u32(b[0]) << 24) | (u32(b[1]) << 16) | (u32(b[2]) << 8) | u32(b[3]);
}

// File-table entries: 12 bytes, big-endian.
//   +0 isDir (top byte) | name offset (low 24 bits)
//   +4 parent dir (dirs) / file data offset (files)
//   +8 next entry past this dir's subtree (dirs) / file length (files)
struct Fst {
    const u8* base;
    u32 word(u32 i, u32 w) const { return be32(base + i * 12 + w * 4); }
    bool isDir(u32 i) const { return (word(i, 0) & 0xFF000000) != 0; }
    u32 stringOff(u32 i) const { return word(i, 0) & 0x00FFFFFF; }
    u32 parent(u32 i) const { return word(i, 1); }
    u32 position(u32 i) const { return word(i, 1); }
    u32 next(u32 i) const { return word(i, 2); }
    u32 length(u32 i) const { return word(i, 2); }
};

Fst fst(const ARCHandle* h) { return Fst{static_cast<const u8*>(h->FSTStart)}; }

inline int lower(int c) { return (c >= 'A' && c <= 'Z') ? c - 'A' + 'a' : c; }

bool isSame(const char* path, const char* string) {
    while (*string != '\0') {
        if (lower(static_cast<unsigned char>(*path++)) != lower(static_cast<unsigned char>(*string++))) {
            return false;
        }
    }
    return *path == '/' || *path == '\0';
}

u32 copyName(char* dest, const char* src, u32 maxlen) {
    u32 i = maxlen;
    while (i > 0 && *src != '\0') {
        *dest++ = *src++;
        i--;
    }
    return maxlen - i;
}

u32 entryToPath(ARCHandle* handle, u32 entry, char* path, u32 maxlen) {
    if (entry == 0) {
        return 0;
    }
    const Fst f = fst(handle);
    const char* name = handle->FSTStringStart + f.stringOff(entry);
    u32 loc = entryToPath(handle, f.parent(entry), path, maxlen);
    if (loc == maxlen) {
        return loc;
    }
    path[loc++] = '/';
    loc += copyName(path + loc, name, maxlen - loc);
    return loc;
}

}  // namespace

extern "C" {

BOOL ARCInitHandle(void* arcStart, ARCHandle* handle) {
    const u8* hdr = static_cast<const u8*>(arcStart);
    if (be32(hdr) != kArcMagic) {
        PORT_ERROR("arc", "ARCInitHandle: bad archive format");
        return FALSE;
    }
    const u32 fstStart = be32(hdr + 4);
    const u32 fstSize = be32(hdr + 8);
    const u32 fileStart = be32(hdr + 12);

    handle->archiveStartAddr = arcStart;
    handle->FSTStart = static_cast<u8*>(arcStart) + fstStart;
    handle->fileStart = static_cast<u8*>(arcStart) + fileStart;
    handle->entryNum = fst(handle).next(0);
    handle->FSTStringStart = static_cast<char*>(handle->FSTStart) + handle->entryNum * 12;
    handle->FSTLength = fstSize;
    handle->currDir = 0;
    return TRUE;
}

s32 ARCConvertPathToEntrynum(ARCHandle* handle, const char* pathPtr) {
    const Fst f = fst(handle);
    u32 dirLookAt = handle->currDir;

    while (true) {
        if (*pathPtr == '\0') {
            return static_cast<s32>(dirLookAt);
        }
        if (*pathPtr == '/') {
            dirLookAt = 0;
            pathPtr++;
            continue;
        }
        if (*pathPtr == '.') {
            if (pathPtr[1] == '.') {
                if (pathPtr[2] == '/') {
                    dirLookAt = f.parent(dirLookAt);
                    pathPtr += 3;
                    continue;
                }
                if (pathPtr[2] == '\0') {
                    return static_cast<s32>(f.parent(dirLookAt));
                }
            } else if (pathPtr[1] == '/') {
                pathPtr += 2;
                continue;
            } else if (pathPtr[1] == '\0') {
                return static_cast<s32>(dirLookAt);
            }
        }

        const char* end = pathPtr;
        while (*end != '\0' && *end != '/') {
            end++;
        }
        const bool isDir = *end != '\0';
        const s32 length = static_cast<s32>(end - pathPtr);

        u32 i = dirLookAt + 1;
        bool found = false;
        while (i < f.next(dirLookAt)) {
            if (!f.isDir(i) && isDir) {
                i++;
                continue;
            }
            const char* name = handle->FSTStringStart + f.stringOff(i);
            if (name[0] == '.' && name[1] == '\0') {
                i++;
                continue;
            }
            if (isSame(pathPtr, name)) {
                found = true;
                break;
            }
            i = f.isDir(i) ? f.next(i) : i + 1;
        }
        if (!found) {
            return -1;
        }
        if (!isDir) {
            return static_cast<s32>(i);
        }
        dirLookAt = i;
        pathPtr += length + 1;
    }
}

BOOL ARCOpen(ARCHandle* handle, const char* fileName, ARCFileInfo* af) {
    const s32 entry = ARCConvertPathToEntrynum(handle, fileName);
    if (entry < 0) {
        PORT_WARN("arc", "ARCOpen: '{}' not found in archive", fileName);
        return FALSE;
    }
    const Fst f = fst(handle);
    if (f.isDir(static_cast<u32>(entry))) {
        return FALSE;
    }
    af->handle = handle;
    af->startOffset = f.position(static_cast<u32>(entry));
    af->length = f.length(static_cast<u32>(entry));
    return TRUE;
}

BOOL ARCFastOpen(ARCHandle* handle, s32 entrynum, ARCFileInfo* af) {
    const Fst f = fst(handle);
    if (entrynum < 0 || static_cast<u32>(entrynum) >= handle->entryNum || f.isDir(static_cast<u32>(entrynum))) {
        return FALSE;
    }
    af->handle = handle;
    af->startOffset = f.position(static_cast<u32>(entrynum));
    af->length = f.length(static_cast<u32>(entrynum));
    return TRUE;
}

BOOL ARCGetCurrentDir(ARCHandle* handle, char* path, u32 maxlen) {
    const Fst f = fst(handle);
    u32 loc = entryToPath(handle, handle->currDir, path, maxlen);
    if (loc == maxlen) {
        path[maxlen - 1] = '\0';
        return FALSE;
    }
    if (f.isDir(handle->currDir)) {
        if (loc == maxlen - 1) {
            path[loc] = '\0';
            return FALSE;
        }
        path[loc++] = '/';
    }
    path[loc] = '\0';
    return TRUE;
}

void* ARCGetStartAddrInMem(ARCFileInfo* af) {
    return static_cast<u8*>(af->handle->archiveStartAddr) + af->startOffset;
}

u32 ARCGetLength(ARCFileInfo* af) { return af->length; }

BOOL ARCClose(ARCFileInfo*) { return TRUE; }

BOOL ARCChangeDir(ARCHandle* handle, const char* dirName) {
    const s32 entry = ARCConvertPathToEntrynum(handle, dirName);
    if (entry < 0 || !fst(handle).isDir(static_cast<u32>(entry))) {
        return FALSE;
    }
    handle->currDir = static_cast<u32>(entry);
    return TRUE;
}

BOOL ARCOpenDir(ARCHandle* handle, const char* dirName, ARCDir* dir) {
    const s32 entry = ARCConvertPathToEntrynum(handle, dirName);
    const Fst f = fst(handle);
    if (entry < 0 || !f.isDir(static_cast<u32>(entry))) {
        return FALSE;
    }
    dir->handle = handle;
    dir->entryNum = static_cast<u32>(entry);
    dir->location = static_cast<u32>(entry) + 1;
    dir->next = f.next(static_cast<u32>(entry));
    return TRUE;
}

BOOL ARCReadDir(ARCDir* dir, ARCDirEntry* dirent) {
    ARCHandle* handle = dir->handle;
    const Fst f = fst(handle);
    u32 loc = dir->location;
    while (true) {
        if (loc <= dir->entryNum || dir->next <= loc) {
            return FALSE;
        }
        dirent->handle = handle;
        dirent->entryNum = loc;
        dirent->isDir = f.isDir(loc);
        dirent->name = handle->FSTStringStart + f.stringOff(loc);
        if (dirent->name[0] == '.' && dirent->name[1] == '\0') {
            loc++;
            continue;
        }
        break;
    }
    dir->location = f.isDir(loc) ? f.next(loc) : loc + 1;
    return TRUE;
}

BOOL ARCCloseDir(ARCDir*) { return TRUE; }

}  // extern "C"
