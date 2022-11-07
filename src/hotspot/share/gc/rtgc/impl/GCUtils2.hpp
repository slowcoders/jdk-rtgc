#include <memory.h>
#include "utilities/globalDefinitions_gcc.hpp"
#include "utilities/debug.hpp"
#include "gc/rtgc/rtgcDebug.hpp"


namespace RTGC {

static const int OOP_IN_CHUNK = 7;
struct TinyChunk {
	ShortOOP _items[OOP_IN_CHUNK];
	int32_t _nextOffset;
};

class NodeIterator {
    static ShortOOP g_end_of_node;
public:
    ShortOOP  _current;
    ShortOOP* _next_p;

    void initSingleIterator(ShortOOP temp) {
        _current = temp;
        _next_p = &g_end_of_node;
    }

    bool hasNext() {
        return (_next_p[0] != NULL);
    }

    ShortOOP& peekPrev() {
        return _current;
    }

    ShortOOP& next() {
        precond(hasNext());
        ShortOOP next = _next_p[0];
        if ((next & 1) != 0) {
            _next_p = _next_p + next;
        }
        _current = *_next_p++;
        return _current;
    }

    void reset(ShortOOP* location) {
        _current = location[0];
        _next = location + 1;
    }
};

class TinyVector : private SimpleVector<ShortOOP> {
    TinyChunk* _head;

    void removeAt(int idx) {
        if (SUPER::size() == 2) {
            ShortOOP remained = SUPER::_data->_items[1 - idx];
            SUPER::deallocate();
            SUPER::_slot = (intptr_t)remained | SINGLE_ITEM_FLAG;
        }
        else {
            SUPER::removeFast(idx);
        }
    }

    uint32_t& top_offset() {
        return &(uint32_t)&_head.items[1];
    }

    ShortOOP pop() {
        uint32 top = top_offset();
        if (top >= OOP_IN_CHUNK) {
            top = 0
        }
    }

public:
    TinyVector() : SUPER(nullptr) {}

    TinyVector(DoNotInitialize data) : SUPER(data) {}

    ~TinyVector() {}

    bool empty() {
        return SUPER::_data == nullptr;
    }

    ShortOOP first() {
        return _head._items[0];
    }

    void setFirst(ShortOOP first) {
        _head._items[0] = first;
    }


    void add(ShortOOP item) {
        uint32_t& top = top_offset();
        if (top >= OOP_IN_CHUNK) {
            ShortOOP first = _head._items[0];
            _head._items[0] = item;
            _head = new TinyChunk(_head);
            _head.items[0] = _first;
            _head.items[1] = OOP_IN_CHUNK - 1;
        } else {
            _head._items[top--] = item;
        }
    }


    void remove(ShortOOP item) {
        precond(item != nullptr);
        precond(SUPER::_data != nullptr);
        if (SUPER::_slot < 0) {
            precond(SUPER::_slot == ((intptr_t)item | SINGLE_ITEM_FLAG));
            SUPER::_data = nullptr;
        }
        else {
            int idx = SUPER::indexOf(item);
            precond(idx >= 0);
            removeAt(idx);
        }
    }


    bool removeMatchedItems(ShortOOP item) {
        NodeIterator ni(this);
        while (ni.hasNext()) {
            if (ni.peekPrev() == item) {

                ni.location()[0] = 
            }
        }
        precond(item != nullptr);
        if (SUPER::_slot <= 0) {
            if (SUPER::_slot == ((intptr_t)item | SINGLE_ITEM_FLAG)) {
                SUPER::_data = nullptr;
                return true;
            }
            return false;
        }
        else if (SUPER::removeMatchedItems(item)) {
            if (SUPER::size() <= 1) {
                if (SUPER::size() == 0) {
                    SUPER::deallocate();
                    SUPER::_data = nullptr;
                }
                else {
                    ShortOOP remained = SUPER::_data->_items[0];
                    SUPER::deallocate();
                    SUPER::_slot = (intptr_t)remained | SINGLE_ITEM_FLAG;
                }
            }
            return true;
        }
        return false;
    }

    void* getRawData() {
        return SUPER::_data;
    }
};

template <class T, int max_bucket=1024>
class HugeArray : public SimpleVector<T, FixedAllocator<max_bucket>> {
    typedef SimpleVector<T, FixedAllocator<max_bucket>> _SUPER;
public:    
    HugeArray() : _SUPER(DoNotInitialize::Flag) {}
};

template <class T, size_t MAX_BUCKET, int indexOffset, int clearOffset>
class MemoryPool {
    T* _items;
    T* _free;
    T* _next;
    void* _end;
    #if GC_DEBUG
    int _cntFree;
    #endif


    T*& NextFree(void* ptr) {
        return *(T**)ptr;
    }

public:
    void initialize() {
        _end = _next = (T*)VirtualMemory::reserve_memory(MAX_BUCKET*MEM_BUCKET_SIZE);
        _items = _next - indexOffset;
        _free = nullptr;
        #if GC_DEBUG
        _cntFree = 0;
        #endif
    }

    #if GC_DEBUG
    int getAllocatedItemCount() {
        return _next - _items - _cntFree - indexOffset;
    }
    #endif

    T* allocate() {
        T* ptr;
        if (_free == nullptr) {
            ptr = _next ++;
            if (_next >= _end) {
                VirtualMemory::commit_memory(_items, _end, MEM_BUCKET_SIZE);
                _end = (char*)_end + MEM_BUCKET_SIZE;
            }
        }
        else {
            ptr = _free;
            #if GC_DEBUG
            _cntFree --;
            #endif
            _free = NextFree(ptr);
            if (clearOffset >= 0) {
                memset((char*)ptr + clearOffset, 0, sizeof(T) - clearOffset);
            }
        }
        return ptr;
    }

    void delete_(T* ptr) {
        ptr->~T();
        #if GC_DEBUG
        _cntFree ++;
        #endif
        NextFree(ptr) = _free;
        _free = ptr;
    }

    T* getPointer(int idx) {
        assert(_items + idx < _next, "invalid idx: %d (max=%ld)\n", idx, _next - _items);
        T* ptr = _items + idx;
        return ptr;
    }

    int size() {
        return _next - _items;
    }

    int getIndex(void* ptr) {
        return (int)((T*)ptr - _items);
    }

    bool isInside(void* mem) {
        return mem >= _items && mem < _items + MAX_BUCKET;
    }
};


}
