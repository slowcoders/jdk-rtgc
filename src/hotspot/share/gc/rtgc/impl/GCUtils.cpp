#include "runtime/globals.hpp"
#include "memory/virtualspace.hpp"
#include "oops/oopHandle.hpp"
#include "oops/oopHandle.inline.hpp"
#include "oops/compressedOops.hpp"
#include "gc/rtgc/rtgcGlobals.hpp"
#include "GCObject.hpp"
#include "../rtHeapEx.hpp"

using namespace RTGC;

namespace RTGC {
    void* _offset2Pointer(uint32_t offset) {
        rt_assert(offset != 0);
        rt_assert_f(!rtHeap::useModifyFlag() || (offset & 1) == 0, "wrong offset %x\n", offset); 
        uintptr_t base = (uintptr_t)CompressedOops::base();
        int shift = UseCompressedOops ? CompressedOops::shift() : 3;
        return (void*)(base + ((uintptr_t)offset << shift));
    }

    uint32_t _pointer2offset(void* ptr) {
        uintptr_t ref = (uintptr_t)ptr; 
        uintptr_t base = (uintptr_t)CompressedOops::base();
        rt_assert(ref > base);
        rt_assert(ref < base + OopEncodingHeapMax);
        int shift = UseCompressedOops ? CompressedOops::shift() : 3;
        uint32_t result = (uint32_t)((ref - base) >> shift);
        rt_assert(!rtHeap::useModifyFlag() || (result & 1) == 0); 
        rt_assert_f(_offset2Pointer(result) == ptr, "reversibility %p >> %d -> %x : %p\n", 
            ptr, shift, result, _offset2Pointer(result));
        return result;
    }

#if 0
    class TailNodeIterator : public NodeIterator<false> {
    public:    
        TailNodeIterator(ReferrerList* list) {
            _ptr = list->lastItemPtr();
            _end = list->firstItemPtr();
        }     
    };
#endif
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
    rt_assert(bytes % MEM_BUCKET_ALIGN_SIZE == 0);
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
    rt_assert_f(addr != NULL, "reserve mem fail");
    rtgc_log(false, "reserve_memory %p %dk\n", addr, (int)(bytes/1024));
    return addr;
}

void RTGC::VirtualMemory::commit_memory(void* addr, void* bucket, size_t bytes) {
    rt_assert(bytes % MEM_BUCKET_ALIGN_SIZE == 0);
#if _USE_JVM
    rtgc_log(1, "commit_memory\n");
    return;
#elif defined(_MSC_VER)
    addr = VirtualAlloc(bucket, MEM_BUCKET_ALIGN_SIZE, MEM_COMMIT, PAGE_READWRITE);
    if (addr != 0) return;
#elif _USE_MMAP
    int res = mprotect(bucket, bytes, PROT_READ|PROT_WRITE);
    rtgc_log(false, "commit_memory mprotect %p:%p %d res=%d\n", addr, bucket, (int)(bytes/1024), res);
    if (res == 0) return;
#elif _ULIMIT    
    void* mem = ::realloc(addr, offset + bytes);
    if (mem == addr) return;
#endif
    rt_assert_f(0, "OutOfMemoryError:E009");
}

void RTGC::VirtualMemory::free(void* addr, size_t bytes) {
    rt_assert(bytes % MEM_BUCKET_ALIGN_SIZE == 0);
#if _USE_JVM
    fatal(1, "free_memory\n");
    return;
#elif defined(_MSC_VER)
    addr = VirtualFree(addr, bytes, MEM_RELEASE);
    if (addr != 0) return;
#elif _USE_MMAP
    int res = munmap(addr, bytes);
    if (res == 0) return;
#else    
    ::free(addr);
    return;
#endif
    rt_assert_f(0, "Invalid Address:E00A");
}
