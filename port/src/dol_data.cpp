// Game data that the original build linked into main.dol (see dol_data.hpp).
//
// The bytes are copied from the user's disc image at startup; this file only
// knows where they live in the RMGK01 executable.

#include "port/dol_data.hpp"
#include "port/log.hpp"

#include <nod.h>

#include <cstring>

// Referenced by game code as `extern const u8 StoryEventBCSV[]` and
// `extern const JMapData GalaxyIDBCSV`. Global variable names are not
// type-mangled, so plain byte arrays satisfy both. They live in the
// executable's data segment, i.e. inside the emulated Wii address window.
alignas(32) unsigned char StoryEventBCSV[0x1E0];
alignas(32) unsigned char GalaxyIDBCSV[0xD20];

namespace port::dol {
namespace {

struct Blob {
    const char* name;
    uint32_t address;  // virtual address in RMGK01's main.dol
    uint32_t size;
    unsigned char* dest;
};

const Blob kBlobs[] = {
    {"StoryEventBCSV", 0x8053DC20, sizeof(StoryEventBCSV), StoryEventBCSV},
    {"GalaxyIDBCSV", 0x8053DE00, sizeof(GalaxyIDBCSV), GalaxyIDBCSV},
};

uint32_t be32(const uint8_t* p) {
    return (uint32_t(p[0]) << 24) | (uint32_t(p[1]) << 16) | (uint32_t(p[2]) << 8) | uint32_t(p[3]);
}

// DOL header: 7 text + 11 data sections; per section a file offset (0x00),
// load address (0x48) and size (0x90), all big-endian.
bool dolFileOffset(const uint8_t* dol, size_t dolSize, uint32_t addr, uint32_t size, size_t* out) {
    if (dolSize < 0x100) {
        return false;
    }
    for (int i = 0; i < 18; i++) {
        const uint32_t off = be32(dol + 0x00 + i * 4);
        const uint32_t va = be32(dol + 0x48 + i * 4);
        const uint32_t len = be32(dol + 0x90 + i * 4);
        if (len != 0 && addr >= va && addr + size <= va + len) {
            const size_t fileOff = size_t(off) + (addr - va);
            if (fileOff + size > dolSize) {
                return false;
            }
            *out = fileOff;
            return true;
        }
    }
    return false;
}

}  // namespace

bool loadEmbeddedData(const char* discPath) {
    NodHandle* disc = nullptr;
    if (nod_disc_open(discPath, nullptr, &disc) != NOD_RESULT_OK) {
        PORT_ERROR("dol", "cannot open disc image '{}': {}", discPath, nod_error_message());
        return false;
    }

    NodDiscHeader header{};
    nod_disc_header(disc, &header);
    if (memcmp(header.game_id, "RMGK01", 6) != 0) {
        PORT_ERROR("dol", "disc is {:.6s}; this build needs Super Mario Galaxy RMGK01 (Korea, rev 0)",
                   header.game_id);
        nod_free(disc);
        return false;
    }

    NodHandle* part = nullptr;
    if (nod_disc_open_partition_kind(disc, NOD_PARTITION_KIND_DATA, nullptr, &part) != NOD_RESULT_OK) {
        PORT_ERROR("dol", "cannot open the disc's data partition: {}", nod_error_message());
        nod_free(disc);
        return false;
    }

    bool ok = true;
    NodPartitionMeta meta{};
    if (nod_partition_meta(part, &meta) != NOD_RESULT_OK || meta.raw_dol.data == nullptr) {
        PORT_ERROR("dol", "cannot read main.dol: {}", nod_error_message());
        ok = false;
    } else {
        for (const Blob& b : kBlobs) {
            size_t off;
            if (!dolFileOffset(meta.raw_dol.data, meta.raw_dol.size, b.address, b.size, &off)) {
                PORT_ERROR("dol", "{} (0x{:08X}) is not in main.dol", b.name, b.address);
                ok = false;
                continue;
            }
            memcpy(b.dest, meta.raw_dol.data + off, b.size);
        }
    }

    nod_free(part);
    nod_free(disc);
    if (ok) {
        PORT_INFO("dol", "loaded embedded game data from main.dol");
    }
    return ok;
}

}  // namespace port::dol
