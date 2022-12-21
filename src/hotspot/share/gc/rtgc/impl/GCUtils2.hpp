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
        Chunk*  getNextChunk() { return (Chunk*)(&_last_item_offset + _last_item_offset); }
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

    // returns removed item pointer
    const void* replace(ShortOOP old_p, ShortOOP new_p);

    // returns removed item pointer (the memory may not accessable);
    const void* remove(ShortOOP item);

    // returns lowerest removed item pointer (the memory may not accessable);
    const void* removeMatchedItems(ShortOOP item);

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

    static void delete_(ReferrerList* list) {
        Chunk* chunk = getContainingChunck(list->getLastItemOffsetPtr());
        while (true) {
            g_chunkPool.delete_(chunk);
            if (chunk == list->_head) break;
            chunk = chunk->getNextChunk();
        };
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

    Chunk* getContainingChunck(const ShortOOP* pItem) {
        return (Chunk*)((uintptr_t)ptr & ~CHUNK_MASK);
    }

    void cut_tail_end(ShortOOP* copy_to);
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
        precond(hasNext());
        const ShortOOP* oop = _ptr ++;
        precond((GCObject*)*oop != NULL);
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
        precond(!list->isTooSmall());
        if (!list->hasMultiChunk()) {
            _ptr = list->firstItemPtr();
            _end = list->lastItemPtr() + 1;
        } else {
            _ptr = list->lastItemPtr();
            _end = list->getNextChuckOffsetPtr();
        }
        postcond(_ptr != _end);
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