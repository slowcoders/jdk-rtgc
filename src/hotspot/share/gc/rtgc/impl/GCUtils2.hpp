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
    void initEmpty() {
        _head._last_item_offset = -MAX_COUNT_IN_CHUNK;
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
        return approximated_item_count() == 0;
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
        precond(!empty());
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

    const ShortOOP* getItemPtr(ShortOOP item);

    uint32_t approximated_item_count() {
        uint32_t size = _head._last_item_offset + (MAX_COUNT_IN_CHUNK+1);
        return size;
    }

    const ShortOOP* getLastItemOffsetPtr() {
        return &_head._items[MAX_COUNT_IN_CHUNK];
    }    

    static bool isEndOfChunk(const ShortOOP* ptr) {
        return ((uintptr_t)ptr & CHUNK_MASK) == (sizeof(Chunk) - sizeof(ShortOOP));
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

    const ShortOOP* extend_tail(Chunk* last_chunk);

    Chunk* dealloc_chunk(Chunk* chunk);

    void set_last_item_ptr(const ShortOOP* pLast) {
        _head._last_item_offset = pLast - &_head._items[MAX_COUNT_IN_CHUNK];
    }

    void cut_tail_end(ShortOOP* copy_to);
};

template <bool trace_reverse>
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
        return _ptr != (trace_reverse ? _end : NULL);
    }

    const ShortOOP* getAndNext() {
        precond(hasNext());
        const ShortOOP* oop = _ptr ++;
        precond((GCObject*)*oop != NULL);
        if (_ptr != _end) {
            if (ReferrerList::isEndOfChunk(_ptr)) {
                _ptr = _ptr + *(int32_t*)_ptr;
            }
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
        precond(!list->isTooSmall());
        if (!list->hasMultiChunk()) {
            _ptr = list->firstItemPtr();
            _end = list->lastItemPtr() + 1;
        } else {
            _ptr = list->lastItemPtr();
            _end = list->getLastItemOffsetPtr();
        }
        postcond(_ptr != _end);
    }     
};

class AnchorIterator : public NodeIterator<false> {
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
        this->_current = *getAndNext();
        return this->_current;
    }

    const ShortOOP* getAndNext() {
        this->_current = *_ptr;
        return NodeIterator<false>::getAndNext();
    }
};

}