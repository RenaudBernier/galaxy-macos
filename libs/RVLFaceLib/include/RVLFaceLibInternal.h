#ifndef RVL_FACE_LIBRARY_INTERNAL_H
#define RVL_FACE_LIBRARY_INTERNAL_H
#ifdef __cplusplus
extern "C" {
#endif

// Some functions are declared extern in the RFLi headers but defined `static`.
// MWCC keeps the earlier external linkage (other TUs call them); clang rejects
// the redeclaration, so drop `static` on the host.
#ifdef TARGET_PC
#define RFL_STATIC
#else
#define RFL_STATIC static
#endif

#include "RVLFaceLib.h"
#include "RFLi_Config.h"
#include "RFLi_Controller.h"
#include "RFLi_DataUtility.h"
#include "RFLi_Database.h"
#include "RFLi_DefaultDatabase.h"
#include "RFLi_Format.h"
#include "RFLi_HiddenDatabase.h"
#include "RFLi_MakeRandomFace.h"
#include "RFLi_MakeTex.h"
#include "RFLi_MiddleDatabase.h"
#include "RFLi_Model.h"
#include "RFLi_NANDAccess.h"
#include "RFLi_NANDLoader.h"
#include "RFLi_System.h"
#include "RFLi_Texture.h"
#include "RFLi_Types.h"

#ifndef LENGTHOF
#define LENGTHOF(x) (sizeof(x) / sizeof((x)[0]))
#endif

#ifdef TARGET_PC
// SDK functions RFL calls without a prototype (MWCC accepted implicit
// declarations). Signatures follow the Revolution SDK.
#include <revolution/gx.h>
#include <revolution/mtx.h>
#include <revolution/nand.h>
#include <revolution/wpad.h>

s32 WPADReadFaceData(s32 chan, void* buf, u32 len, u32 addr, WPADCallback callback);
void C_MTXLookAt(Mtx m, const Vec* camPos, const Vec* camUp, const Vec* target);
void GXGetCullMode(GXCullMode* mode);
u32 PSMTXInvXpose(const Mtx src, Mtx xpose);
void NANDSetUserData(NANDCommandBlock* block, void* data);
void* NANDGetUserData(const NANDCommandBlock* block);
s32 NANDPrivateSafeOpenAsync(const char* path, NANDFileInfo* info, u8 accType, void* buf, u32 len, NANDCallback cb,
                             NANDCommandBlock* block);
s32 NANDSafeCloseAsync(NANDFileInfo* info, NANDCallback cb, NANDCommandBlock* block);
s32 NANDGetLengthAsync(NANDFileInfo* info, u32* length, NANDCallback cb, NANDCommandBlock* block);
s32 NANDSeek(NANDFileInfo* info, s32 offset, s32 whence);
// 16-bit wchar_t string functions come from the port runtime; the host's are
// for 32-bit wchar_t (and Apple's <wchar.h> poisons them under -fshort-wchar).
wchar_t* wcsncpy(wchar_t* dst, const wchar_t* src, size_t n);
#endif

#ifdef __cplusplus
}
#endif
#endif
