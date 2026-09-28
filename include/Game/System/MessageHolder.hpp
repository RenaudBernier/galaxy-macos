#pragma once

#include <revolution/types.h>

class JMapInfo;
class TalkMessageInfo;
class TalkNode;

// BMG message file blocks; big-endian file data.
struct MessageInfoBlock {
    BE(u32) mMagic;
    BE(u32) mBlockSize;
    BE(u16) mItemCount;
    BE(u16) mItemSize;
    BE(u32) _C;
};

struct MessageDataBlock {
    BE(u32) mMagic;
    BE(u32) mBlockSize;
};

struct MessageFlowBlock {
    BE(u32) mMagic;
    BE(u32) mBlockSize;
    BE(u16) mNodeCount;
    BE(u16) _A;
    BE(u32) _C;
};

struct MessageFLI1Block {
    BE(u32) mMagic;
    BE(u32) mBlockSize;
};

class MessageData {
public:
    MessageData(const char*);

    bool getMessageDirect(TalkMessageInfo*, const char*) const;
    bool getMessage(TalkMessageInfo*, u16, u16) const;
    TalkNode* findNode(const char*) const;
    TalkNode* getNode(u32) const;
    TalkNode* getBranchNode(u32) const;
    bool isValidBranchNode(u32) const;
    u8* getMessageInfoTool(int) const;
    s32 findMessageIndex(const char*) const;

    JMapInfo* mIDTable;            // 0x0
    MessageInfoBlock* mInfoBlock;  // 0x4
    MessageDataBlock* mDataBlock;  // 0x8
    u32 _C;
    MessageFlowBlock* mFlowBlock;  // 0x10
    BE(u16)* _14;
    u8* _18;
    MessageFLI1Block* mFLI1Block;  // 0x1C
};

class MessageHolder {
public:
    MessageHolder();

    void initSceneData();
    void destroySceneData();
    void initSystemData();
    void initGameData();

    /* 0x00 */ MessageData* mSystemMessageData;
    /* 0x04 */ MessageData* mGameMessageData;
    /* 0x08 */ MessageData* mSceneMessageData;
};

class MessageSystem {
public:
    class Node {};

    struct FlowNodeBranch {};

    struct FlowNodeEvent {
        /* 0x00 */ u8 mFlowType;
        /* 0x01 */ u8 mEventType;
        /* 0x02 */ BE(u16) mBranchID;
        /* 0x04 */ BE(u32) mArg;
    };

    static bool getSystemMessageDirect(TalkMessageInfo*, const char*);
    static bool getGameMessageDirect(TalkMessageInfo*, const char*);
    static bool getLayoutMessageDirect(TalkMessageInfo*, const char*);
    static MessageData* getSceneMessageData();
};
