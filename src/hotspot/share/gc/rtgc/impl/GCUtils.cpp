#include "gc/rtgc/rtgcDebug.hpp"
#include "oops/compressedOops.hpp"
#include "GCObject.hpp"
#include "memory/virtualspace.hpp"
#include "oops/oopHandle.inline.hpp"

using namespace RTGC;

namespace RTGC {
    static const int OBJ_ALIGN = 8;
    ReferrerList::ChunkPool ReferrerList::g_chunkPool;

    uint32_t _pointer2offset(void* ref) {
        precond(ref != nullptr);
        assert(((uintptr_t)ref & (OBJ_ALIGN-1)) == 0, "invalid_short_oop %p\n", ref);
        precond((address)ref > CompressedOops::base());
        uintptr_t offset = ((address)ref - CompressedOops::base()) / OBJ_ALIGN;
        assert(offset == (uint32_t)offset, "invalid_short_oop %p\n", ref);
        return (uint32_t)offset;
    }

    void* _offset2Pointer(uint32_t offset) {
        precond(offset != 0);
		return (void*)(CompressedOops::base() + (uintptr_t)offset * OBJ_ALIGN);
    }
}

ShortOOP* ReferrerList::extend_tail(Chunk* last_chunk) {
    Chunk* tail = g_chunkPool.allocate();
    precond(((uintptr_t)tail & CHUNK_MASK) == 0);
    tail->_last_item_offset = last_chunk->_items - &tail->_items[MAX_COUNT_IN_CHUNK];
    return &tail->_items[MAX_COUNT_IN_CHUNK-1];
}


ReferrerList::Chunk* ReferrerList::dealloc_chunk(Chunk* chunk) {
    //rtgc_log(true, "dealloc_chunk %p\n", this);
    Chunk* prev = (Chunk*)(&chunk->_items[MAX_COUNT_IN_CHUNK] + chunk->_last_item_offset);
    precond(((uintptr_t)prev & CHUNK_MASK) == 0);
    g_chunkPool.delete_(chunk);
    return prev;
}


void ReferrerList::cut_tail_end(ShortOOP* copy_to) {
    ShortOOP* pLast = lastItemPtr();
    if (copy_to != NULL) {
        *copy_to = *pLast;
    }
    Chunk* last_chunk = (Chunk*)((uintptr_t)pLast & ~CHUNK_MASK);
    if (last_chunk == &_head) {
        _head._last_item_offset --;
    } else if (pLast - last_chunk->_items == MAX_COUNT_IN_CHUNK - 1) {
        last_chunk = dealloc_chunk(last_chunk);
        if (last_chunk == &_head) {
            _head._last_item_offset = -1;
        } else {
            set_last_item_ptr(&last_chunk->_items[0]);
        }
    } else {
        _head._last_item_offset ++;
    }
}

void ReferrerList::add(ShortOOP item) {
    ShortOOP* pLast = lastItemPtr();
    Chunk* last_chunk = (Chunk*)((uintptr_t)pLast & ~CHUNK_MASK);
    if (last_chunk == &_head) {
        if (pLast == &_head._items[MAX_COUNT_IN_CHUNK - 1]) {
            pLast = extend_tail(last_chunk);
            set_last_item_ptr(pLast);
        } else {
            pLast++;
            _head._last_item_offset ++;
        }
    } else {
        if (((uintptr_t)pLast % sizeof(Chunk)) == 0) {
            pLast = extend_tail(last_chunk);
            set_last_item_ptr(pLast);
        } else {
            pLast --;
            _head._last_item_offset --;
        }
    }
    *pLast = item;
}

static ShortOOP* __getItemPtr(ReverseIterator& iter, ShortOOP item) {
    while (iter.hasNext()) {
        ShortOOP& ptr = iter.next();
        if (ptr == item) {
            return &ptr;
        }
    }
    return NULL;
}

ShortOOP* ReferrerList::getItemPtr(ShortOOP item) {
    ReverseIterator iter(this);
    return __getItemPtr(iter, item);
}

void ReferrerList::replaceFirst(ShortOOP new_first) {
    ShortOOP old_first = this->front();
    if (old_first != new_first) {
        ShortOOP* pItem = getItemPtr(new_first);
        assert(pItem != NULL, "incorrect anchor %p(%s)\n",
            (GCObject*)new_first, RTGC::getClassName((GCObject*)new_first));
        *firstItemPtr() = new_first;
        *pItem = old_first;
    }
}

const void* ReferrerList::remove(ShortOOP item) {
    ShortOOP* pItem = getItemPtr(item);
    if (pItem != NULL) {
        cut_tail_end(pItem);
        return pItem;
    }
    return NULL;
}

const void* ReferrerList::removeMatchedItems(ShortOOP item) {
    ReverseIterator iter(this);
    const void* last_removed = NULL;
    while (true) {
        ShortOOP* pItem = __getItemPtr(iter, item);
        if (pItem == NULL) break;
        cut_tail_end(pItem);
        last_removed = pItem;
    }
    return last_removed;
}

AnchorIterator::AnchorIterator(GCObject* obj) {
    obj->initIterator(this);
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
    rtgc_log(false, "reserve_memory %p %dk\n", addr, (int)(bytes/1024));
    return addr;
}

void RTGC::VirtualMemory::commit_memory(void* addr, void* bucket, size_t bytes) {
    precond(bytes % MEM_BUCKET_SIZE == 0);
#if _USE_JVM
    rtgc_log(1, "commit_memory\n");
    return;
#elif defined(_MSC_VER)
    addr = VirtualAlloc(bucket, MEM_BUCKET_SIZE, MEM_COMMIT, PAGE_READWRITE);
    if (addr != 0) return;
#elif _USE_MMAP
    int res = mprotect(bucket, bytes, PROT_READ|PROT_WRITE);
    rtgc_log(false, "commit_memory mprotect %p:%p %d res=%d\n", addr, bucket, (int)(bytes/1024), res);
    if (res == 0) return;
#elif _ULIMIT    
    void* mem = ::realloc(addr, offset + bytes);
    if (mem == addr) return;
#endif
    assert(0, "OutOfMemoryError:E009");
}

void RTGC::VirtualMemory::free(void* addr, size_t bytes) {
    precond(bytes % MEM_BUCKET_SIZE == 0);
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
    assert(0, "Invalid Address:E00A");
}
