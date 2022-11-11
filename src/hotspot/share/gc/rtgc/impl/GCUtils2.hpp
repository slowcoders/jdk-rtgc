#include <memory.h>
#include "utilities/globalDefinitions_gcc.hpp"
#include "utilities/debug.hpp"
#include "gc/rtgc/rtgcDebug.hpp"


namespace RTGC {

class ReferrerList {
    friend class ReverseIterator;    

    static const int MAX_COUNT_IN_CHUNK = 7;
    struct Chunk {
        ShortOOP _items[MAX_COUNT_IN_CHUNK];
        int32_t _last_item_offset;
    };
    static const int CHUNK_MASK = (sizeof(Chunk) - 1);

    Chunk _head;

public:
    void init() {
        _head._last_item_offset = -MAX_COUNT_IN_CHUNK;
    }

    void init(ShortOOP first, GCObject* second) {
        _head._items[0] = first;
        _head._items[1] = second;
        _head._last_item_offset = -(MAX_COUNT_IN_CHUNK - 1);
    }

    ShortOOP* firstItemPtr() {
        return &_head._items[0];
    }

    ShortOOP* lastItemPtr() {
        return &_head._items[MAX_COUNT_IN_CHUNK] + _head._last_item_offset;
    }

    bool empty() {
        return _head._last_item_offset == -(MAX_COUNT_IN_CHUNK+1);
    }

    bool hasSingleItem() {
        return _head._last_item_offset + MAX_COUNT_IN_CHUNK == 0;
    }

    bool isTooSmall() {
        uint32_t offset = (uint32_t)_head._last_item_offset + MAX_COUNT_IN_CHUNK + 1;
        return offset < 2;
    }

    bool hasMultiChunk() {
        return (uint32_t)_head._last_item_offset < (uint32_t)(-MAX_COUNT_IN_CHUNK);
    }

    ShortOOP front() {
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

    // returns removed item pointer (the memory may not accessable);
    const void* remove(ShortOOP item);

    const void* removeMatchedItems(ShortOOP item);

    ShortOOP* getItemPtr(ShortOOP item);

    int approximated_item_count() {
        int count = lastItemPtr() - firstItemPtr();
        if (count < 0) {
            count = -count;
        }
        return count;
    }

    ShortOOP* getLastItemOffsetPtr() {
        return &_head._items[MAX_COUNT_IN_CHUNK];
    }    

    static void validateChunktemPtr(ShortOOP*& ptr) {
        if (((uintptr_t)ptr & CHUNK_MASK) == sizeof(Chunk) - sizeof(int32_t)) {
            ptr = ptr + *(int32_t*)ptr;
        }
    }

    static void initialize() {
        g_chunkPool.initialize();
    }

    static ReferrerList* allocate() {
        return (ReferrerList*)g_chunkPool.allocate();
    }

    static int getIndex(ReferrerList* referrers) {
        return g_chunkPool.getIndex(&referrers->_head);
    }

    static ReferrerList* getPointer(uint32_t idx) {
        return (ReferrerList*)g_chunkPool.getPointer(idx);
    }

    static void delete_(ReferrerList* referrers) {
        g_chunkPool.delete_(&referrers->_head);
    }

    static int getAllocatedItemCount() {
        return g_chunkPool.getAllocatedItemCount();
    }

private:
    typedef MemoryPool<Chunk, 64*1024*1024, 1, -1> ChunkPool;
    
    static  ChunkPool g_chunkPool;

    ShortOOP* extend_tail(Chunk* last_chunk);

    Chunk* dealloc_chunk(Chunk* chunk);

    void set_last_item_ptr(ShortOOP* pLast) {
        _head._last_item_offset = pLast - &_head._items[MAX_COUNT_IN_CHUNK];
    }

    void cut_tail_end(ShortOOP* copy_to);
};

template <bool trace_reverse>
class NodeIterator {
protected:    
    ShortOOP* _ptr;
    ShortOOP* _end;

public:
    void initEmpty() {
        _ptr = _end = NULL;
    }

    void initIterator(ReferrerList* vector) {
        if (!vector->hasMultiChunk()) {
            if (vector->empty()) {
                _ptr = _end = NULL;
            } else {
                _ptr = vector->firstItemPtr();
                _end = vector->lastItemPtr() + 1;
            }
        } else if (trace_reverse) {
            _ptr = vector->lastItemPtr();
            _end = vector->getLastItemOffsetPtr();
            postcond(_ptr != _end);
        } else {
            _ptr = vector->firstItemPtr();
            _end = _ptr;
        }
    } 

    void initSingleIterator(ShortOOP* temp) {
        _ptr = temp;
        _end = temp + 1;
    }

    bool hasNext() {
        return _ptr != (trace_reverse ? _end : NULL);
    }

    ShortOOP& next() {
        precond(hasNext());
        ShortOOP& oop = *_ptr ++;
        precond((GCObject*)oop != NULL);
        if (_ptr != _end) {
            ReferrerList::validateChunktemPtr(_ptr);
            if (!trace_reverse && _ptr == _end) {
                _ptr = NULL;
            } 
        } else if (!trace_reverse) {
            _ptr = NULL;
        }
        return oop;
    }
};

class ReverseIterator : public NodeIterator<true> {
public:     
    ReverseIterator(ReferrerList* list) {
        initIterator(list);
    }
};

class AnchorIterator : public NodeIterator<false> {
    GCObject* _current;
public:    
    AnchorIterator(GCObject* obj) {
        initialize(obj);
    }

    void initialize(GCObject* obj);

    GCObject* peekPrev() {
        return _current;
    }

    GCObject* next_obj() {
        this->_current = next();
        return this->_current;
    }

    ShortOOP& next() {
        this->_current = *_ptr;
        return NodeIterator<false>::next();
    }
};

}