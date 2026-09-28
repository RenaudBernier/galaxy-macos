#pragma once

#include <revolution.h>

namespace nw4r {
    namespace ut {
        typedef u32 SigWord;

        // Overlaid on (big-endian) file data.
        struct BinaryFileHeader {
            BE(SigWord) signature;
            BE(u16) byteOrder;
            BE(u16) version;
            BE(u32) fileSize;
            BE(u16) headerSize;
            BE(u16) dataBlocks;
        };

        struct BinaryBlockHeader {
            BE(SigWord) kind;
            BE(u32) size;
        };

        bool IsValidBinaryFile(const BinaryFileHeader *, u32, u16, u16);
    };
};
