// THP movies: host implementations of the SDK's THP decoders.
//
// A THP video frame is a baseline JPEG (YCbCr 4:2:0) with two quirks: the
// entropy-coded data has no 0xFF byte stuffing, and restart intervals only
// re-align the bit reader to a byte boundary (there are no RSTn markers). The
// SDK decoder writes the planes straight into GX I8 textures (8x4 texel
// tiles), which is what THPVideoDecode produces here: Y at full size, U and V
// at half size.
//
// THP audio is DSP-ADPCM, decoded as by the SDK (THPAudio.c) from big-endian
// frame headers.

#include "port/log.hpp"

#include <revolution/types.h>

#include <cmath>
#include <cstring>

namespace {

constexpr u8 kZigzag[64] = {
    0,  1,  8,  16, 9,  2,  3,  10, 17, 24, 32, 25, 18, 11, 4,  5,  12, 19, 26, 33, 40, 48,
    41, 34, 27, 20, 13, 6,  7,  14, 21, 28, 35, 42, 49, 56, 57, 50, 43, 36, 29, 22, 15, 23,
    30, 37, 44, 51, 58, 59, 52, 45, 38, 31, 39, 46, 53, 60, 61, 54, 47, 55, 62, 63,
};

struct HuffTable {
    bool valid = false;
    u8 vals[256];
    s32 mincode[17];
    s32 maxcode[18];
    s32 valptr[17];
};

struct Component {
    u8 id = 0;
    u8 quant = 0;
    u8 dcTable = 0;
    u8 acTable = 0;
    s32 pred = 0;
};

struct Decoder {
    const u8* data = nullptr;
    const u8* end = nullptr;
    u32 bitBuf = 0;
    s32 bitCount = 0;

    u16 quant[4][64] = {};
    HuffTable dc[4];
    HuffTable ac[4];
    Component comps[3];
    u32 width = 0;
    u32 height = 0;
    u32 restartInterval = 0;

    u32 bits(u32 n) {
        while (bitCount < static_cast<s32>(n)) {
            const u32 byte = data < end ? *data++ : 0;
            bitBuf = (bitBuf << 8) | byte;
            bitCount += 8;
        }
        bitCount -= n;
        return (bitBuf >> bitCount) & ((1u << n) - 1);
    }

    void alignToByte() { bitCount -= bitCount % 8; }

    s32 decodeHuffman(const HuffTable& h) {
        s32 code = 0;
        for (s32 len = 1; len <= 16; len++) {
            code = (code << 1) | static_cast<s32>(bits(1));
            if (code <= h.maxcode[len]) {
                return h.vals[h.valptr[len] + code - h.mincode[len]];
            }
        }
        return 0;  // corrupt data
    }

    static s32 extend(u32 v, u32 n) {
        return n == 0 ? 0 : (v < (1u << (n - 1)) ? static_cast<s32>(v) - static_cast<s32>((1u << n) - 1) : static_cast<s32>(v));
    }
};

bool readDQT(Decoder& d, const u8* p, u32 len) {
    const u8* end = p + len;
    while (p < end) {
        const u8 pq = p[0] >> 4, tq = p[0] & 3;
        p++;
        for (int k = 0; k < 64; k++) {
            d.quant[tq][k] = pq ? static_cast<u16>(p[k * 2] << 8 | p[k * 2 + 1]) : p[k];
        }
        p += pq ? 128 : 64;
    }
    return true;
}

bool readDHT(Decoder& d, const u8* p, u32 len) {
    const u8* end = p + len;
    while (p < end) {
        const u8 tc = p[0] >> 4, th = p[0] & 3;
        HuffTable& h = tc ? d.ac[th] : d.dc[th];
        const u8* counts = p + 1;
        u32 total = 0;
        for (int i = 0; i < 16; i++) {
            total += counts[i];
        }
        if (total > 256) {
            return false;
        }
        memcpy(h.vals, p + 17, total);
        s32 code = 0, k = 0;
        for (int len = 1; len <= 16; len++) {
            h.valptr[len] = k;
            h.mincode[len] = code;
            code += counts[len - 1];
            k += counts[len - 1];
            h.maxcode[len] = counts[len - 1] ? code - 1 : -1;
            code <<= 1;
        }
        h.maxcode[17] = 0x7FFFFFFF;
        h.valid = true;
        p += 17 + total;
    }
    return true;
}

// Separable float IDCT; `in` is dequantized, in natural order.
void idct8x8(const float in[64], u8 out[64]) {
    static float sCos[8][8];
    static bool sInit = false;
    if (!sInit) {
        for (int x = 0; x < 8; x++) {
            for (int u = 0; u < 8; u++) {
                const float c = u == 0 ? 1.0f / std::sqrt(2.0f) : 1.0f;
                sCos[x][u] = 0.5f * c * std::cos((2 * x + 1) * u * static_cast<float>(M_PI) / 16.0f);
            }
        }
        sInit = true;
    }
    float tmp[64];
    for (int v = 0; v < 8; v++) {  // rows: frequency u -> x
        const float* row = in + v * 8;
        for (int x = 0; x < 8; x++) {
            float s = 0.0f;
            for (int u = 0; u < 8; u++) {
                s += sCos[x][u] * row[u];
            }
            tmp[v * 8 + x] = s;
        }
    }
    for (int x = 0; x < 8; x++) {  // columns: frequency v -> y
        for (int y = 0; y < 8; y++) {
            float s = 0.0f;
            for (int v = 0; v < 8; v++) {
                s += sCos[y][v] * tmp[v * 8 + x];
            }
            const int value = static_cast<int>(std::lround(s + 128.0f));
            out[y * 8 + x] = static_cast<u8>(value < 0 ? 0 : (value > 255 ? 255 : value));
        }
    }
}

bool decodeBlock(Decoder& d, Component& c, u8 out[64]) {
    const HuffTable& dcTab = d.dc[c.dcTable];
    const HuffTable& acTab = d.ac[c.acTable];
    if (!dcTab.valid || !acTab.valid) {
        return false;
    }
    const u16* q = d.quant[c.quant];
    float coef[64] = {};
    const s32 t = d.decodeHuffman(dcTab);
    c.pred += Decoder::extend(t ? d.bits(t) : 0, t);
    coef[0] = static_cast<float>(c.pred * q[0]);
    for (int k = 1; k < 64;) {
        const s32 rs = d.decodeHuffman(acTab);
        const s32 r = rs >> 4, s = rs & 15;
        if (s == 0) {
            if (r != 15) {
                break;  // EOB
            }
            k += 16;
            continue;
        }
        k += r;
        if (k > 63) {
            break;
        }
        coef[kZigzag[k]] = static_cast<float>(Decoder::extend(d.bits(s), s) * q[k]);
        k++;
    }
    idct8x8(coef, out);
    return true;
}

// Writes an 8x8 block into a GX I8 texture (8x4 tiles) of the given width.
void storeBlock(u8* plane, u32 planeWidth, u32 bx, u32 by, const u8 block[64]) {
    for (u32 y = 0; y < 8; y++) {
        const u32 py = by + y;
        u8* row = plane + (py >> 2) * (planeWidth * 4) + (bx >> 3) * 32 + (py & 3) * 8;
        memcpy(row, block + y * 8, 8);
    }
}

s32 decodeFrame(const u8* file, u8* tileY, u8* tileU, u8* tileV) {
    Decoder d;
    const u8* p = file;
    const u8* const limit = file + (16u << 20);  // frames are far smaller
    for (;;) {
        if (p >= limit || *p++ != 0xFF) {
            return 3;  // bad syntax
        }
        while (*p == 0xFF) {
            p++;
        }
        const u8 marker = *p++;
        if (marker == 0xD8) {  // SOI
            continue;
        }
        const u32 len = static_cast<u32>(p[0] << 8 | p[1]);
        const u8* body = p + 2;
        switch (marker) {
        case 0xDB:
            readDQT(d, body, len - 2);
            break;
        case 0xC4:
            if (!readDHT(d, body, len - 2)) {
                return 3;
            }
            break;
        case 0xC0: {  // SOF0
            d.height = static_cast<u32>(body[1] << 8 | body[2]);
            d.width = static_cast<u32>(body[3] << 8 | body[4]);
            const u8 n = body[5];
            if (n != 3) {
                return 11;
            }
            for (int i = 0; i < 3; i++) {
                d.comps[i].id = body[6 + i * 3];
                d.comps[i].quant = body[8 + i * 3] & 3;
            }
            break;
        }
        case 0xDD:  // DRI
            d.restartInterval = static_cast<u32>(body[0] << 8 | body[1]);
            break;
        case 0xDA: {  // SOS: decode the scan
            const u8 n = body[0];
            for (int i = 0; i < n && i < 3; i++) {
                const u8 id = body[1 + i * 2];
                const u8 tables = body[2 + i * 2];
                for (auto& c : d.comps) {
                    if (c.id == id) {
                        c.dcTable = tables >> 4 & 3;
                        c.acTable = tables & 3;
                    }
                }
            }
            d.data = p + len;
            d.end = d.data + (8u << 20);
            if (d.width == 0 || d.height == 0 || (d.width & 15) || (d.height & 15)) {
                return 3;
            }
            const u32 mcuW = d.width / 16, mcuH = d.height / 16;
            u32 restartLeft = d.restartInterval;
            u8 block[64];
            for (u32 my = 0; my < mcuH; my++) {
                for (u32 mx = 0; mx < mcuW; mx++) {
                    for (u32 b = 0; b < 4; b++) {
                        if (!decodeBlock(d, d.comps[0], block)) {
                            return 3;
                        }
                        storeBlock(tileY, d.width, mx * 16 + (b & 1) * 8, my * 16 + (b >> 1) * 8, block);
                    }
                    if (!decodeBlock(d, d.comps[1], block)) {
                        return 3;
                    }
                    storeBlock(tileU, d.width / 2, mx * 8, my * 8, block);
                    if (!decodeBlock(d, d.comps[2], block)) {
                        return 3;
                    }
                    storeBlock(tileV, d.width / 2, mx * 8, my * 8, block);
                    if (d.restartInterval != 0 && --restartLeft == 0) {
                        restartLeft = d.restartInterval;
                        d.alignToByte();
                        for (auto& c : d.comps) {
                            c.pred = 0;
                        }
                    }
                }
            }
            return 0;
        }
        default:
            if (!((marker >= 0xE0 && marker <= 0xEF) || marker == 0xFE)) {
                return 11;  // unsupported marker
            }
            break;
        }
        p += len;
    }
}

u32 be32(const u8* p) { return static_cast<u32>(p[0] << 24 | p[1] << 16 | p[2] << 8 | p[3]); }
s16 be16(const u8* p) { return static_cast<s16>(p[0] << 8 | p[1]); }

struct AdpcmState {
    const u8* data;
    u32 nibble;
    u8 predictor;
    u8 scale;

    void init(const u8* p) {
        data = p;
        nibble = 2;
        predictor = (*data & 0x70) >> 4;
        scale = *data & 0xF;
        data++;
    }

    s32 next() {
        if (!(nibble & 0xF)) {
            predictor = (*data & 0x70) >> 4;
            scale = *data & 0xF;
            data++;
            nibble += 2;
        }
        s32 sample;
        if (nibble & 1) {
            sample = static_cast<s32>(static_cast<u32>(*data & 0xF) << 28) >> 28;
            data++;
        } else {
            sample = static_cast<s32>(static_cast<u32>(*data & 0xF0) << 24) >> 28;
        }
        nibble++;
        return sample;
    }
};

// One channel of THP audio; `coefs` points at the 8x2 big-endian coefficients.
void decodeChannel(const u8* data, const u8* coefs, s16 yn1, s16 yn2, u32 count, s16* out, s32 step) {
    AdpcmState st;
    st.init(data);
    for (u32 i = 0; i < count; i++) {
        const s32 sample = st.next();
        s64 yn = static_cast<s64>(be16(coefs + st.predictor * 4 + 2)) * yn2;
        yn += static_cast<s64>(be16(coefs + st.predictor * 4)) * yn1;
        yn += static_cast<s64>(sample << st.scale) << 11;
        yn <<= 5;
        yn += 0x8000;
        if (yn > 2147483647LL) {
            yn = 2147483647LL;
        }
        if (yn < -2147483648LL) {
            yn = -2147483648LL;
        }
        *out = static_cast<s16>(yn >> 16);
        out += step;
        yn2 = yn1;
        yn1 = static_cast<s16>(yn >> 16);
    }
}

}  // namespace

extern "C" {

BOOL THPInit(void) { return TRUE; }

s32 THPVideoDecode(void* file, void* tileY, void* tileU, void* tileV, void* work) {
    (void)work;
    if (file == nullptr) {
        return 25;
    }
    if (tileY == nullptr || tileU == nullptr || tileV == nullptr) {
        return 27;
    }
    const s32 result = decodeFrame(static_cast<const u8*>(file), static_cast<u8*>(tileY), static_cast<u8*>(tileU),
                                   static_cast<u8*>(tileV));
    if (result != 0) {
        PORT_WARN("thp", "video frame decode failed ({})", result);
    }
    return result;
}

// Header (all big-endian): u32 offsetNextChannel, u32 sampleSize,
// s16 lCoef[8][2], s16 rCoef[8][2], s16 lYn1, lYn2, rYn1, rYn2; then the
// left channel's data, and the right channel's at offsetNextChannel.
u32 THPAudioDecode(s16* buffer, u8* audio, s32 flag) {
    if (buffer == nullptr || audio == nullptr) {
        return 0;
    }
    const u32 nextChannel = be32(audio);
    const u32 samples = be32(audio + 4);
    const u8* lCoef = audio + 8;
    const u8* rCoef = audio + 8 + 32;
    const s16 lYn1 = be16(audio + 72), lYn2 = be16(audio + 74);
    const s16 rYn1 = be16(audio + 76), rYn2 = be16(audio + 78);
    const u8* left = audio + 80;
    const u8* right = left + nextChannel;

    s16* outRight = buffer;
    s16* outLeft;
    s32 step;
    if (flag == 1) {
        outLeft = buffer + samples;
        step = 1;
    } else {
        outLeft = buffer + 1;
        step = 2;
    }

    if (nextChannel == 0) {
        // Mono: the same samples on both channels.
        decodeChannel(left, lCoef, lYn1, lYn2, samples, outLeft, step);
        decodeChannel(left, lCoef, lYn1, lYn2, samples, outRight, step);
    } else {
        decodeChannel(left, lCoef, lYn1, lYn2, samples, outLeft, step);
        decodeChannel(right, rCoef, rYn1, rYn2, samples, outRight, step);
    }
    return samples;
}

}  // extern "C"
