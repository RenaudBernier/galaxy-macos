#include "JSystem/J3DGraphAnimator/J3DJoint.hpp"
#include "JSystem/J3DGraphLoader/J3DModelLoader.hpp"
#include "JSystem/J3DGraphAnimator/J3DModelData.hpp"
#include "JSystem/J3DGraphBase/J3DMaterial.hpp"
#include "JSystem/J3DGraphLoader/J3DJointFactory.hpp"
#include "JSystem/J3DGraphLoader/J3DMaterialFactory.hpp"
#include "JSystem/J3DGraphLoader/J3DMaterialFactory_v21.hpp"
#include "JSystem/J3DGraphLoader/J3DShapeFactory.hpp"
#include "JSystem/JKernel/JKRHeap.hpp"
#include "JSystem/JSupport/JSupport.hpp"
#include "JSystem/JUtility/JUTNameTab.hpp"
#include <stdint.h>

J3DModelLoader::J3DModelLoader()
    : mpModelData(NULL), mpMaterialTable(NULL), mpShapeBlock(NULL), mpMaterialBlock(NULL), mpModelHierarchy(NULL), field_0x18(0), mEnvelopeSize(0) {
#ifdef TARGET_PC
    mHostOrder = false;
#endif
}

#ifdef TARGET_PC
// Model files are big-endian. Structures read only by the loader/factories use
// BE() fields; arrays that the CPU and the GPU read directly at runtime
// (vertex attributes, envelope/draw tables, shape descriptors) are converted
// to host byte order in place, once per file. The file header padding
// ("SVR3" block at 0x10) records that the conversion was done, since the same
// resource can be loaded several times.
namespace {

const u32 kHostOrderMarker = 0x50434C45;  // 'PCLE'

bool isHostOrder(const void* data) {
    return PortReadBE32((const u8*)data + 0x18) == kHostOrderMarker;
}

void setHostOrder(const void* data) {
    PortWriteBE32((u8*)data + 0x18, kHostOrderMarker);
}

// Extent of the section at `offset` in a block whose sections are given by
// `offsets`: up to the next section, or to the end of the block.
u32 sectionSize(const J3DModelBlock* block, const J3DFileOffset* offsets, int count, u32 offset) {
    u32 end = block->mBlockSize;
    for (int i = 0; i < count; i++) {
        const u32 o = offsets[i];
        if (o > offset && o < end) {
            end = o;
        }
    }
    return end - offset;
}

void swapUnits(void* data, u32 size, u32 unit) {
    switch (unit) {
    case 2:
        PortSwap16Array(data, size / 2);
        break;
    case 3:
        // 24-bit packed values (GX_RGBA6).
        for (u8* p = (u8*)data; p + 3 <= (u8*)data + size; p += 3) {
            const u8 t = p[0];
            p[0] = p[2];
            p[2] = t;
        }
        break;
    case 4:
        PortSwap32Array(data, size / 4);
        break;
    default:
        break;
    }
}

u32 compUnit(GXCompType type) {
    switch (type) {
    case GX_U16:
    case GX_S16:
        return 2;
    case GX_F32:
        return 4;
    default:
        return 1;
    }
}

u32 colorUnit(GXCompType type) {
    switch (type) {
    case GX_RGB565:
    case GX_RGBA4:
        return 2;
    case GX_RGBA6:
        return 3;
    default:
        return 1;  // GX_RGB8 / GX_RGBX8 / GX_RGBA8 are byte arrays
    }
}

}  // namespace
#endif

#ifdef TARGET_PC
#define J3D_FILE_MAGIC(data, i) PortReadBE32((const u8*)(data) + (i) * 4)
#else
#define J3D_FILE_MAGIC(data, i) (reinterpret_cast< const u32* >(data)[i])
#endif

J3DModelData* J3DModelLoaderDataBase::load(void const* i_data, u32 i_flags) {
    if (i_data == NULL) {
        return NULL;
    }
    if (J3D_FILE_MAGIC(i_data, 0) == 'J3D1' && J3D_FILE_MAGIC(i_data, 1) == 'bmd1') {
        return NULL;
    }
    if (J3D_FILE_MAGIC(i_data, 0) == 'J3D2' && J3D_FILE_MAGIC(i_data, 1) == 'bmd2') {
        J3DModelLoader_v21 loader;
        return loader.load(i_data, i_flags);
    }
    if (J3D_FILE_MAGIC(i_data, 0) == 'J3D2' && J3D_FILE_MAGIC(i_data, 1) == 'bmd3') {
        J3DModelLoader_v26 loader;
        return loader.load(i_data, i_flags);
    }
    return NULL;
}

J3DMaterialTable* J3DModelLoaderDataBase::loadMaterialTable(const void* data) {
    if (data == NULL) {
        return NULL;
    }
    if (J3D_FILE_MAGIC(data, 0) == 'J3D2' && J3D_FILE_MAGIC(data, 1) == 'bmt3') {
        J3DModelLoader_v26 loader;
        return loader.loadMaterialTable(data);
    }
    return NULL;
}

J3DModelData* J3DModelLoaderDataBase::loadBinaryDisplayList(const void* data, u32 flags) {
    if (data == NULL) {
        return NULL;
    }
    if (J3D_FILE_MAGIC(data, 0) == 'J3D2' && (J3D_FILE_MAGIC(data, 1) == 'bdl3' || J3D_FILE_MAGIC(data, 1) == 'bdl4')) {
        J3DModelLoader_v26 loader;
        return loader.loadBinaryDisplayList(data, flags);
    }
    return NULL;
}

J3DModelData* J3DModelLoader::load(void const* i_data, u32 i_flags) {
#ifdef TARGET_PC
    mHostOrder = isHostOrder(i_data);
#endif
    JKRGetCurrentHeap()->getTotalFreeSize();
    mpModelData = new J3DModelData();
    mpModelData->clear();
    mpModelData->mpRawData = i_data;
    mpModelData->setModelDataType(0);
    mpMaterialTable = &mpModelData->mMaterialTable;
    J3DModelFileData const* data = (J3DModelFileData*)i_data;
    J3DModelBlock const* block = data->mBlocks;
    for (u32 block_no = 0; block_no < data->mBlockNum; block_no++) {
        switch (block->mBlockType) {
        case 'INF1':
            readInformation((J3DModelInfoBlock*)block, (s32)i_flags);
            break;
        case 'VTX1':
            readVertex((J3DVertexBlock*)block);
            break;
        case 'EVP1':
            readEnvelop((J3DEnvelopeBlock*)block);
            break;
        case 'DRW1':
            readDraw((J3DDrawBlock*)block);
            break;
        case 'JNT1':
            readJoint((J3DJointBlock*)block);
            break;
        case 'MAT3':
            readMaterial((J3DMaterialBlock*)block, (s32)i_flags);
            break;
        case 'MAT2':
            readMaterial_v21((J3DMaterialBlock_v21*)block, (s32)i_flags);
            break;
        case 'SHP1':
            readShape((J3DShapeBlock*)block, (s32)i_flags);
            break;
        case 'TEX1':
            readTexture((J3DTextureBlock*)block);
            break;
        default:
            break;
        }
        block = (J3DModelBlock*)((uintptr_t)block + block->mBlockSize);
    }
    J3DModelHierarchy const* hierarchy = mpModelData->getHierarchy();
    mpModelData->makeHierarchy(NULL, &hierarchy);
    mpModelData->getShapeTable()->sortVcdVatCmd();
    mpModelData->getJointTree().findImportantMtxIndex();
    setupBBoardInfo();
    if (mpModelData->getFlag() & 0x100) {
        for (u16 shape_no = 0; shape_no < mpModelData->getShapeNum(); shape_no++) {
            mpModelData->getShapeNodePointer(shape_no)->onFlag(0x200);
        }
    }
#ifdef TARGET_PC
    setHostOrder(i_data);
#endif
    return mpModelData;
}

J3DMaterialTable* J3DModelLoader::loadMaterialTable(void const* i_data) {
    mpMaterialTable = new J3DMaterialTable();
    mpMaterialTable->clear();
    J3DModelFileData const* data = (J3DModelFileData*)i_data;
    J3DModelBlock const* block = data->mBlocks;
    for (u32 block_no = 0; block_no < data->mBlockNum; block_no++) {
        switch (block->mBlockType) {
        case 'MAT3':
            readMaterialTable((J3DMaterialBlock*)block, 0x51100000);
            break;
        case 'MAT2':
            readMaterialTable_v21((J3DMaterialBlock_v21*)block, 0x51100000);
            break;
        case 'TEX1':
            readTextureTable((J3DTextureBlock*)block);
            break;
        default:
            break;
        }
        block = (J3DModelBlock*)((uintptr_t)block + block->mBlockSize);
    }
    if (mpMaterialTable->mTexture == NULL) {
        mpMaterialTable->mTexture = new J3DTexture(0, NULL);
    }
    return mpMaterialTable;
}

J3DModelData* J3DModelLoader::loadBinaryDisplayList(void const* i_data, u32 i_flags) {
#ifdef TARGET_PC
    mHostOrder = isHostOrder(i_data);
#endif
    mpModelData = new J3DModelData();
    mpModelData->clear();
    mpModelData->mpRawData = i_data;
    mpModelData->setModelDataType(1);
    mpMaterialTable = &mpModelData->mMaterialTable;
    J3DModelFileData const* data = (J3DModelFileData*)i_data;
    J3DModelBlock const* block = data->mBlocks;
    for (u32 block_no = 0; block_no < data->mBlockNum; block_no++) {
        s32 flags;
        switch (block->mBlockType) {
        case 'INF1':
            flags = i_flags;
            readInformation((J3DModelInfoBlock*)block, flags);
            break;
        case 'VTX1':
            readVertex((J3DVertexBlock*)block);
            break;
        case 'EVP1':
            readEnvelop((J3DEnvelopeBlock*)block);
            break;
        case 'DRW1':
            readDraw((J3DDrawBlock*)block);
            break;
        case 'JNT1':
            readJoint((J3DJointBlock*)block);
            break;
        case 'SHP1':
            readShape((J3DShapeBlock*)block, i_flags);
            break;
        case 'TEX1':
            readTexture((J3DTextureBlock*)block);
            break;
        case 'MDL3':
            readMaterialDL((J3DMaterialDLBlock*)block, i_flags);
            modifyMaterial(i_flags);
            break;
        case 'MAT3':
            flags = 0x50100000;
            flags |= (i_flags & 0x3000000);
            mpMaterialBlock = (J3DMaterialBlock*)block;
            if (((u32)i_flags & 0x3000) == 0) {
                readMaterial((J3DMaterialBlock*)block, flags);
            } else if (((u32)i_flags & 0x3000) == 0x2000) {
                readPatchedMaterial((J3DMaterialBlock*)block, flags);
            }
            break;
        default:
            break;
        }
        block = (J3DModelBlock*)((uintptr_t)block + block->mBlockSize);
    }
    J3DModelHierarchy const* hierarchy = mpModelData->getHierarchy();
    mpModelData->makeHierarchy(NULL, &hierarchy);
    mpModelData->getShapeTable()->sortVcdVatCmd();
    mpModelData->getJointTree().findImportantMtxIndex();
    setupBBoardInfo();
    mpModelData->indexToPtr();
#ifdef TARGET_PC
    setHostOrder(i_data);
#endif
    return mpModelData;
}

void J3DModelLoader::setupBBoardInfo() {
    for (u16 i = 0; i < mpModelData->getJointNum(); i++) {
        J3DMaterial* mesh = mpModelData->getJointNodePointer(i)->getMesh();
        if (mesh != NULL) {
            u32 shape_index = mesh->getShape()->getIndex();
            BE(u16)* index_table = JSUConvertOffsetToPtr< BE(u16) >(mpShapeBlock, (uintptr_t)mpShapeBlock->mpIndexTable);
            J3DShapeInitData* shape_init_data = JSUConvertOffsetToPtr< J3DShapeInitData >(mpShapeBlock, (uintptr_t)mpShapeBlock->mpShapeInitData);
            J3DJoint* joint;
            switch (shape_init_data[index_table[shape_index]].mShapeMtxType) {
            case 0:
                joint = mpModelData->getJointNodePointer(i);
                joint->setMtxType(0);
                break;
            case 1:
                joint = mpModelData->getJointNodePointer(i);
                joint->setMtxType(1);
                mpModelData->mbHasBillboard = true;
                break;
            case 2:
                joint = mpModelData->getJointNodePointer(i);
                joint->setMtxType(2);
                mpModelData->mbHasBillboard = true;
                break;
            case 3:
                joint = mpModelData->getJointNodePointer(i);
                joint->setMtxType(0);
                break;
            default:
                break;
            }
        }
    }
}

void J3DModelLoader::readInformation(J3DModelInfoBlock const* i_block, u32 i_flags) {
    mpModelData->mFlags = i_flags | i_block->mFlags;
    mpModelData->getJointTree().setFlag(mpModelData->mFlags);
    J3DMtxCalc* mtx_calc = NULL;
    switch (mpModelData->mFlags & 0xf) {
    case 0:
        mtx_calc = new J3DMtxCalcNoAnm< J3DMtxCalcCalcTransformBasic, J3DMtxCalcJ3DSysInitBasic >();
        break;
    case 1:
        mtx_calc = new J3DMtxCalcNoAnm< J3DMtxCalcCalcTransformSoftimage, J3DMtxCalcJ3DSysInitSoftimage >();
        break;
    case 2:
        mtx_calc = new J3DMtxCalcNoAnm< J3DMtxCalcCalcTransformMaya, J3DMtxCalcJ3DSysInitMaya >();
        break;
    }
    mpModelData->setBasicMtxCalc(mtx_calc);
    mpModelData->getVertexData().mPacketNum = i_block->mPacketNum;
    mpModelData->getVertexData().mVtxNum = i_block->mVtxNum;
    mpModelData->setHierarchy(JSUConvertOffsetToPtr< J3DModelHierarchy >(i_block, i_block->mpHierarchy));
}

inline J3DMtxCalcNoAnmBase::J3DMtxCalcNoAnmBase() {
}

static _GXCompType getFmtType(_GXVtxAttrFmtList* i_fmtList, _GXAttr i_attr) {
    for (; i_fmtList->attr != GX_VA_NULL; i_fmtList++) {
        if (i_fmtList->attr == i_attr) {
            return i_fmtList->type;
        }
    }
    return GX_F32;
}

#ifdef TARGET_PC
static void J3DSwapVertexBlock(J3DVertexBlock const* i_block) {
    const J3DFileOffset* offsets = &i_block->mpVtxAttrFmtList;
    const int offsetNum = 13;

    // Attribute format list: {u32 attr, u32 cnt, u32 type, u8 frac, pad[3]} until GX_VA_NULL.
    u8* fmt = JSUConvertOffsetToPtr< u8 >(i_block, i_block->mpVtxAttrFmtList);
    if (fmt == NULL) {
        return;
    }
    for (u8* e = fmt;; e += 0x10) {
        PortSwap32Array(e, 3);
        if (*(u32*)e == GX_VA_NULL) {
            break;
        }
    }
    const GXVtxAttrFmtList* fmtList = (const GXVtxAttrFmtList*)fmt;
    auto findFmt = [&](GXAttr attr, GXCompType* type) {
        for (const GXVtxAttrFmtList* f = fmtList; f->attr != GX_VA_NULL; f++) {
            if (f->attr == attr) {
                *type = f->type;
                return true;
            }
        }
        return false;
    };
    auto swapArray = [&](const J3DFileOffset& off, GXAttr attr, bool isColor) {
        const u32 o = off;
        if (o == 0) {
            return;
        }
        GXCompType type = GX_F32;
        if (!findFmt(attr, &type) && attr == GX_VA_NBT) {
            findFmt(GX_VA_NRM, &type);
        }
        swapUnits((u8*)i_block + o, sectionSize(i_block, offsets, offsetNum, o), isColor ? colorUnit(type) : compUnit(type));
    };

    swapArray(i_block->mpVtxPosArray, GX_VA_POS, false);
    swapArray(i_block->mpVtxNrmArray, GX_VA_NRM, false);
    swapArray(i_block->mpVtxNBTArray, GX_VA_NBT, false);
    for (int i = 0; i < 2; i++) {
        swapArray(i_block->mpVtxColorArray[i], (GXAttr)(GX_VA_CLR0 + i), true);
    }
    for (int i = 0; i < 8; i++) {
        swapArray(i_block->mpVtxTexCoordArray[i], (GXAttr)(GX_VA_TEX0 + i), false);
    }
}
#endif

void J3DModelLoader::readVertex(J3DVertexBlock const* i_block) {
#ifdef TARGET_PC
    if (!mHostOrder) {
        J3DSwapVertexBlock(i_block);
    }
#endif
    J3DVertexData& vertex_data = mpModelData->getVertexData();
    vertex_data.mVtxAttrFmtList = JSUConvertOffsetToPtr< GXVtxAttrFmtList >(i_block, i_block->mpVtxAttrFmtList);
    vertex_data.mVtxPosArray = JSUConvertOffsetToPtr< void >(i_block, i_block->mpVtxPosArray);
    vertex_data.mVtxNrmArray = JSUConvertOffsetToPtr< void >(i_block, i_block->mpVtxNrmArray);
    vertex_data.mVtxNBTArray = JSUConvertOffsetToPtr< void >(i_block, i_block->mpVtxNBTArray);
    for (int i = 0; i < 2; i++) {
        vertex_data.mVtxColorArray[i] = (GXColor*)JSUConvertOffsetToPtr< void >(i_block, i_block->mpVtxColorArray[i]);
    }
    for (int i = 0; i < 8; i++) {
        vertex_data.mVtxTexCoordArray[i] = JSUConvertOffsetToPtr< void >(i_block, i_block->mpVtxTexCoordArray[i]);
    }
#ifdef TARGET_PC
    {
        const J3DFileOffset* offsets = &i_block->mpVtxAttrFmtList;
        auto arraySize = [&](const J3DFileOffset& o) -> u32 {
            const u32 off = o;
            return off != 0 ? sectionSize(i_block, offsets, 13, off) : 0;
        };
        vertex_data.mVtxArrSize[GX_VA_POS - GX_VA_POS] = arraySize(i_block->mpVtxPosArray);
        vertex_data.mVtxArrSize[GX_VA_NRM - GX_VA_POS] = arraySize(i_block->mpVtxNrmArray);
        for (int i = 0; i < 2; i++) {
            vertex_data.mVtxArrSize[GX_VA_CLR0 - GX_VA_POS + i] = arraySize(i_block->mpVtxColorArray[i]);
        }
        for (int i = 0; i < 8; i++) {
            vertex_data.mVtxArrSize[GX_VA_TEX0 - GX_VA_POS + i] = arraySize(i_block->mpVtxTexCoordArray[i]);
        }
        vertex_data.mVtxArrSize[12] = arraySize(i_block->mpVtxNBTArray);
    }
#endif

    _GXCompType nrm_type = getFmtType(vertex_data.mVtxAttrFmtList, GX_VA_NRM);
    u32 nrm_size = nrm_type == GX_F32 ? 12 : 6;

    void* nrm_end = NULL;
    if (vertex_data.mVtxNBTArray != NULL) {
        nrm_end = vertex_data.mVtxNBTArray;
    } else if (vertex_data.mVtxColorArray[0] != NULL) {
        nrm_end = vertex_data.mVtxColorArray[0];
    } else if (vertex_data.mVtxTexCoordArray[0] != NULL) {
        nrm_end = vertex_data.mVtxTexCoordArray[0];
    }

    if (vertex_data.mVtxNrmArray == NULL) {
        vertex_data.mNrmNum = 0;
    } else if (nrm_end != NULL) {
        vertex_data.mNrmNum = ((uintptr_t)nrm_end - (uintptr_t)vertex_data.mVtxNrmArray) / nrm_size + 1;
    } else {
        vertex_data.mNrmNum = (i_block->mBlockSize - (uintptr_t)i_block->mpVtxNrmArray) / nrm_size + 1;
    }

    void* color0_end = NULL;
    if (vertex_data.mVtxColorArray[1] != NULL) {
        color0_end = vertex_data.mVtxColorArray[1];
    } else if (vertex_data.mVtxTexCoordArray[0] != NULL) {
        color0_end = vertex_data.mVtxTexCoordArray[0];
    }

    if (vertex_data.mVtxColorArray[0] == NULL) {
        vertex_data.mColNum = 0;
    } else if (color0_end != NULL) {
        vertex_data.mColNum = ((uintptr_t)color0_end - (uintptr_t)vertex_data.mVtxColorArray[0]) / 4 + 1;
    } else {
        vertex_data.mColNum = (i_block->mBlockSize - (uintptr_t)i_block->mpVtxColorArray[0]) / 4 + 1;
    }

    if (vertex_data.mVtxTexCoordArray[0] == NULL) {
        vertex_data.mTexCoordNum = 0;
    } else {
        vertex_data.mTexCoordNum = (i_block->mBlockSize - (uintptr_t)i_block->mpVtxTexCoordArray[0]) / 8 + 1;
    }
}

void J3DModelLoader::readEnvelop(J3DEnvelopeBlock const* i_block) {
#ifdef TARGET_PC
    if (!mHostOrder) {
        const J3DFileOffset* offsets = &i_block->mpWEvlpMixMtxNum;
        const u32 index = i_block->mpWEvlpMixIndex;
        const u32 weight = i_block->mpWEvlpMixWeight;
        const u32 invMtx = i_block->mpInvJointMtx;
        if (index != 0) {
            swapUnits((u8*)i_block + index, sectionSize(i_block, offsets, 4, index), 2);
        }
        if (weight != 0) {
            swapUnits((u8*)i_block + weight, sectionSize(i_block, offsets, 4, weight), 4);
        }
        if (invMtx != 0) {
            swapUnits((u8*)i_block + invMtx, sectionSize(i_block, offsets, 4, invMtx), 4);
        }
    }
#endif
    mpModelData->getJointTree().mWEvlpMtxNum = i_block->mWEvlpMtxNum;
    mpModelData->getJointTree().mWEvlpMixMtxNum = JSUConvertOffsetToPtr< u8 >(i_block, i_block->mpWEvlpMixMtxNum);
    mpModelData->getJointTree().mWEvlpMixMtxIndex = JSUConvertOffsetToPtr< u16 >(i_block, i_block->mpWEvlpMixIndex);
    mpModelData->getJointTree().mWEvlpMixWeight = JSUConvertOffsetToPtr< f32 >(i_block, i_block->mpWEvlpMixWeight);
    mpModelData->getJointTree().mInvJointMtx = JSUConvertOffsetToPtr< Mtx >(i_block, i_block->mpInvJointMtx);
}

void J3DModelLoader::readDraw(J3DDrawBlock const* i_block) {
#ifdef TARGET_PC
    if (!mHostOrder) {
        const u32 index = i_block->mpDrawMtxIndex;
        if (index != 0) {
            swapUnits((u8*)i_block + index, sectionSize(i_block, &i_block->mpDrawMtxFlag, 2, index), 2);
        }
    }
#endif
    u16 i;
    J3DModelData* modelData = mpModelData;
    modelData->getJointTree().mDrawMtxData.mEntryNum = i_block->mMtxNum - mpModelData->getJointTree().mWEvlpMtxNum;
    modelData->getJointTree().mDrawMtxData.mDrawMtxFlag = JSUConvertOffsetToPtr< u8 >(i_block, i_block->mpDrawMtxFlag);
    modelData->getJointTree().mDrawMtxData.mDrawMtxIndex = JSUConvertOffsetToPtr< u16 >(i_block, i_block->mpDrawMtxIndex);
    for (i = 0; i < modelData->getJointTree().mDrawMtxData.mEntryNum; i++) {
        if (modelData->getJointTree().mDrawMtxData.mDrawMtxFlag[i] == 1) {
            break;
        }
    }
    modelData->getJointTree().mDrawMtxData.mDrawFullWgtMtxNum = i;
    mpModelData->getJointTree().mWEvlpImportantMtxIdx = new u16[modelData->getJointTree().mDrawMtxData.mEntryNum];
}

void J3DModelLoader::readJoint(J3DJointBlock const* i_block) {
    J3DJointFactory factory(*i_block);
    mpModelData->getJointTree().mJointNum = i_block->mJointNum;
    if (i_block->mpNameTable != NULL) {
        mpModelData->getJointTree().mJointName = new JUTNameTab(JSUConvertOffsetToPtr< ResNTAB >(i_block, i_block->mpNameTable));
    } else {
        mpModelData->getJointTree().mJointName = NULL;
    }
    mpModelData->getJointTree().mJointNodePointer = new J3DJoint*[mpModelData->getJointTree().mJointNum];
    for (u16 i = 0; i < mpModelData->getJointTree().getJointNum(); i++) {
        mpModelData->getJointTree().mJointNodePointer[i] = factory.create(i);
    }
}

void J3DModelLoader_v26::readMaterial(J3DMaterialBlock const* i_block, u32 i_flags) {
    J3DMaterialFactory factory(*i_block);
    mpMaterialTable->mMaterialNum = i_block->mMaterialNum;
    mpMaterialTable->mUniqueMatNum = factory.countUniqueMaterials();
    if (i_block->mpNameTable != NULL) {
        mpMaterialTable->mMaterialName = new JUTNameTab(JSUConvertOffsetToPtr< ResNTAB >(i_block, i_block->mpNameTable));
    } else {
        mpMaterialTable->mMaterialName = NULL;
    }
    mpMaterialTable->mMaterialNodePointer = new J3DMaterial*[mpMaterialTable->mMaterialNum];
    if (i_flags & 0x200000) {
        mpMaterialTable->field_0x10 = new (0x20) J3DMaterial[mpMaterialTable->mUniqueMatNum];
    } else {
        mpMaterialTable->field_0x10 = NULL;
    }
    if (i_flags & 0x200000) {
        for (u16 i = 0; i < mpMaterialTable->mUniqueMatNum; i++) {
            factory.create(&mpMaterialTable->field_0x10[i], J3DMaterialFactory::MATERIAL_TYPE_NORMAL, i, i_flags);
            mpMaterialTable->field_0x10[i].mDiffFlag = (uintptr_t)&mpMaterialTable->field_0x10[i] >> 4;
        }
    }
    for (u16 i = 0; i < mpMaterialTable->mMaterialNum; i++) {
        mpMaterialTable->mMaterialNodePointer[i] = factory.create(NULL, J3DMaterialFactory::MATERIAL_TYPE_NORMAL, i, i_flags);
    }
    if (i_flags & 0x200000) {
        for (u16 i = 0; i < mpMaterialTable->mMaterialNum; i++) {
            mpMaterialTable->mMaterialNodePointer[i]->mDiffFlag = (uintptr_t)&mpMaterialTable->field_0x10[factory.getMaterialID(i)] >> 4;
            mpMaterialTable->mMaterialNodePointer[i]->mpOrigMaterial = &mpMaterialTable->field_0x10[factory.getMaterialID(i)];
        }
    } else {
        for (u16 i = 0; i < mpMaterialTable->mMaterialNum; i++) {
            mpMaterialTable->mMaterialNodePointer[i]->mDiffFlag = ((uintptr_t)mpMaterialTable->mMaterialNodePointer >> 4) + factory.getMaterialID(i);
        }
    }
}

void J3DModelLoader_v21::readMaterial_v21(J3DMaterialBlock_v21 const* i_block, u32 i_flags) {
    J3DMaterialFactory_v21 factory(*i_block);
    mpMaterialTable->mMaterialNum = i_block->mMaterialNum;
    mpMaterialTable->mUniqueMatNum = factory.countUniqueMaterials();
    if (i_block->mpNameTable != NULL) {
        mpMaterialTable->mMaterialName = new JUTNameTab(JSUConvertOffsetToPtr< ResNTAB >(i_block, i_block->mpNameTable));
    } else {
        mpMaterialTable->mMaterialName = NULL;
    }
    mpMaterialTable->mMaterialNodePointer = new J3DMaterial*[mpMaterialTable->mMaterialNum];
    if (i_flags & 0x200000) {
        mpMaterialTable->field_0x10 = new (0x20) J3DMaterial[mpMaterialTable->mUniqueMatNum];
    } else {
        mpMaterialTable->field_0x10 = NULL;
    }
    if (i_flags & 0x200000) {
        for (u16 i = 0; i < mpMaterialTable->mUniqueMatNum; i++) {
            factory.create(&mpMaterialTable->field_0x10[i], i, i_flags);
            mpMaterialTable->field_0x10[i].mDiffFlag = (uintptr_t)&mpMaterialTable->field_0x10[i] >> 4;
        }
    }
    for (u16 i = 0; i < mpMaterialTable->mMaterialNum; i++) {
        mpMaterialTable->mMaterialNodePointer[i] = factory.create(NULL, i, i_flags);
    }
    if (i_flags & 0x200000) {
        for (u16 i = 0; i < mpMaterialTable->mMaterialNum; i++) {
            mpMaterialTable->mMaterialNodePointer[i]->mDiffFlag = (uintptr_t)&mpMaterialTable->field_0x10[factory.getMaterialID(i)] >> 4;
            mpMaterialTable->mMaterialNodePointer[i]->mpOrigMaterial = &mpMaterialTable->field_0x10[factory.getMaterialID(i)];
        }
    } else {
        for (u16 i = 0; i < mpMaterialTable->mMaterialNum; i++) {
            mpMaterialTable->mMaterialNodePointer[i]->mDiffFlag = 0xc0000000;
        }
    }
}

void J3DModelLoader::readShape(J3DShapeBlock const* i_block, u32 i_flags) {
#ifdef TARGET_PC
    if (!mHostOrder) {
        // Vertex descriptor lists ({u32 attr, u32 type} pairs) are handed to
        // GXSetVtxDescv and the matrix table is read by J3DShapeMtxMulti.
        const J3DFileOffset* offsets = &i_block->mpShapeInitData;
        const u32 vtxDesc = i_block->mpVtxDescList;
        const u32 mtxTable = i_block->mpMtxTable;
        if (vtxDesc != 0) {
            swapUnits((u8*)i_block + vtxDesc, sectionSize(i_block, offsets, 8, vtxDesc), 4);
        }
        if (mtxTable != 0) {
            swapUnits((u8*)i_block + mtxTable, sectionSize(i_block, offsets, 8, mtxTable), 2);
        }
    }
#endif
    mpShapeBlock = i_block;
    J3DShapeTable* shape_table = mpModelData->getShapeTable();
    J3DShapeFactory factory(*i_block);
    shape_table->mShapeNum = i_block->mShapeNum;
    if (i_block->mpNameTable != NULL) {
        shape_table->mShapeName = new JUTNameTab(JSUConvertOffsetToPtr< ResNTAB >(i_block, i_block->mpNameTable));
    } else {
        shape_table->mShapeName = NULL;
    }
    shape_table->mShapeNodePointer = new J3DShape*[shape_table->mShapeNum];
    factory.allocVcdVatCmdBuffer(shape_table->mShapeNum);
    J3DModelHierarchy const* hierarchy_entry = mpModelData->getHierarchy();
    GXVtxDescList* vtx_desc_list = NULL;
    for (; hierarchy_entry->mType != 0; hierarchy_entry++) {
        if (hierarchy_entry->mType == 0x12) {
            shape_table->mShapeNodePointer[hierarchy_entry->mValue] = factory.create(hierarchy_entry->mValue, i_flags, vtx_desc_list);
            vtx_desc_list = factory.getVtxDescList(hierarchy_entry->mValue);
        }
    }
}

void J3DModelLoader::readTexture(J3DTextureBlock const* i_block) {
    ResTIMG* texture_res;
    u32 texture_num = i_block->mTextureNum;
    texture_res = JSUConvertOffsetToPtr< ResTIMG >(i_block, i_block->mpTextureRes);
    if (i_block->mpNameTable != NULL) {
        mpMaterialTable->mTextureName = new JUTNameTab(JSUConvertOffsetToPtr< ResNTAB >(i_block, i_block->mpNameTable));
    } else {
        mpMaterialTable->mTextureName = NULL;
    }
    mpMaterialTable->mTexture = new J3DTexture(texture_num, texture_res);
}

void J3DModelLoader_v26::readMaterialTable(J3DMaterialBlock const* i_block, u32 i_flags) {
    J3DMaterialFactory factory(*i_block);
    mpMaterialTable->mMaterialNum = i_block->mMaterialNum;
    if (i_block->mpNameTable != NULL) {
        mpMaterialTable->mMaterialName = new JUTNameTab(JSUConvertOffsetToPtr< ResNTAB >(i_block, i_block->mpNameTable));
    } else {
        mpMaterialTable->mMaterialName = NULL;
    }
    mpMaterialTable->mMaterialNodePointer = new J3DMaterial*[mpMaterialTable->mMaterialNum];
    for (u16 i = 0; i < mpMaterialTable->mMaterialNum; i++) {
        mpMaterialTable->mMaterialNodePointer[i] = factory.create(NULL, J3DMaterialFactory::MATERIAL_TYPE_NORMAL, i, i_flags);
    }
    for (u16 i = 0; i < mpMaterialTable->mMaterialNum; i++) {
        mpMaterialTable->mMaterialNodePointer[i]->mDiffFlag = (uintptr_t)mpMaterialTable->mMaterialNodePointer + factory.getMaterialID(i);
    }
}

void J3DModelLoader_v21::readMaterialTable_v21(J3DMaterialBlock_v21 const* i_block, u32 i_flags) {
    J3DMaterialFactory_v21 factory(*i_block);
    mpMaterialTable->mMaterialNum = i_block->mMaterialNum;
    if (i_block->mpNameTable != NULL) {
        mpMaterialTable->mMaterialName = new JUTNameTab(JSUConvertOffsetToPtr< ResNTAB >(i_block, i_block->mpNameTable));
    } else {
        mpMaterialTable->mMaterialName = NULL;
    }
    mpMaterialTable->mMaterialNodePointer = new J3DMaterial*[mpMaterialTable->mMaterialNum];
    for (u16 i = 0; i < mpMaterialTable->mMaterialNum; i++) {
        mpMaterialTable->mMaterialNodePointer[i] = factory.create(NULL, i, i_flags);
    }
    for (u16 i = 0; i < mpMaterialTable->mMaterialNum; i++) {
        mpMaterialTable->mMaterialNodePointer[i]->mDiffFlag = ((uintptr_t)mpMaterialTable->mMaterialNodePointer >> 4) + factory.getMaterialID(i);
    }
}

void J3DModelLoader::readTextureTable(J3DTextureBlock const* i_block) {
    ResTIMG* texture_res;
    u32 texture_num = i_block->mTextureNum;
    texture_res = JSUConvertOffsetToPtr< ResTIMG >(i_block, i_block->mpTextureRes);
    if (i_block->mpNameTable != NULL) {
        mpMaterialTable->mTextureName = new JUTNameTab(JSUConvertOffsetToPtr< ResNTAB >(i_block, i_block->mpNameTable));
    } else {
        mpMaterialTable->mTextureName = NULL;
    }
    mpMaterialTable->mTexture = new J3DTexture(texture_num, texture_res);
}

void J3DModelLoader::readPatchedMaterial(J3DMaterialBlock const* i_block, u32 i_flags) {
    J3DMaterialFactory factory(*i_block);
    mpMaterialTable->mMaterialNum = i_block->mMaterialNum;
    mpMaterialTable->mUniqueMatNum = factory.countUniqueMaterials();
    if (i_block->mpNameTable != NULL) {
        mpMaterialTable->mMaterialName = new JUTNameTab(JSUConvertOffsetToPtr< ResNTAB >(i_block, i_block->mpNameTable));
    } else {
        mpMaterialTable->mMaterialName = NULL;
    }
    mpMaterialTable->mMaterialNodePointer = new J3DMaterial*[mpMaterialTable->mMaterialNum];
    mpMaterialTable->field_0x10 = NULL;
    for (u16 i = 0; i < mpMaterialTable->mMaterialNum; i++) {
        mpMaterialTable->mMaterialNodePointer[i] = factory.create(NULL, J3DMaterialFactory::MATERIAL_TYPE_PATCHED, i, i_flags);
        mpMaterialTable->mMaterialNodePointer[i]->mDiffFlag = ((uintptr_t)mpMaterialTable->mMaterialNodePointer >> 4) + factory.getMaterialID(i);
    }
}

void J3DModelLoader::readMaterialDL(J3DMaterialDLBlock const* i_block, u32 i_flags) {
    J3DMaterialFactory factory(*i_block);
    if (mpMaterialTable->mMaterialNum == 0) {
        mpMaterialTable->field_0x1c = 1;
        mpMaterialTable->mMaterialNum = i_block->mMaterialNum;
        mpMaterialTable->mUniqueMatNum = i_block->mMaterialNum;
        if (i_block->mpNameTable != NULL) {
            mpMaterialTable->mMaterialName = new JUTNameTab(JSUConvertOffsetToPtr< ResNTAB >(i_block, i_block->mpNameTable));
        } else {
            mpMaterialTable->mMaterialName = NULL;
        }
        mpMaterialTable->mMaterialNodePointer = new J3DMaterial*[mpMaterialTable->mMaterialNum];
        mpMaterialTable->field_0x10 = NULL;
        for (u16 i = 0; i < mpMaterialTable->mMaterialNum; i++) {
            mpMaterialTable->mMaterialNodePointer[i] = factory.create(NULL, J3DMaterialFactory::MATERIAL_TYPE_LOCKED, i, i_flags);
        }
        for (u16 i = 0; i < mpMaterialTable->mMaterialNum; i++) {
            mpMaterialTable->mMaterialNodePointer[i]->mDiffFlag = 0xc0000000;
        }
    } else {
        for (u16 i = 0; i < mpMaterialTable->mMaterialNum; i++) {
            mpMaterialTable->mMaterialNodePointer[i] =
                factory.create(mpMaterialTable->mMaterialNodePointer[i], J3DMaterialFactory::MATERIAL_TYPE_LOCKED, i, i_flags);
        }
    }
}

void J3DModelLoader::modifyMaterial(u32 i_flags) {
    if (i_flags & 0x2000) {
        J3DMaterialFactory factory(*mpMaterialBlock);
        for (u16 i = 0; i < mpMaterialTable->mMaterialNum; i++) {
            factory.modifyPatchedCurrentMtx(mpMaterialTable->mMaterialNodePointer[i], i);
        }
    }
}
