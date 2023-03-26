#ifndef SHARE_GC_RTGC_IMPL_ANCHOR_LIST_HPP
#define SHARE_GC_RTGC_IMPL_ANCHOR_LIST_HPP
#include <memory.h>
#include "utilities/globalDefinitions_gcc.hpp"
#include "utilities/debug.hpp"
#include "gc/rtgc/rtgcDebug.hpp"


namespace RTGC {

static const uint32_t RTGC_NO_HASHCODE = 0x80000000;
static const uint32_t ANCHOR_LIST_INDEX_MASK = RTGC_NO_HASHCODE - 1;

static const bool  MARK_ALIVE_CHUNK = true;

class ReferrerList {
    friend class ReverseIterator;    
public:
    static const int MAX_COUNT_IN_CHUNK = 7;
    struct Chunk {
        ShortOOP _items[MAX_COUNT_IN_CHUNK];
        int32_t _last_item_offset;
        
        Chunk*  getNextChunk()  { 
            rt_assert_f((_last_item_offset % 8) != 0, "wrong offset %d", _last_item_offset);  
            return (Chunk*)(&_last_item_offset + _last_item_offset); 
        }
        
        bool isAlive()       { 
            return !MARK_ALIVE_CHUNK || _last_item_offset != 0; 
        }
        
        void setDestroyed()  { 
            rt_assert_f(MARK_ALIVE_CHUNK && isAlive(), "not alive chunk %p", this); 
            _last_item_offset = 0; 
        }
    };
    static const int CHUNK_MASK = (sizeof(Chunk) - 1);

    Chunk _head;

public:
    void initEmpty() {
        _head._last_item_offset = -(MAX_COUNT_IN_CHUNK + 1);
    }

    void init(ShortOOP first) {
        _head._items[0] = first;
        if (MARK_ALIVE_CHUNK) {
            // NextFree 주소값 clear.
            *(int32_t*)&_head._items[1] = 0;
        }
        _head._last_item_offset = -(MAX_COUNT_IN_CHUNK);
    }

    void init(ShortOOP first, GCObject* second) {
        _head._items[0] = first;
        _head._items[1] = second;
        _head._last_item_offset = -(MAX_COUNT_IN_CHUNK - 1);
    }

    const ShortOOP* firstItemPtr() {
        return &_head._items[0];
    }

    const ShortOOP* lastItemPtr() {
        return &_head._items[MAX_COUNT_IN_CHUNK] + _head._last_item_offset;
    }

    bool empty() {
        return _head._last_item_offset + (MAX_COUNT_IN_CHUNK+1) == 0;
    }

    bool hasSingleItem() {
        return approximated_item_count() == 1;
    }

    bool isTooSmall() {
        return approximated_item_count() <= 1;
    }

    bool hasMultiChunk() {
        return approximated_item_count() > MAX_COUNT_IN_CHUNK;
    }

    ShortOOP front() {
        rt_assert(!empty());
        return _head._items[0];
    }

    void replaceFirst(ShortOOP first);

    void push_back(ShortOOP item) {
        add(item);
    }

    void add(ShortOOP item);

    bool contains(ShortOOP item) {
        return getItemPtr(item) != NULL;
    }

    // returns removed item pointer
    const void* replace(ShortOOP old_p, ShortOOP new_p);

    // returns removed item pointer (the memory may not accessable);
    const void* remove(ShortOOP item);

    // returns lowerest removed item pointer (the memory may not accessable);
    const void* removeMatchedItems(ShortOOP item);

    void removeDirtyItems(ShortOOP dirtyStartMark);

    const ShortOOP* getItemPtr(ShortOOP item);

    int approximated_item_count() {
        int size = _head._last_item_offset + (MAX_COUNT_IN_CHUNK+1);
        if (size < 0 || size > MAX_COUNT_IN_CHUNK) {
            size = (MAX_COUNT_IN_CHUNK * 2) - 1;
        }
        return size;
    }

    const ShortOOP* getNextChuckOffsetPtr() {
        return &_head._items[MAX_COUNT_IN_CHUNK];
    }    


    static bool isEndOfChunk(const ShortOOP* ptr) {
        return ((uintptr_t)ptr & CHUNK_MASK) == (sizeof(Chunk) - sizeof(ShortOOP));
    }

    static void initialize();

    static void clearTemporalChunkPool();
    
    static ReferrerList* allocate(bool isTenured);

    static int getIndex(ReferrerList* referrers) {
        rt_assert(g_chunkPool.contains(referrers) || g_tempChunkPool.contains(referrers));
        return (&referrers->_head - g_chunkPool.getPointer(0)) | RTGC_NO_HASHCODE;
    }

    static ReferrerList* getPointer(uint32_t idx) {
        return (ReferrerList*)g_chunkPool.getPointer(0) + (idx & ~RTGC_NO_HASHCODE);
    }

    static void deleteSingleChunkList(ReferrerList* list) {
        rt_assert(!list->hasMultiChunk());
        dealloc_chunk(&list->_head);
    }

    static void delete_(ReferrerList* list);

    static int getAllocatedItemCount() {
        return g_chunkPool.getAllocatedItemCount();
    }

    static Chunk* getContainingChunck(const ShortOOP* pItem) {
        return (Chunk*)((uintptr_t)pItem & ~CHUNK_MASK);
    }

    void cut_tail_end(ShortOOP* copy_to);

private:
    typedef MemoryPool<Chunk, 80*1024*1024, 1, -1> ChunkPool;
    
    static ChunkPool g_chunkPool;
    static ChunkPool g_tempChunkPool;

    static void dealloc_chunk(Chunk* chunk);

    const ShortOOP* extend_tail(Chunk* last_chunk);

    void set_last_item_ptr(const ShortOOP* pLast) {
        _head._last_item_offset = pLast - &_head._items[MAX_COUNT_IN_CHUNK];
        rt_assert(_head._last_item_offset != 0);
        rt_assert(_head.isAlive());
    }

    const ShortOOP* getFirstDirtyItemPtr();

public:
    template <typename T>
    static void iterateAllAnchors(T* iter) {
        Chunk* chunk = g_chunkPool.getPointer(1); // 1 = indexStart;
        Chunk* endOfChunk = chunk + g_chunkPool.size();
        int size_of_chunks = (char*)endOfChunk - (char*)chunk;
        for (; chunk < endOfChunk; chunk++) {
            if (chunk->isAlive()) {
                int cntItem = ReferrerList::MAX_COUNT_IN_CHUNK;
                ShortOOP* ppAnchor = chunk->_items;
                for (; --cntItem >= 0; ppAnchor ++) {
                    iter->do_oop(ppAnchor);
                }
            }
        }
    }    
};

template <bool trace_forward >
class NodeIterator {
protected:    
    const ShortOOP* _ptr;
    const ShortOOP* _end;

public:
    void initEmpty() {
        _ptr = _end = NULL;
    }

    void initSingleIterator(const ShortOOP* temp) {
        _ptr = temp;
        _end = temp + 1;
    }

    bool hasNext() {
        return _ptr != (trace_forward ? NULL : _end);
    }

    GCObject* next() {
        return *next_ptr();
    }

    const ShortOOP* next_ptr() {
        rt_assert(hasNext());
        const ShortOOP* oop = _ptr ++;
        rt_assert(oop->getOffset() != 0);
        if (_ptr != _end) {
            if (ReferrerList::isEndOfChunk(_ptr)) {
                _ptr = _ptr + *(int32_t*)_ptr;
            }
            if (trace_forward && _ptr == _end) {
                _ptr = NULL;
            } 
        } else if (trace_forward ) {
            _ptr = NULL;
        }
        return oop;
    }
};

class ReverseIterator : public NodeIterator<false> {
public:     
    ReverseIterator(ReferrerList* list) {
        rt_assert(!list->isTooSmall());
        if (!list->hasMultiChunk()) {
            _ptr = list->firstItemPtr();
            _end = list->lastItemPtr() + 1;
        } else {
            _ptr = list->lastItemPtr();
            _end = list->getNextChuckOffsetPtr();
        }
        rt_assert(_ptr != _end);
    }     
};

class AnchorIterator : public NodeIterator<true> {
    GCObject* _current;
public:    
    AnchorIterator(GCObject* obj) {
        initialize(obj);
    }

    void initialize(GCObject* obj);

    void initIterator(ReferrerList* list) {
        if (!list->hasMultiChunk()) {
            if (list->empty()) {
                _ptr = _end = NULL;
            } else {
                _ptr = list->firstItemPtr();
                _end = list->lastItemPtr() + 1;
            }
        } else {
            _ptr = list->firstItemPtr();
            _end = _ptr;
        }
    } 

    GCObject* peekPrev() {
        return _current;
    }

    GCObject* next() {
        this->_current = *NodeIterator<true>::next_ptr();
        return this->_current;
    }
};

}
#endif