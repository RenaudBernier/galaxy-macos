#pragma once

#include "nw4r/lyt/types.h"
#include "nw4r/math/types.h"
#include <revolution.h>

namespace nw4r {
    namespace lyt {
        namespace res {
            // Resource structures are overlaid on (big-endian) BRLYT/BRLAN data.
            // Multi-byte values use BE(T); the Res* aggregate types below are the
            // big-endian twins of the runtime types, converting on read.
#ifdef TARGET_PC
            struct ResVEC2 {
                BE(f32) x;
                BE(f32) y;
                operator math::VEC2() const { return math::VEC2(x, y); }
            };

            struct ResVEC3 {
                BE(f32) x;
                BE(f32) y;
                BE(f32) z;
                operator math::VEC3() const { return math::VEC3(x, y, z); }
            };

            struct ResSize {
                BE(f32) width;
                BE(f32) height;
                operator Size() const { return Size(width, height); }
            };

            struct ResInflationLRTB {
                BE(f32) l;
                BE(f32) r;
                BE(f32) t;
                BE(f32) b;
                operator InflationLRTB() const {
                    InflationLRTB v;
                    v.l = l;
                    v.r = r;
                    v.t = t;
                    v.b = b;
                    return v;
                }
            };

            struct ResColorS10 {
                BE(s16) r;
                BE(s16) g;
                BE(s16) b;
                BE(s16) a;
                operator GXColorS10() const {
                    GXColorS10 v;
                    v.r = r;
                    v.g = g;
                    v.b = b;
                    v.a = a;
                    return v;
                }
            };

            struct ResTexSRT {
                ResVEC2 translate;
                BE(f32) rotate;
                ResVEC2 scale;
                operator TexSRT() const {
                    TexSRT v;
                    v.translate = translate;
                    v.rotate = rotate;
                    v.scale = scale;
                    return v;
                }
            };
#else
            typedef math::VEC2 ResVEC2;
            typedef math::VEC3 ResVEC3;
            typedef Size ResSize;
            typedef InflationLRTB ResInflationLRTB;
            typedef GXColorS10 ResColorS10;
            typedef TexSRT ResTexSRT;
#endif

            const u32 FILESIGNATURE_RLYT = 'RLYT';

            const u32 DATABLOCKKIND_LAYOUT = 'lyt1';
            const u32 DATABLOCKKIND_PANE = 'pan1';
            const u32 DATABLOCKKIND_PANEBEGIN = 'pas1';
            const u32 DATABLOCKKIND_PANEEND = 'pae1';
            const u32 DATABLOCKKIND_PICTURE = 'pic1';
            const u32 DATABLOCKKIND_TEXTBOX = 'txt1';
            const u32 DATABLOCKKIND_WINDOW = 'wnd1';
            const u32 DATABLOCKKIND_BOUNDING = 'bnd1';
            const u32 DATABLOCKKIND_GROUP = 'grp1';
            const u32 DATABLOCKKIND_GROUPBEGIN = 'grs1';
            const u32 DATABLOCKKIND_GROUPEND = 'gre1';
            const u32 DATABLOCKKIND_FONTLIST = 'fnl1';
            const u32 DATABLOCKKIND_TEXTURELIST = 'txl1';
            const u32 DATABLOCKKIND_MATERIALLIST = 'mat1';
            const u32 DATABLOCKKIND_USERDATALIST = 'usd1';

            const u32 FILESIGNATURE_RLAN = 'RLAN';

            const u32 ANIMATIONTYPE_RLPA = 'RLPA';
            const u32 ANIMATIONTYPE_RLVI = 'RLVI';
            const u32 ANIMATIONTYPE_RLVC = 'RLVC';
            const u32 ANIMATIONTYPE_RLMC = 'RLMC';
            const u32 ANIMATIONTYPE_RLTS = 'RLTS';
            const u32 ANIMATIONTYPE_RLTP = 'RLTP';
            const u32 ANIMATIONTYPE_RLIM = 'RLIM';

            const u32 DATABLOCKKIND_PANEANIMTAG = 'pat1';
            const u32 DATABLOCKKIND_PANEANIMSHARE = 'pah1';
            const u32 DATABLOCKKIND_PANEANIMINFO = 'pai1';

            const u32 RESOURCETYPE_LAYOUT = 'blyt';
            const u32 RESOURCETYPE_ANIMATION = 'anim';
            const u32 RESOURCETYPE_TEXTURE = 'timg';
            const u32 RESOURCETYPE_FONT = 'font';
            const u32 RESOURCETYPE_ARCHIVEFONT = 'fnta';

            struct BinaryFileHeader {
                char signature[4];
                BE(u16) byteOrder;
                BE(u16) version;
                BE(u32) fileSize;
                BE(u16) headerSize;
                BE(u16) dataBlocks;
            };

            struct DataBlockHeader {
                char kind[4];
                BE(u32) size;
            };

            struct Layout {
                DataBlockHeader blockHeader;
                u8 originType;
                u8 padding[3];
                ResSize layoutSize;
            };

            struct Font {
                BE(u32) nameStrOffset;
                u8 type;
                u8 padding[3];
            };

            struct FontList {
                DataBlockHeader blockHeader;
                BE(u16) fontNum;
                u8 padding[2];
            };

            struct Texture {
                BE(u32) nameStrOffset;
                u8 type;
                u8 padding[3];
            };

            struct TextureList {
                DataBlockHeader blockHeader;
                BE(u16) texNum;
                u8 padding[2];
            };

            struct TexMap {
#ifdef TARGET_PC
                TexMap() : wrapSflt(0), wrapTflt(0) {
                    texIdx = 0;
                }
#else
                TexMap() : texIdx(0), wrapSflt(0), wrapTflt(0) {
                }
#endif

                GXTexWrapMode GetWarpModeS() const {
                    return GXTexWrapMode(detail::GetBits(wrapSflt, 0, 2));
                }

                GXTexWrapMode GetWarpModeT() const {
                    return GXTexWrapMode(detail::GetBits(wrapTflt, 0, 2));
                }

                GXTexFilter GetMinFilter() const {
                    const int bitLen = 3;
                    const u8 bitData = detail::GetBits(wrapSflt, 2, bitLen);
                    return GXTexFilter(detail::GetBits(bitData + GX_LINEAR, 0, bitLen));
                }

                GXTexFilter GetMagFilter() const {
                    const int bitLen = 1;
                    const u8 bitData = detail::GetBits(wrapTflt, 2, bitLen);
                    return GXTexFilter(detail::GetBits(bitData + GX_LINEAR, 0, bitLen));
                }

                void SetWarpModeS(GXTexWrapMode value) {
                    detail::SetBits(&wrapSflt, 0, 2, u8(value));
                }

                void SetWarpModeT(GXTexWrapMode value) {
                    detail::SetBits(&wrapTflt, 0, 2, u8(value));
                }

                void SetMinFilter(GXTexFilter value) {
                    const int bitLen = 3;
                    const u8 bitData = u8(detail::GetBits(value - GX_LINEAR, 0, bitLen));  // Set to zero when (value = GX_LINEAR)
                    detail::SetBits(&wrapSflt, 2, bitLen, bitData);
                }

                void SetMagFilter(GXTexFilter value) {
                    const int bitLen = 1;
                    const u8 bitData = u8(detail::GetBits(value - GX_LINEAR, 0, bitLen));
                    detail::SetBits(&wrapTflt, 2, bitLen, bitData);
                }

                BE(u16) texIdx;
                u8 wrapSflt;
                u8 wrapTflt;
            };

            struct MaterialList {
                DataBlockHeader blockHeader;
                BE(u16) materialNum;
                u8 padding[2];
            };

            struct Pane {
                DataBlockHeader blockHeader;
                u8 flag;
                u8 basePosition;
                u8 alpha;
                u8 padding;
                char name[16];
                char userData[8];
                ResVEC3 translate;
                ResVEC3 rotate;
                ResVEC2 scale;
                ResSize size;
            };

            struct Picture : Pane {
                BE(u32) vtxCols[4];
                BE(u16) materialIdx;
                u8 texCoordNum;
                u8 padding[1];
            };

            struct TextBox : Pane {
                BE(u16) textBufBytes;
                BE(u16) textStrBytes;
                BE(u16) materialIdx;
                BE(u16) fontIdx;
                u8 textPosition;
                u8 textAlignment;
                u8 padding[2];
                BE(u32) textStrOffset;
                BE(u32) textCols[2];
                ResSize fontSize;
                BE(f32) charSpace;
                BE(f32) lineSpace;
            };

            struct WindowFrame {
                BE(u16) materialIdx;
                u8 textureFlip;
                u8 padding1;
            };

            struct WindowContent {
                BE(u32) vtxCols[4];
                BE(u16) materialIdx;
                u8 texCoordNum;
                u8 padding[1];
            };

            struct Window : Pane {
                ResInflationLRTB inflation;
                u8 frameNum;
                u8 padding1;
                u8 padding2;
                u8 padding3;
                BE(u32) contentOffset;
                BE(u32) frameOffsetTableOffset;
            };

            struct Bounding : Pane {};

            struct ExtUserDataList {
                DataBlockHeader blockHeader;
                BE(detail::ResU16) num;
                u8 padding[2];
            };

            struct Group {
                DataBlockHeader blockHeader;
                char name[16];
                BE(u16) paneNum;
                u8 padding[2];
            };

            struct MaterialResourceNum {
                u8 GetTexMapNum() const NO_INLINE {
                    return u8(detail::GetBits< u32 >(bits, 0, 4));
                }

                u8 GetTexSRTNum() const NO_INLINE {
                    return u8(detail::GetBits< u32 >(bits, 4, 4));
                }

                u8 GetTexCoordGenNum() const NO_INLINE {
                    return u8(detail::GetBits< u32 >(bits, 8, 4));
                }

                bool HasTevSwapTable() const NO_INLINE {
                    return detail::TestBit< u32 >(bits, 12);
                }

                u8 GetIndTexSRTNum() const NO_INLINE {
                    return u8(detail::GetBits< u32 >(bits, 13, 2));
                }

                u8 GetIndTexStageNum() const NO_INLINE {
                    return u8(detail::GetBits< u32 >(bits, 15, 3));
                }

                u8 GetTevStageNum() const NO_INLINE {
                    return u8(detail::GetBits< u32 >(bits, 18, 5));
                }

                bool HasAlphaCompare() const NO_INLINE {
                    return detail::TestBit< u32 >(bits, 23);
                }

                bool HasBlendMode() const NO_INLINE {
                    return detail::TestBit< u32 >(bits, 24);
                }

                u8 GetChanCtrlNum() const NO_INLINE {
                    return u8(detail::GetBits< u32 >(bits, 25, 1));
                }

                u8 GetMatColNum() const NO_INLINE {
                    return u8(detail::GetBits< u32 >(bits, 27, 1));
                }

                BE(u32) bits;
            };

            struct Material {
                char name[20];
                ResColorS10 tevCols[3];
                GXColor tevKCols[4];
                MaterialResourceNum resNum;
            };

            struct AnimationTagBlock {
                DataBlockHeader blockHeader;
                BE(detail::ResU16) tagOrder;
                BE(detail::ResU16) groupNum;
                BE(detail::ResU32) nameOffset;
                BE(detail::ResU32) groupsOffset;
                BE(detail::ResS16) startFrame;
                BE(detail::ResS16) endFrame;
                u8 flag;
                u8 padding[3];
            };

            struct AnimationShareBlock {
                DataBlockHeader blockHeader;
                BE(detail::ResU32) animShareInfoOffset;
                BE(detail::ResU16) shareNum;
                u8 padding[2];
            };

            struct AnimationBlock {
                DataBlockHeader blockHeader;
                BE(u16) frameSize;
                u8 loop;
                u8 padding1;
                BE(u16) fileNum;
                BE(u16) animContNum;
                BE(u32) animContOffsetsOffset;
            };

            struct AnimationContent {
                char name[MaterialNameStrMax];

                u8 num;
                u8 type;
                u8 padding[2];
            };

            struct AnimationInfo {
                BE(u32) kind;

                u8 num;
                u8 padding[3];
            };

            struct AnimationTarget {
                u8 id;
                u8 target;
                u8 curveType;
                u8 padding1;

                BE(u16) keyNum;
                u8 padding2[2];

                BE(u32) keysOffset;
            };

            struct HermiteKey {
                BE(f32) frame;
                BE(f32) value;
                BE(f32) slope;
            };

            struct StepKey {
                BE(f32) frame;
                BE(u16) value;
                u16 pad;
            };
        };  // namespace res
    };  // namespace lyt
};  // namespace nw4r
