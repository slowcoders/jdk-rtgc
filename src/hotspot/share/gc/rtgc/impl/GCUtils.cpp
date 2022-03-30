#include "gc/rtgc/rtgcDebug.hpp"
#include "oops/compressedOops.hpp"
#include "GCObject.hpp"
#include "memory/virtualspace.hpp"

using namespace RTGC;
namespace RTGC {

    static const int OBJ_ALIGN = 8;

    uint32_t _pointer2offset(void* ref, void* origin) {
        precond(ref != nullptr);
        precond(((uintptr_t)ref & (OBJ_ALIGN-1)) == 0);
        precond((address)ref > CompressedOops::base());
        uintptr_t offset = ((address)ref - CompressedOops::base()) / OBJ_ALIGN;
        precond(offset == (uint32_t)offset);
        return (uint32_t)offset;
    }

    void* _offset2Pointer(uint32_t offset, void* origin) {
        precond(offset != 0);
		return (void*)(CompressedOops::base() + (uintptr_t)offset * OBJ_ALIGN);
    }
}


LinkIterator::LinkIterator(GCObject* obj) {
    this->_anchor = obj;
    assert(0, "not impl");
    #if 0
    const uint16_t* off = obj->getFieldOffsets();
    if (off == GCArray::_array_offsets) {
        this->_idxField = _array->length();
    }
    else {
        this->_offsets = off;
    }
    #endif
}

GCObject* LinkIterator::next() {
    union {
        const uint16_t* offsets;
        intptr_t idxField;
    };
    offsets = this->_offsets;
    assert(0, "not impl");
#if 0
    if (offsets < (void*)(intptr_t)0xFFFFFFFF) {
        while (--idxField >= 0) {
            GCObject* ref = _array->getItemPointer(idxField)->getPointer();//     *pItem++;
            if (ref != nullptr) {
                this->_idxField = idxField;
                return ref;
            }
        }
    }
    else {
        while (true) {
            int offset = *offsets ++;
            if (offset == 0) break;
            OffsetPointer<GCObject>* field = 
                (OffsetPointer<GCObject>*)((uintptr_t)_anchor + offset);
            GCObject* ref = field->getPointer();
            if (ref != nullptr) {
                this->_offsets = offsets;
                return ref;
            }
        }
    }

#endif
    return nullptr;
}








#ifdef _MSC_VER
    #include <windows.h>
#else
    #include <sys/mman.h>
    #include <sys/types.h>
    #include <sys/stat.h>
    #include <fcntl.h>
#endif

#define _USE_JVM 0
#define _USE_MMAP 1
#define _ULIMIT 1
void* RTGC::VirtualMemory::reserve_memory(size_t bytes) {
    precond(bytes % MEM_BUCKET_SIZE == 0);
    void* addr;    
#if _USE_JVM
    size_t total_reserved = bytes;
    size_t page_size = os::vm_page_size();
    size_t alignment = page_size;
    ReservedHeapSpace total_rs(total_reserved, alignment, page_size, NULL);
    addr = total_rs.base();// nullptr;
#elif defined(_MSC_VER)
    addr = VirtualAlloc(nullptr, bytes, MEM_RESERVE, PAGE_READWRITE);
#elif _USE_MMAP
    addr = mmap(nullptr, bytes, PROT_NONE, MAP_PRIVATE|MAP_ANONYMOUS|MAP_NORESERVE, -1, 0);
#else
    addr = malloc(bytes);
#if _ULIMIT    
    addr = realloc(addr, 4096);
#endif
#endif
    assert(addr != NULL, "reserve mem fail");
    return addr;
}

void RTGC::VirtualMemory::commit_memory(void* addr, void* bucket, size_t bytes) {
    precond(bytes % MEM_BUCKET_SIZE == 0);
#if _USE_JVM
    rtgc_log(true, "commit_memory\n");
    return;
#elif defined(_MSC_VER)
    addr = VirtualAlloc(bucket, MEM_BUCKET_SIZE, MEM_COMMIT, PAGE_READWRITE);
    if (addr != 0) return;
#elif _USE_MMAP
    int res = mprotect(bucket, bytes, PROT_READ|PROT_WRITE);
    rtgc_log(0, "commit_memory mprotect %p\n", addr);
    if (res == 0) return;
#elif _ULIMIT    
    void* mem = ::realloc(addr, offset + bytes);
    if (mem == addr) return;
#endif
    assert(0, "OutOfMemoryError:E009");
}

