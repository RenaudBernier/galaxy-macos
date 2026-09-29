// Emulation of the JAudio2 DSP ucode's channel mixer.
//
// JAudio2 keeps 64 channel parameter blocks (JASDsp::TChannel, 0x180 bytes)
// in main memory. For every sub-frame of 0x50 output samples (32 kHz), the
// real DSP decodes each active channel's wave data from ARAM (ADPCM4/ADPCM2/
// PCM16/PCM8, or a built-in oscillator), resamples it by the channel pitch,
// filters it, applies the channel's volumes and mixes the result into the two
// planar DAC buffers named by the DsyncFrame mail. JAudio2 then interleaves
// those into the AI DMA buffers.
//
// Channel blocks are written by CPU code, so they are in host byte order;
// wave data comes from the disc and is big-endian.
//
// The approach follows the Dusklight Twilight Princess port's DSP renderer
// (CC0), which emulates the same ucode.

#include "port/audio.hpp"
#include "port/log.hpp"
#include "port/savestate.hpp"

#include <port/wii_addr.h>

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdlib>
#include <cstring>

namespace {

constexpr int kNumChannels = 64;
constexpr int kSubFrame = 0x50;
// JASDriver::MAX_MIXERLEVEL: channel volumes are volume * 0x2EE0.
constexpr float kVolumeScale = 1.0f / 12000.0f;

// Mirror of JASDsp::TChannel with the field meanings known from the ucode's
// use (names from the Twilight Princess decompilation, which shares the
// layout; SMG's decomp leaves most of them unnamed).
struct DspChannel {
    u16 isActive;                 // 0x000
    u16 isFinished;               // 0x002
    u16 pitch;                    // 0x004 (0x1000 = 1:1)
    s16 _006;                     // 0x006
    u16 resetFlag;                // 0x008 (set by playStart)
    u8 _00A[2];                   // 0x00A
    s16 pauseFlag;                // 0x00C
    s16 _00E;                     // 0x00E
    struct Output {
        u16 bus;                  // mixing bus (connect_table)
        u16 targetVolume;
        u16 currentVolume;
        u16 progress;
    } outputs[6];                 // 0x010
    u8 _040[0x10];                // 0x040
    u16 autoMixerPanDolby;        // 0x050 pan << 8 | dolby
    u16 autoMixerFxMix;           // 0x052
    u16 autoMixerInitVolume;      // 0x054
    u16 autoMixerVolume;          // 0x056
    u16 autoMixerBeenSet;         // 0x058
    u8 _05A[6];                   // 0x05A
    s16 _060;                     // 0x060
    u8 _062[2];                   // 0x062
    u16 samplesPerBlock;          // 0x064
    s16 _066;                     // 0x066
    u32 samplePosition;           // 0x068
    u8 _06C[4];                   // 0x06C
    u32 aramStreamPosition;       // 0x070 (read by JASAramStream)
    u32 samplesLeft;              // 0x074
    s16 _078[4];                  // 0x078
    s16 _080[20];                 // 0x080
    s16 _0A8[4];                  // 0x0A8
    u16 _0B0[16];                 // 0x0B0
    u8 _0D0[0x30];                // 0x0D0
    u16 bytesPerBlock;            // 0x100 (oscillator type for osc channels)
    u16 loopFlag;                 // 0x102
    s16 loopLast;                 // 0x104 ADPCM history at the loop start
    s16 loopPenult;               // 0x106
    u16 filterMode;               // 0x108 0x20 = IIR, low bits = FIR length
    u16 forcedStop;               // 0x10A
    s32 _10C;                     // 0x10C
    u32 loopStartSample;          // 0x110
    u32 endSample;                // 0x114
    u32 waveAramAddress;          // 0x118 (0: oscillator)
    s32 sampleCount;              // 0x11C
    s16 firParams[8];             // 0x120
    u8 _130[0x18];                // 0x130
    s16 iirParams[8];             // 0x148
    u8 _158[0x28];                // 0x158
};
static_assert(sizeof(DspChannel) == 0x180, "DSP channel block layout");
static_assert(offsetof(DspChannel, outputs) == 0x010);
static_assert(offsetof(DspChannel, autoMixerPanDolby) == 0x050);
static_assert(offsetof(DspChannel, samplesPerBlock) == 0x064);
static_assert(offsetof(DspChannel, samplePosition) == 0x068);
static_assert(offsetof(DspChannel, aramStreamPosition) == 0x070);
static_assert(offsetof(DspChannel, samplesLeft) == 0x074);
static_assert(offsetof(DspChannel, bytesPerBlock) == 0x100);
static_assert(offsetof(DspChannel, filterMode) == 0x108);
static_assert(offsetof(DspChannel, loopStartSample) == 0x110);
static_assert(offsetof(DspChannel, waveAramAddress) == 0x118);
static_assert(offsetof(DspChannel, firParams) == 0x120);
static_assert(offsetof(DspChannel, iirParams) == 0x148);

constexpr int kDecodeBufSize = 2048;

// Host-side state the ucode keeps in its own memory.
struct ChannelState {
    s16 hist1;  // most recent decoded sample
    s16 hist2;  // the one before
    s16 decodeBuf[kDecodeBufSize];
    int decodeCount;
    float resamplePos;
    s16 resamplePrev;
    u16 oscPhase;
    float biqIn1, biqIn2, biqOut1, biqOut2;
    float firHist[8];
    float prevGain[3];  // left, right, FX send; NaN = not yet rendered
};

enum { kOutL, kOutR, kOutFx };

// --- Reverb for the FX sends (Freeverb-style, public domain algorithm) -----

struct Comb {
    float* buf = nullptr;
    int size = 0;
    int idx = 0;
    float store = 0.0f;
    float process(float in, float feedback, float damp) {
        const float out = buf[idx];
        store = out * (1.0f - damp) + store * damp;
        buf[idx] = in + store * feedback;
        if (++idx >= size) {
            idx = 0;
        }
        return out;
    }
};

struct Allpass {
    float* buf = nullptr;
    int size = 0;
    int idx = 0;
    float process(float in) {
        const float bufOut = buf[idx];
        const float out = bufOut - in;
        buf[idx] = in + bufOut * 0.5f;
        if (++idx >= size) {
            idx = 0;
        }
        return out;
    }
};

struct Reverb {
    // Freeverb's tunings for 44.1 kHz, scaled to 32 kHz.
    static constexpr int kCombTuning[8] = {1116, 1188, 1277, 1356, 1422, 1491, 1557, 1617};
    static constexpr int kAllpassTuning[4] = {556, 441, 341, 225};
    static constexpr int kStereoSpread = 23;
    Comb combL[8], combR[8];
    Allpass apL[4], apR[4];
    float storage[2 * (1617 + 23) * 8 + 2 * (556 + 23) * 4];
    bool ready = false;

    void init() {
        float* p = storage;
        auto take = [&](int n) {
            float* r = p;
            p += n;
            return r;
        };
        const float scale = 32000.0f / 44100.0f;
        for (int i = 0; i < 8; i++) {
            const int l = std::max(1, int(kCombTuning[i] * scale));
            const int r = std::max(1, int((kCombTuning[i] + kStereoSpread) * scale));
            combL[i].buf = take(l);
            combL[i].size = l;
            combR[i].buf = take(r);
            combR[i].size = r;
        }
        for (int i = 0; i < 4; i++) {
            const int l = std::max(1, int(kAllpassTuning[i] * scale));
            const int r = std::max(1, int((kAllpassTuning[i] + kStereoSpread) * scale));
            apL[i].buf = take(l);
            apL[i].size = l;
            apR[i].buf = take(r);
            apR[i].size = r;
        }
        std::memset(storage, 0, sizeof(storage));
        ready = true;
    }

    void process(const float* in, float* outL, float* outR, int n) {
        constexpr float kFeedback = 0.84f;  // room size 0.5
        constexpr float kDamp = 0.28f;      // damping 0.7
        constexpr float kInputGain = 0.015f;
        constexpr float kWet = 3.0f;
        for (int s = 0; s < n; s++) {
            const float x = in[s] * kInputGain;
            float l = 0.0f, r = 0.0f;
            for (int i = 0; i < 8; i++) {
                l += combL[i].process(x, kFeedback, kDamp);
                r += combR[i].process(x, kFeedback, kDamp);
            }
            for (int i = 0; i < 4; i++) {
                l = apL[i].process(l);
                r = apR[i].process(r);
            }
            outL[s] += l * kWet;
            outR[s] += r * kWet;
        }
    }
};

// --- Global DSP state -------------------------------------------------------

// Saved with save states: where the game's tables are and each channel's
// playback state (the reverb only holds echo tails, so it isn't).
PORT_SAVED DspChannel* sChannels = nullptr;
PORT_SAVED u32 sVaram = 0;
PORT_SAVED s16 sAdpcmCoef[16][2] = {
    {0, 0},           {0x0800, 0},      {0, 0x0800},      {0x0400, 0x0400},
    {0x1000, -0x0800}, {0x0E00, -0x0600}, {0x0C00, -0x0400}, {0x1200, -0x0A00},
    {0x1068, -0x08C8}, {0x12C0, -0x08FC}, {0x1400, -0x0C00}, {0x0800, -0x0800},
    {0x0400, -0x0400}, {-0x0400, 0x0400}, {-0x0400, 0},      {-0x0800, 0},
};
PORT_SAVED ChannelState sState[kNumChannels];
Reverb sReverb;
bool sWarnedFormat = false;
PORT_SAVED s16 sEvolvingHarmonic[64];
PORT_SAVED bool sHarmonicInit = false;

u32 blockBytes(const DspChannel& c) {
    if (c.samplesPerBlock == 1) {
        return c.bytesPerBlock == 16 ? 2 : c.bytesPerBlock == 8 ? 1 : 0;
    }
    return c.bytesPerBlock;
}

bool supportedFormat(const DspChannel& c) {
    if (c.samplesPerBlock == 16) {
        return c.bytesPerBlock == 9 || c.bytesPerBlock == 5;
    }
    return c.samplesPerBlock == 1 && (c.bytesPerBlock == 16 || c.bytesPerBlock == 8);
}

const u8* waveData(const DspChannel& c) { return U32_TO_PTR(const u8*, sVaram + c.waveAramAddress); }

s16 clamp16(s32 v) { return static_cast<s16>(v > 0x7FFF ? 0x7FFF : v < -0x8000 ? -0x8000 : v); }

void resetChannel(DspChannel& c, ChannelState& st) {
    c.samplesLeft = c.endSample > c.samplePosition ? c.endSample - c.samplePosition : 0;
    st.hist1 = st.hist2 = 0;
    st.decodeCount = 0;
    st.resamplePos = 0.0f;
    st.resamplePrev = 0;
    st.oscPhase = 0;
    st.biqIn1 = st.biqIn2 = st.biqOut1 = st.biqOut2 = 0.0f;
    std::fill(std::begin(st.firHist), std::end(st.firHist), 0.0f);
    std::fill(std::begin(st.prevGain), std::end(st.prevGain), NAN);
    c.resetFlag = 0;
}

// Decodes `count` samples starting at the block-aligned sample `first`.
void decode(const DspChannel& c, ChannelState& st, u32 first, u32 count, s16* out) {
    const u8* data = waveData(c);
    if (c.samplesPerBlock == 1) {
        if (c.bytesPerBlock == 16) {
            const u8* p = data + first * 2;
            for (u32 i = 0; i < count; i++) {
                out[i] = static_cast<s16>(PortReadBE16(p + i * 2));
            }
        } else {
            const s8* p = reinterpret_cast<const s8*>(data + first);
            for (u32 i = 0; i < count; i++) {
                out[i] = static_cast<s16>(p[i] << 8);
            }
        }
        return;
    }
    const u32 frameBytes = c.bytesPerBlock;
    const u8* frame = data + (first / 16) * frameBytes;
    u32 produced = 0;
    while (produced < count) {
        const u8 header = frame[0];
        const s32 scale = 1 << (header >> 4);
        const s32 coef0 = sAdpcmCoef[header & 0xF][0];
        const s32 coef1 = sAdpcmCoef[header & 0xF][1];
        for (int s = 0; s < 16 && produced < count; s++) {
            s32 nibble;
            if (frameBytes == 9) {
                const u8 b = frame[1 + s / 2];
                nibble = static_cast<s8>(static_cast<u8>((s % 2 == 0 ? b >> 4 : b & 0xF) << 4)) >> 4;
                nibble <<= 11;
            } else {
                // ADPCM2: four 2-bit samples per byte, scaled to the 4-bit range.
                const u8 b = frame[1 + s / 4];
                nibble = static_cast<s8>(static_cast<u8>(((b >> (6 - (s % 4) * 2)) & 3) << 6)) >> 6;
                nibble <<= 13;
            }
            const s16 sample = clamp16((nibble * scale + coef0 * st.hist1 + coef1 * st.hist2) >> 11);
            st.hist2 = st.hist1;
            st.hist1 = sample;
            out[produced++] = sample;
        }
        frame += frameBytes;
    }
}

// Reads one contiguous run of samples from the channel's wave data into out.
int readChunk(DspChannel& c, ChannelState& st, int desired, s16* out, int outCap) {
    static s16 tmp[kDecodeBufSize + 32];
    const u32 spb = c.samplesPerBlock;
    u32 skip = c.samplePosition % spb;
    if (skip != 0) {
        // Resume at the start of the block (loops can start mid-block).
        desired += static_cast<int>(skip);
        c.samplePosition -= skip;
        c.samplesLeft += skip;
    }
    desired = static_cast<int>((static_cast<u32>(desired) + spb - 1) / spb * spb);
    const u32 n = std::min<u32>({c.samplesLeft, static_cast<u32>(desired), static_cast<u32>(kDecodeBufSize + 16)});
    decode(c, st, c.samplePosition, n, tmp);
    c.samplesLeft -= n;
    c.samplePosition += n;
    int outCount = static_cast<int>(n) - static_cast<int>(skip);
    outCount = std::min(outCount, outCap);
    if (outCount > 0) {
        std::memcpy(out, tmp + skip, outCount * sizeof(s16));
    }
    return std::max(outCount, 0);
}

void fillDecodeBuf(DspChannel& c, ChannelState& st, int needed) {
    for (int guard = 0; st.decodeCount < needed && guard < 64; guard++) {
        if (c.samplesLeft == 0) {
            if (!c.loopFlag) {
                break;
            }
            c.samplesLeft = c.endSample > c.loopStartSample ? c.endSample - c.loopStartSample : 0;
            c.samplePosition = c.loopStartSample;
            st.hist2 = c.loopPenult;
            st.hist1 = c.loopLast;
            if (c.samplesLeft == 0) {
                break;
            }
        }
        const int space = kDecodeBufSize - st.decodeCount;
        if (space <= 0) {
            break;
        }
        st.decodeCount += readChunk(c, st, std::min(space, needed - st.decodeCount), st.decodeBuf + st.decodeCount, space);
    }
    const u32 bb = blockBytes(c);
    c.aramStreamPosition = c.waveAramAddress + (c.samplePosition / c.samplesPerBlock) * bb;
}

void generateEvolvingHarmonic() {
    if (!sHarmonicInit) {
        sEvolvingHarmonic[62] = 8191;
        sEvolvingHarmonic[63] = 16383;
        sHarmonicInit = true;
    }
    u32 prev2 = static_cast<u32>(sEvolvingHarmonic[62]);
    u32 prev1 = static_cast<u32>(sEvolvingHarmonic[63]);
    for (int i = 0; i < 64; i += 2) {
        u32 cur = static_cast<u32>(sEvolvingHarmonic[i]);
        sEvolvingHarmonic[i] = static_cast<s16>(static_cast<s32>(prev2 * prev1 - (cur << 16)) >> 16);
        prev2 = prev1;
        prev1 = cur;
        cur = static_cast<u32>(sEvolvingHarmonic[i + 1]);
        sEvolvingHarmonic[i + 1] = static_cast<s16>(static_cast<s32>(2u * (prev2 * prev1 + (cur << 16))) >> 16);
        prev2 = prev1;
        prev1 = cur;
    }
}

void renderOscillator(DspChannel& c, ChannelState& st, float* buf) {
    const u32 step = c.pitch >> 1;
    for (int i = 0; i < kSubFrame; i++) {
        const u16 ph = st.oscPhase;
        float s;
        switch (c.bytesPerBlock) {
        case 0:  // square, 50% duty
            s = ph < 0x8000u ? 0.5f : -0.5f;
            break;
        case 3:  // square, 25% duty
            s = ph < 0x4000u ? 0.5f : -0.5f;
            break;
        case 1:   // saw
        case 12:  // evolving ramp
            s = static_cast<s16>(ph) / 32768.0f;
            break;
        case 4:  // triangle
            s = 0.5f - std::fabs(static_cast<s16>(ph) / 32768.0f);
            break;
        case 7:
        case 10:  // sine
            s = std::sin(ph * (2.0f * static_cast<float>(M_PI) / 65536.0f)) * 0.5f;
            break;
        case 11:  // evolving harmonic
            s = sEvolvingHarmonic[ph >> 10] / 32768.0f;
            break;
        default:
            s = 0.0f;
            break;
        }
        buf[i] = s;
        st.oscPhase = static_cast<u16>(ph + step);
    }
}

void renderWave(DspChannel& c, ChannelState& st, float* buf) {
    const float step = c.pitch / 4096.0f;
    const int needed = static_cast<int>(st.resamplePos + kSubFrame * step) + 2;
    fillDecodeBuf(c, st, needed);
    if (st.decodeCount < needed && c.samplesLeft == 0 && !c.loopFlag) {
        c.isFinished = 1;
    }

    // Linear resampling.
    float pos = st.resamplePos;
    s16 prev = st.resamplePrev;
    s16 next = st.decodeCount > 0 ? st.decodeBuf[0] : prev;
    int src = 0;
    for (int i = 0; i < kSubFrame; i++) {
        buf[i] = (prev + pos * (next - prev)) / 32768.0f;
        pos += step;
        while (pos >= 1.0f) {
            pos -= 1.0f;
            prev = next;
            src++;
            next = src < st.decodeCount ? st.decodeBuf[src] : prev;
        }
    }
    st.resamplePos = pos;
    st.resamplePrev = prev;
    const int remaining = st.decodeCount - src;
    if (remaining > 0) {
        std::memmove(st.decodeBuf, st.decodeBuf + src, remaining * sizeof(s16));
    }
    st.decodeCount = std::max(0, remaining);
}

void applyFilters(DspChannel& c, ChannelState& st, float* buf) {
    const int firTaps = std::min(c.filterMode & 0x1F, 8);
    if (firTaps > 0) {
        float coef[8];
        for (int k = 0; k < 8; k++) {
            coef[k] = c.firParams[k] / 32768.0f;
        }
        for (int i = 0; i < kSubFrame; i++) {
            for (int k = 7; k > 0; k--) {
                st.firHist[k] = st.firHist[k - 1];
            }
            st.firHist[0] = buf[i];
            float acc = 0.0f;
            for (int k = 0; k < firTaps; k++) {
                acc += coef[k] * st.firHist[k];
            }
            buf[i] = acc;
        }
    }
    if (c.filterMode & 0x20) {
        // Biquad: out[n] = b1*in[n-1] + b2*in[n-2] + a1*out[n-1] + a2*out[n-2]
        const float b1 = c.iirParams[0] / 32768.0f;
        const float b2 = c.iirParams[1] / 32768.0f;
        const float a1 = c.iirParams[2] / 32768.0f;
        const float a2 = c.iirParams[3] / 32768.0f;
        for (int i = 0; i < kSubFrame; i++) {
            const float out = std::clamp(b1 * st.biqIn1 + b2 * st.biqIn2 + a1 * st.biqOut1 + a2 * st.biqOut2, -1.0f, 1.0f);
            st.biqIn2 = st.biqIn1;
            st.biqIn1 = buf[i];
            st.biqOut2 = st.biqOut1;
            st.biqOut1 = out;
            buf[i] = out;
        }
    }
}

struct Gains {
    float target[3] = {0.0f, 0.0f, 0.0f};
    float init[3] = {0.0f, 0.0f, 0.0f};
};

void addGain(Gains& g, int out, float target, float init) {
    g.target[out] += target;
    g.init[out] += init;
}

Gains channelGains(const DspChannel& c) {
    Gains g;
    if (c.autoMixerBeenSet) {
        const float vol = c.autoMixerVolume * kVolumeScale;
        const float init = c.autoMixerInitVolume * kVolumeScale;
        const float right = std::clamp((c.autoMixerPanDolby >> 8) / 127.0f, 0.0f, 1.0f);
        const float left = 1.0f - right;
        addGain(g, kOutL, left * vol, left * init);
        addGain(g, kOutR, right * vol, right * init);
        const float fx = (c.autoMixerFxMix >> 8) / 127.0f;
        addGain(g, kOutFx, fx * vol, fx * init);
        return g;
    }
    for (const auto& o : c.outputs) {
        const float target = o.targetVolume * kVolumeScale;
        const float init = o.currentVolume * kVolumeScale;
        switch (o.bus) {
        case 0x0000:
            break;
        case 0x0D00:  // main left
            addGain(g, kOutL, target, init);
            break;
        case 0x0D60:  // main right
            addGain(g, kOutR, target, init);
            break;
        case 0x0DC0:
        case 0x0DC8:  // centre
            addGain(g, kOutL, target * 0.7071f, init * 0.7071f);
            addGain(g, kOutR, target * 0.7071f, init * 0.7071f);
            break;
        case 0x0E20:  // surround left, folded into the front
            addGain(g, kOutL, target * 0.7071f, init * 0.7071f);
            break;
        case 0x0E80:
        case 0x0E88:  // surround right
            addGain(g, kOutR, target * 0.7071f, init * 0.7071f);
            break;
        default:  // FX/aux buses
            addGain(g, kOutFx, target, init);
            break;
        }
    }
    return g;
}

void mixInto(float* dst, const float* src, float from, float to) {
    if (from == 0.0f && to == 0.0f) {
        return;
    }
    const float step = (to - from) / kSubFrame;
    for (int i = 0; i < kSubFrame; i++) {
        dst[i] += src[i] * (from + step * i);
    }
}

}  // namespace

namespace port::audio {

void dspSetupTable(u32 channelBuf, u32 resampleFilter, u32 adpcmFilter, u32 fxBuf) {
    (void)resampleFilter;
    (void)fxBuf;
    sChannels = U32_TO_PTR(DspChannel*, channelBuf);
    if (adpcmFilter >= 0x80000000u) {
        // 16 big-endian coefficient pairs (JASDsp::DSPADPCM_FILTER).
        const u8* p = U32_TO_PTR(const u8*, adpcmFilter);
        for (int i = 0; i < 16; i++) {
            sAdpcmCoef[i][0] = static_cast<s16>(PortReadBE16(p + i * 4));
            sAdpcmCoef[i][1] = static_cast<s16>(PortReadBE16(p + i * 4 + 2));
        }
    }
    for (auto& st : sState) {
        std::memset(&st, 0, sizeof(st));
        std::fill(std::begin(st.prevGain), std::end(st.prevGain), NAN);
    }
    if (!sReverb.ready) {
        sReverb.init();
    }
    PORT_INFO("audio", "DSP channel table at 0x{:08X}", channelBuf);
}

void dspSetVaram(u32 varam) { sVaram = varam; }

void dspRenderSubFrame(u32 dacL, u32 dacR, u32 index, u32 mixerLevel) {
    float left[kSubFrame] = {};
    float right[kSubFrame] = {};
    float fx[kSubFrame] = {};
    bool anyFx = false;

    if (sChannels != nullptr) {
        generateEvolvingHarmonic();
        for (int i = 0; i < kNumChannels; i++) {
            DspChannel& c = sChannels[i];
            ChannelState& st = sState[i];
            if (!c.isActive || c.pauseFlag) {
                continue;
            }
            if (c.resetFlag) {
                resetChannel(c, st);
            }
            if (c.forcedStop) {
                c.isFinished = 1;
                continue;
            }
            if (c.isFinished) {
                continue;
            }

            float mono[kSubFrame];
            if (c.waveAramAddress == 0) {
                renderOscillator(c, st, mono);
            } else if (supportedFormat(c)) {
                renderWave(c, st, mono);
            } else {
                if (!sWarnedFormat) {
                    sWarnedFormat = true;
                    PORT_WARN("audio", "unsupported wave format ({} samples / {} bytes per block)", c.samplesPerBlock,
                              c.bytesPerBlock);
                }
                c.isFinished = 1;
                continue;
            }
            applyFilters(c, st, mono);

            const Gains g = channelGains(c);
            float* outs[3] = {left, right, fx};
            for (int o = 0; o < 3; o++) {
                if (std::isnan(st.prevGain[o])) {
                    st.prevGain[o] = g.init[o];
                }
                mixInto(outs[o], mono, st.prevGain[o], g.target[o]);
                if (o == kOutFx && (st.prevGain[o] != 0.0f || g.target[o] != 0.0f)) {
                    anyFx = true;
                }
                st.prevGain[o] = g.target[o];
            }
        }
    }

    if (sReverb.ready) {
        // Keep running while the tail decays.
        static int tailFrames = 0;
        if (anyFx) {
            tailFrames = 32000 * 4 / kSubFrame;
        }
        if (tailFrames > 0) {
            tailFrames--;
            sReverb.process(fx, left, right, kSubFrame);
        }
    }

    const float gain = (mixerLevel & 0xFFFF) / static_cast<float>(0x4000) * 32767.0f;
    s16* outL = U32_TO_PTR(s16*, dacL) + index * kSubFrame;
    s16* outR = U32_TO_PTR(s16*, dacR) + index * kSubFrame;
    for (int i = 0; i < kSubFrame; i++) {
        outL[i] = clamp16(static_cast<s32>(std::lrint(left[i] * gain)));
        outR[i] = clamp16(static_cast<s32>(std::lrint(right[i] * gain)));
    }
}

}  // namespace port::audio
