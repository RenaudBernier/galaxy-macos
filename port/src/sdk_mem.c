// RVL MEM heap library (expanded heaps + allocators), used by the HOME menu,
// WPAD and NWC24 glue. The decomp's heap code is portable (its pointer
// arithmetic goes through UIntPtr, uintptr_t on the host) and is compiled
// as-is. Only the intrusive list is reimplemented: the decomp's version
// computes link addresses through u32.
#include "revolution/mem/list.h"

#include <stdint.h>

static MEMLink* GetLink(const MEMList* list, void* obj) {
    return (MEMLink*)((uintptr_t)obj + list->offs);
}

void MEMInitList(MEMList* list, u16 offs) {
    list->head = NULL;
    list->tail = NULL;
    list->num = 0;
    list->offs = offs;
}

void MEMAppendListObject(MEMList* list, void* obj) {
    MEMLink* link = GetLink(list, obj);
    link->next = NULL;
    link->prev = list->tail;
    if (list->head == NULL) {
        list->head = obj;
    } else {
        GetLink(list, list->tail)->next = obj;
    }
    list->tail = obj;
    list->num++;
}

void MEMRemoveListObject(MEMList* list, void* obj) {
    MEMLink* link = GetLink(list, obj);
    if (link->prev == NULL) {
        list->head = link->next;
    } else {
        GetLink(list, link->prev)->next = link->next;
    }
    if (link->next == NULL) {
        list->tail = link->prev;
    } else {
        GetLink(list, link->next)->prev = link->prev;
    }
    link->prev = NULL;
    link->next = NULL;
    list->num--;
}

void* MEMGetNextListObject(MEMList* list, void* obj) {
    if (obj == NULL) {
        return list->head;
    }
    return GetLink(list, obj)->next;
}

// Metrowerks intrinsic used by the heap code (game code gets it from prelude.h).
s32 __abs(s32 x) {
    return x < 0 ? -x : x;
}

// The heap code's globals are game state, like the game's own (prelude.h).
#pragma clang section bss = "__DATA,__game_bss" data = "__DATA,__game_data"
#include "../../src/RVL_SDK/mem/mem_heapCommon.c"
#include "../../src/RVL_SDK/mem/mem_expHeap.c"
#include "../../src/RVL_SDK/mem/mem_allocator.c"
