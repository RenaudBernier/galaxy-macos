// SDK services that don't exist on the host, or that only touch the Wii's
// hardware (caches, MSR, exception contexts). Each reports "not available"
// in the way the game already handles, or does nothing when that's correct.

#include "port/log.hpp"
#include "port/savestate.hpp"

#include <revolution/nwc24.h>
#include <revolution/os.h>
#include <revolution/rso.h>
#include <revolution/thp.h>
#include <revolution/vf.h>
#include <revolution/wenc.h>

#include <cstdint>
#include <cstring>

// The macro forms in os.h would rename the function definitions below.
#undef OSCachedToPhysical

extern "C" {

// ---------------------------------------------------------------------------
// WiiConnect24 (Wii message board): not available on the host.
// ---------------------------------------------------------------------------

NWC24Err NWC24OpenLib(void* work) {
    (void)work;
    return NWC24_ERR_NOT_SUPPORTED;
}
NWC24Err NWC24CloseLib(void) { return NWC24_OK; }
s32 NWC24GetErrorCode(void) { return 0; }
NWC24Err NWC24GetMyUserId(NWC24UserId* id) {
    if (id != nullptr) {
        *id = 0;
    }
    return NWC24_ERR_NOT_SUPPORTED;
}
NWC24Err NWC24InitMsgObj(NWC24MsgObj* msg, NWC24MsgType type) {
    (void)msg;
    (void)type;
    return NWC24_ERR_NOT_SUPPORTED;
}
NWC24Err NWC24CommitMsg(NWC24MsgObj* msg) {
    (void)msg;
    return NWC24_ERR_NOT_SUPPORTED;
}
NWC24Err NWC24GetMsgSize(const NWC24MsgObj* msg, u32* size) {
    (void)msg;
    if (size != nullptr) {
        *size = 0;
    }
    return NWC24_ERR_NOT_SUPPORTED;
}
NWC24Err NWC24SetMsgAltName(NWC24MsgObj*, const u16*, u32) { return NWC24_ERR_NOT_SUPPORTED; }
NWC24Err NWC24SetMsgAttached(NWC24MsgObj*, const char*, u32, NWC24MIMEType) { return NWC24_ERR_NOT_SUPPORTED; }
NWC24Err NWC24SetMsgLedPattern(NWC24MsgObj*, u16) { return NWC24_ERR_NOT_SUPPORTED; }
NWC24Err NWC24SetMsgMBDelay(NWC24MsgObj*, u8) { return NWC24_ERR_NOT_SUPPORTED; }
NWC24Err NWC24SetMsgMBNoReply(NWC24MsgObj*, BOOL) { return NWC24_ERR_NOT_SUPPORTED; }
NWC24Err NWC24SetMsgTag(NWC24MsgObj*, u16) { return NWC24_ERR_NOT_SUPPORTED; }
NWC24Err NWC24SetMsgText(NWC24MsgObj*, const char*, u32, NWC24Charset, NWC24Encoding) {
    return NWC24_ERR_NOT_SUPPORTED;
}
NWC24Err NWC24SetMsgToId(NWC24MsgObj*, NWC24UserId) { return NWC24_ERR_NOT_SUPPORTED; }

// VF (virtual FAT file system used by NWC24).
void VFInitEx(void* heap, u32 size) {
    (void)heap;
    (void)size;
}

// ---------------------------------------------------------------------------
// RSO dynamic modules. The HOME Menu module contains PowerPC code and cannot
// be linked on the host: every step reports failure.
// ---------------------------------------------------------------------------

BOOL RSOListInit(void* list) {
    (void)list;
    return FALSE;
}
BOOL RSOLinkList(void* rso, void* bss) {
    (void)rso;
    (void)bss;
    return FALSE;
}
int RSOGetJumpCodeSize(const RSOObjectHeader* header) {
    (void)header;
    return 0;
}
void RSOMakeJumpCode(const RSOObjectHeader* header, void* buf) {
    (void)header;
    (void)buf;
}
int RSOLinkJump(RSOObjectHeader* rso, const RSOObjectHeader* table, void* jumps) {
    (void)rso;
    (void)table;
    (void)jumps;
    return 0;
}
BOOL RSOIsImportSymbolResolvedAll(const RSOObjectHeader* rso) {
    (void)rso;
    return FALSE;
}
void* RSOFindExportSymbolAddr(const RSOObjectHeader* rso, const char* name) {
    (void)rso;
    PORT_WARN("rso", "RSO symbol '{}' requested; modules are not supported", name ? name : "?");
    return nullptr;
}

// Wii Remote speaker ADPCM encoder: the remote speaker is not emulated.
s32 WENCGetEncodeData(WENCInfo* info, u32 flag, const s16* pcm, s32 samples, u8* out) {
    (void)info;
    (void)flag;
    (void)pcm;
    if (out != nullptr && samples > 0) {
        memset(out, 0, static_cast<size_t>(samples + 1) / 2);
    }
    return samples;
}

// ---------------------------------------------------------------------------
// Caches and CPU state: host memory is coherent, so cache maintenance is a
// no-op except dcbz, which zeroes whole 32-byte lines.
// ---------------------------------------------------------------------------

void DCFlushRange(void*, u32) {}
void DCInvalidateRange(void*, u32) {}
void DCStoreRange(void*, u32) {}
void DCStoreRangeNoSync(void*, u32) {}
void DCZeroRange(void* addr, u32 size) {
    const uintptr_t start = reinterpret_cast<uintptr_t>(addr) & ~uintptr_t(31);
    const uintptr_t end = (reinterpret_cast<uintptr_t>(addr) + size + 31) & ~uintptr_t(31);
    memset(reinterpret_cast<void*>(start), 0, end - start);
}
void LCEnable(void) {}
void LCDisable(void) {}

void PPCHalt(void) {
    PORT_FATAL("os", "PPCHalt");
    __builtin_trap();
}
void PPCSync() { __atomic_thread_fence(__ATOMIC_SEQ_CST); }

namespace {
// MSR with external interrupts (EE), FP available and machine check enabled.
u32 sMsr = 0x0000B032;
}  // namespace
u32 PPCMfmsr(void) { return sMsr; }
void PPCMtmsr(u32 msr) { sMsr = msr; }

void OSProtectRange(u32 channel, void* addr, u32 size, u32 control) {
    (void)channel;
    (void)addr;
    (void)size;
    (void)control;
}

u32 OSCachedToPhysical(const void* caddr) { return PTR_TO_U32(caddr) - OS_BASE_CACHED; }

void OSRegisterVersion(const char* id) { PORT_INFO("os", "{}", id ? id : ""); }

// ---------------------------------------------------------------------------
// Exception contexts (JUTException). Host threads have no PowerPC register
// context; these keep a per-thread dummy so the bookkeeping stays consistent.
// ---------------------------------------------------------------------------

u32 __OSFpscrEnableBits = 0;

namespace {
thread_local OSContext tDummyContext;
thread_local OSContext* tCurrentContext = nullptr;
PORT_SAVED OSErrorHandler sErrorHandlers[32];
}  // namespace

OSContext* OSGetCurrentContext(void) { return tCurrentContext != nullptr ? tCurrentContext : &tDummyContext; }
void OSSetCurrentContext(OSContext* context) { tCurrentContext = context; }
void OSClearContext(OSContext* context) {
    context->mode = 0;
    context->state = 0;
}
void OSFillFPUContext(OSContext* context) { (void)context; }

OSErrorHandler OSSetErrorHandler(OSError error, OSErrorHandler handler) {
    if (error >= 32) {
        return nullptr;
    }
    OSErrorHandler prev = sErrorHandlers[error];
    sErrorHandlers[error] = handler;
    return prev;
}

// Stack pointer as a Wii address (0 when the calling host thread's stack is
// outside the emulated window, e.g. before the game thread starts).
u32 OSGetStackPointer(void) {
    volatile int marker = 0;
    const uintptr_t sp = reinterpret_cast<uintptr_t>(&marker);
    if (sp - gPortWiiBase >= 0x80000000ull) {
        return 0;
    }
    return PTR_TO_U32(reinterpret_cast<const void*>(sp));
}

}  // extern "C"

// Metrowerks' out-of-line memcpy, declared with C++ linkage in revolution/types.h.
void* __memcpy(void* dst, const void* src, int n) { return memcpy(dst, src, static_cast<size_t>(n)); }
