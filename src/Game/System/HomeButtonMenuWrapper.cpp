#include "Game/System/HomeButtonMenuWrapper.hpp"
#include "Game/Util/FileUtil.hpp"
#include "Game/Util/MemoryUtil.hpp"
#include <JSystem/JKernel/JKRExpHeap.hpp>
#include <revolution/rso.h>

void (*HBMCreateRSO)(const HBMDataInfo*);
void (*HBMInitRSO)(void);
void (*HBMCalcRSO)(const HBMControllerData*);
void (*HBMDrawRSO)(void);
HBMSelectBtnNum (*HBMGetSelectBtnNumRSO)(void);
void (*HBMSetAdjustFlagRSO)(int);
void (*HBMStartBlackOutRSO)(void);

static RSOExportFuncTable exp_tbl[] = {{"HBMCreateRSO", (u32*)&HBMCreateRSO},
                                       {"HBMInitRSO", (u32*)&HBMInitRSO},
                                       {"HBMCalcRSO", (u32*)&HBMCalcRSO},
                                       {"HBMDrawRSO", (u32*)&HBMDrawRSO},
                                       {"HBMGetSelectBtnNumRSO", (u32*)&HBMGetSelectBtnNumRSO},
                                       {"HBMSetAdjustFlagRSO", (u32*)&HBMSetAdjustFlagRSO},
                                       {"HBMStartBlackOutRSO", (u32*)&HBMStartBlackOutRSO}};

typedef void (*ProloguePtr)(BOOL);

void RSO::setupRsoHomeButtonMenu() {
#ifdef TARGET_PC
    // PORT: hw - the HOME Menu ships as a relocatable module of PowerPC code
    // (HomeButtonMenuWrapperRSO.rso), which can't run on the host. The menu is
    // left unavailable; the wrappers below do nothing while it isn't loaded.
    return;
#endif
    u32 i;
    RSOObjectHeader* rsoPtr;
    RSOExportFuncTable* pTbl;
    const RSOObjectHeader* symbolTable = reinterpret_cast< const RSOObjectHeader* >(MR::receiveFile("/ModuleData/product.sel"));
    int jumpCodeSize;
    void* bss;
    void* jumps;

    RSOListInit((void*)symbolTable);
    if (symbolTable != nullptr) {
        jumpCodeSize = RSOGetJumpCodeSize(symbolTable);

        if (jumpCodeSize == 0) {
            jumps = nullptr;
        } else {
            jumps = new (MR::getStationedHeapGDDR3(), 0) char[jumpCodeSize];
            RSOMakeJumpCode(symbolTable, jumps);
        }
        rsoPtr = reinterpret_cast< RSOObjectHeader* >(MR::receiveFile("/ModuleData/HomeButtonMenuWrapperRSO.rso"));
        if (rsoPtr->mBssSize) {
            bss = new (MR::getStationedHeapGDDR3(), 0) char[rsoPtr->mBssSize];
        }

        RSOLinkList((void*)rsoPtr, bss);

        if (rsoPtr != nullptr) {
            RSOLinkJump(rsoPtr, symbolTable, jumps);
            reinterpret_cast< ProloguePtr >(rsoPtr->mProlog)(RSOIsImportSymbolResolvedAll(rsoPtr));
            for (i = 0; i < ARRAY_SIZE(exp_tbl); i++) {
                pTbl = &exp_tbl[i];
                RSOFindExportSymbolAddr(rsoPtr, pTbl->symbol_name);
                // PORT: hw (the HOME Menu RSO contains PowerPC code; the host needs a native replacement)
                *(pTbl->symbol_ptr) = PTR_TO_U32(RSOFindExportSymbolAddr(rsoPtr, pTbl->symbol_name));
            }
        }
    }
}

void RSO::HBMCreate(const HBMDataInfo* pHBInfo) {
#ifdef TARGET_PC
    if (HBMCreateRSO == nullptr) {
        return;
    }
#endif
    (*HBMCreateRSO)(pHBInfo);
}

void RSO::HBMInit() {
#ifdef TARGET_PC
    if (HBMInitRSO == nullptr) {
        return;
    }
#endif
    (*HBMInitRSO)();
}

void RSO::HBMCalc(const HBMControllerData* pController) {
#ifdef TARGET_PC
    if (HBMCalcRSO == nullptr) {
        return;
    }
#endif
    (*HBMCalcRSO)(pController);
}

void RSO::HBMDraw() {
#ifdef TARGET_PC
    if (HBMDrawRSO == nullptr) {
        return;
    }
#endif
    (*HBMDrawRSO)();
}

HBMSelectBtnNum RSO::HBMGetSelectBtnNum() {
#ifdef TARGET_PC
    if (HBMGetSelectBtnNumRSO == nullptr) {
        // Without the menu, report it closed right away (as if with the HOME
        // button), or the game would wait on it forever.
        return HBM_SELECT_HOMEBTN;
    }
#endif
    return (*HBMGetSelectBtnNumRSO)();
}

void RSO::HBMSetAdjustFlag(int flag) {
#ifdef TARGET_PC
    if (HBMSetAdjustFlagRSO == nullptr) {
        return;
    }
#endif
    (*HBMSetAdjustFlagRSO)(flag);
}

void RSO::HBMStartBlackOut() {
#ifdef TARGET_PC
    if (HBMStartBlackOutRSO == nullptr) {
        return;
    }
#endif
    (*HBMStartBlackOutRSO)();
}

void _unresolved(void) {
}
