// Audio: DSP channel mixer (audio_dsp.cpp) and host output (audio_output.cpp).
#pragma once

#include <revolution/types.h>

namespace port::audio {

// --- DSP (emulates the JAudio2 DSP ucode's channel mixing) -----------------

// DsetupTable (mail 0x81): Wii addresses of the channel parameter blocks, the
// resampling filter table, the ADPCM coefficient table and the FX buffers.
void dspSetupTable(u32 channelBuf, u32 resampleFilter, u32 adpcmFilter, u32 fxBuf);
// DsetVARAM (mail 0x8E): Wii address that ARAM addresses are relative to.
void dspSetVaram(u32 varam);
// Renders sub-frame `index` (0x50 samples) of a DsyncFrame into the planar DAC
// buffers at Wii addresses `dacL`/`dacR`. `mixerLevel` is 0x4000 for unity.
// Must run where the CPU can't touch the channel blocks (interrupt context).
void dspRenderSubFrame(u32 dacL, u32 dacR, u32 index, u32 mixerLevel);

// --- Output ---------------------------------------------------------------

// Queues one AI DMA block (interleaved right/left s16, as the Wii's AI reads
// it) for playback at `rateHz`.
void outputPushDma(const s16* rightLeft, u32 frames, double rateHz);
// Seconds of audio queued for the device, or a negative value when output is
// disabled.
double outputQueuedSeconds();

}  // namespace port::audio
