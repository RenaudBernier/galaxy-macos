#pragma once

#include <revolution.h>

namespace nw4r {
    namespace ut {
        struct CharWidths {
            s8 left;
            u8 glyphWidth;
            s8 charWidth;
        };

        // BRFNT blocks, overlaid on (big-endian) file data. The pointer fields
        // hold file offsets until ResFont::Rebuild relocates them.
        struct FontTextureGlyph {
            u8 cellWidth;
            u8 cellHeight;
            s8 baselinePos;
            u8 maxCharWidth;
            BE(u32) sheetSize;
            BE(u16) sheetNum;
            BE(u16) sheetFormat;
            BE(u16) sheetRow;
            BE(u16) sheetLine;
            BE(u16) sheetWidth;
            BE(u16) sheetHeight;
            PTR32(u8) sheetImage;
        };

        struct FontWidth {
            BE(u16) indexBegin;
            BE(u16) indexEnd;
            PTR32(FontWidth) pNext;
            CharWidths widthTable[];
        };

        struct FontCodeMap {
            BE(u16) ccodeBegin;
            BE(u16) ccodeEnd;
            BE(u16) mappingMethod;
            BE(u16) reserved;
            PTR32(FontCodeMap) pNext;
            BE(u16) mapInfo[];
        };

        struct FontInformation {
            u8 fontType;
            s8 linefeed;
            BE(u16) alterCharIndex;
            CharWidths defaultWidth;
            u8 encoding;
            PTR32(FontTextureGlyph) pGlyph;
            PTR32(FontWidth) pWidth;
            PTR32(FontCodeMap) pMap;
            u8 height;
            u8 width;
            u8 ascent;
            u8 padding_[1];
        };
    };  // namespace ut
};  // namespace nw4r
