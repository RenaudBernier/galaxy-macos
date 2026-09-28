#include "nw4r/ut/RomFont.h"
#include <cstring>

namespace nw4r {
    namespace ut {
        RomFont::RomFont() : mFontHeader(NULL), mAlternateChar('?') {
            mDefaultWidths.left = 0;
            mDefaultWidths.glyphWidth = 0;
            mDefaultWidths.charWidth = 0;
        }

        RomFont::~RomFont() {
        }

#ifdef TARGET_PC
        // The Wii system font lives in the console's IPL ROM and is not
        // available on the host; RomFont behaves as an empty font.
        int RomFont::GetWidth() const {
            return 0;
        }
        int RomFont::GetHeight() const {
            return 0;
        }
        int RomFont::GetAscent() const {
            return 0;
        }
        int RomFont::GetDescent() const {
            return 0;
        }
        int RomFont::GetBaselinePos() const {
            return 0;
        }
        int RomFont::GetCellHeight() const {
            return 0;
        }
        int RomFont::GetCellWidth() const {
            return 0;
        }
        int RomFont::GetMaxCharWidth() const {
            return 0;
        }
        Font::Type RomFont::GetType() const {
            return TYPE_ROM;
        }
        GXTexFmt RomFont::GetTextureFormat() const {
            return GX_TF_I4;
        }
        int RomFont::GetLineFeed() const {
            return 0;
        }
        const CharWidths RomFont::GetDefaultCharWidths() const {
            return mDefaultWidths;
        }
        void RomFont::SetDefaultCharWidths(const CharWidths& widths) {
            mDefaultWidths = widths;
        }
        bool RomFont::SetAlternateChar(CharCode c) {
            mAlternateChar = c;
            return false;
        }
        void RomFont::SetLineFeed(int) {
        }
        int RomFont::GetCharWidth(CharCode) const {
            return 0;
        }
        const CharWidths RomFont::GetCharWidths(CharCode) const {
            return mDefaultWidths;
        }
        void RomFont::GetGlyph(Glyph* glyphPtr, CharCode) const {
            memset(glyphPtr, 0, sizeof(*glyphPtr));
        }
        bool RomFont::HasGlyph(CharCode) const {
            return false;
        }
        FontEncoding RomFont::GetEncoding() const {
            return FONT_ENCODING_UTF16;
        }
#endif

    }
}
