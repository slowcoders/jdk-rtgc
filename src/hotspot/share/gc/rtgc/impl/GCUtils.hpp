//#include <stdlib.h>

// #include <deque>
// #include <list>
// #include <map>
// #include <vector>
// #include <functional>
// #include <chrono>
#include <memory.h>
#include "utilities/globalDefinitions_gcc.hpp"
#include "utilities/debug.hpp"
#include "gc/rtgc/rtgcLog.hpp"

#define GC_DEBUG 1
// #if defined(_DEBUG) || defined(GC_DEBUG)
//   #include "assert.h"
// #else
//   #undef assert
//   #define assert(t) // ignore
// #endif

#define PP_MERGE_TOKEN_EX(L, R)	L##R
#define PP_MERGE_TOKEN(L, R)	PP_MERGE_TOKEN_EX(L, R)

#define PP_TO_STRING_EX(T)	    #T
#define PP_TO_STRING(T)			PP_TO_STRING_EX(T)

namespace RTGC {

static const int MEM_BUCKET_SIZE = 64 * 1024;

struct VirtualMemory {
    static void* reserve_memory(size_t bytes);
    static void  commit_memory(void* mem, void* bucket, size_t bytes);
    static void  free(void* mem, size_t bytes);
};

struct DefaultAllocator {
    static void* alloc(size_t size);
    static void* realloc(void* mem, size_t size);
    static void  free(void* mem);
};

template <class T, class Allocator = DefaultAllocator>
struct DynamicAllocator {
    static void* alloc(uint32_t& capacity, size_t item_size, size_t header_size) {
        return Allocator::alloc(capacity * item_size + header_size);
    }

    static void* realloc(void* mem, uint32_t& capacity, size_t item_size, size_t header_size) {
        capacity = ((capacity * 2 + 7) & ~7) - 1;
        return Allocator::realloc(mem, capacity * item_size + header_size);
    }

    static void free(void* mem) {
        Allocator::free(mem);
    }
};

template <int max_bucket>
struct FixedAllocator {

    static void* alloc(uint32_t& capacity, size_t item_size, size_t header_size) {
        capacity = (MEM_BUCKET_SIZE - header_size) / item_size;
        rtgc_log(false, "fixed_alloc cap=%d, off=%d\n", capacity, (int)header_size);
        void* mem = VirtualMemory::reserve_memory(max_bucket * MEM_BUCKET_SIZE);
        VirtualMemory::commit_memory(mem, mem, MEM_BUCKET_SIZE);
        return mem;
    }

    static void* realloc(void* mem, uint32_t& capacity, size_t item_size, size_t header_size) {
        int idx_bucket = (capacity * item_size + MEM_BUCKET_SIZE - 1) / MEM_BUCKET_SIZE;
        precond(idx_bucket < max_bucket);
        int mem_offset = idx_bucket * MEM_BUCKET_SIZE;
        VirtualMemory::commit_memory(mem, (char*)mem + mem_offset, MEM_BUCKET_SIZE);
        capacity = (mem_offset + MEM_BUCKET_SIZE - header_size) / item_size;
        rtgc_log(false, "fixed_realloc cap=%d, off=%d\n", capacity, (int)header_size);
        return mem; 
    }

    static void free(void* mem) { 
        VirtualMemory::free(mem, max_bucket * MEM_BUCKET_SIZE); 
    }
};

enum class DoNotInitialize {
    Flag
};



template <class T, class Allocator = DynamicAllocator<T>>
class SimpleVector {
public:
    struct Data {
        uint32_t _size;
        uint32_t _capacity;
        T _items[31];
    };


protected:
    union {
        Data* _data;
        intptr_t _slot;
    };

    size_t alloc_size(size_t capacity) {
        return sizeof(int) * 2 + sizeof(T) * capacity;
    }

    SimpleVector(DoNotInitialize data) {
        // do nothing;
    }

    void initial_allocate(uint32_t capacity, int initial_size) {
        _data = (Data*)Allocator::alloc(capacity, sizeof(T), 8);
        _data->_capacity = (int)capacity;
        _data->_size = initial_size;
    }

    void deallocate() {
        if ((intptr_t)_data > 0) Allocator::free(_data);
    }

public:
    SimpleVector(uint32_t capacity = 8) {
        initial_allocate(capacity, 0);
    }

    SimpleVector(Data* data) {
        _data = data;
    }

    void initialize() {
        if (_data == NULL) initial_allocate(8, 0);
    }

    ~SimpleVector() {
        deallocate();
    }

    T front() {
        precond(!empty());
        return this->at(0);
    }

    int size() {
        return _data->_size;
    }

    int getCapacity() {
        return _data->_capacity;
    }

    bool empty() {
        return _data->_size == 0; 
    }


    T* push_empty() {
        if (_data->_size >= _data->_capacity) {
            _data = (Data*)Allocator::realloc(_data, _data->_capacity, sizeof(T), 8);
        }
        return _data->_items + _data->_size++;
    }

    T* push_empty_at(int idx) {
        push_empty();
        int copy_size = (size() - idx) * sizeof(T);
        T* src = adr_at(idx);
        memmove(src + 1, src, copy_size);
        return src;
    }

    void push_back(T item) {
        T* back = push_empty();
        back[0] = item;
    }

    T& back() {
        precond(_data->_size > 0);
        return _data->_items[_data->_size - 1];
    }

    void pop_back() {
        precond(_data->_size > 0);
        _data->_size --;
    }

    T& operator[](size_t __n) {
        precond(__n >= 0 && __n < _data->_capacity);
        return _data->_items[__n];
    }

    T& at(size_t __n) {
        precond(__n >= 0 && __n < _data->_capacity);
        return _data->_items[__n];
    }

    T* adr_at(size_t __n) {
        precond(__n >= 0 && __n < _data->_capacity);
        return _data->_items + __n;
    }

    void resize(size_t __n) {
        precond(__n >= 0 && __n <= _data->_capacity);
        _data->_size = (int)__n;
    }

    int indexOf(T v) {
        /* find reverse direction */
        int idx = _data->_size;
        T* pObj = _data->_items + idx;
        for (; --idx >= 0; ) {
            --pObj;
            if (*pObj == v) break;
        }
        return idx;
    }

    bool contains(T v) {
        return indexOf(v) >= 0;
    }

    SimpleVector* operator -> () {
        return this;
    }

    void removeAndShift(int idx) {
        T* mem = adr_at(idx);
        int newSize = this->size() - 1;
        this->resize(newSize);
        int remain = newSize - idx;
        if (remain > 0) {
            memcpy(mem, mem + 1, sizeof(T) * remain);
        }
    }


    bool removeFast(T v) {
        int idx = indexOf(v);
        if (idx < 0) {
            return false;
        }
        removeFast(idx);
        return true;
    }

    bool removeMatchedItems(T v) {
        int idx = _data->_size;
        T* pObj = _data->_items + idx;
        bool found = false;
        for (; --idx >= 0; ) {
            --pObj;
            if (*pObj == v) {
                found = true;
                removeFast(idx);
            }
        }
        return found;
    }

    void removeFast(int idx) {
        precond(idx >= 0 && idx < this->size());
        int newSize = this->size() - 1;
        if (idx < newSize) {
            _data->_items[idx] = this->at(newSize);
        }
        this->resize(newSize);
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
