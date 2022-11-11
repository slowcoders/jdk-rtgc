#include "gc/rtgc/rtgcDebug.hpp"
#include "oops/compressedOops.hpp"
#include "GCObject.hpp"
#include "memory/virtualspace.hpp"
#include "oops/oopHandle.inline.hpp"

using namespace RTGC;

namespace RTGC {
    static const int OBJ_ALIGN = 8;

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

    class TailNodeIterator : public NodeIterator<true> {
    public:    
        TailNodeIterator(ReferrerList* list) {
            _ptr = list->lastItemPtr();
            _end = list->firstItemPtr();
        }     
    };

}

ReferrerList::ChunkPool ReferrerList::g_chunkPool;

const ShortOOP* ReferrerList::extend_tail(Chunk* last_chunk) {
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
    const ShortOOP* pLast = lastItemPtr();
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
    const ShortOOP* pLast = lastItemPtr();
    if (!hasMultiChunk()) {
        if (pLast == &_head._items[MAX_COUNT_IN_CHUNK - 1]) {
            pLast = extend_tail(&_head);
            set_last_item_ptr(pLast);
        } else {
            pLast++;
            _head._last_item_offset ++;
        }
    } else {
        if (((uintptr_t)pLast & CHUNK_MASK) == 0) {
            precond(((uintptr_t)pLast & ~CHUNK_MASK) == (uintptr_t)pLast);
            pLast = extend_tail((Chunk*)pLast);
            set_last_item_ptr(pLast);
        } else {
            pLast --;
            _head._last_item_offset --;
        }
    }
    *(ShortOOP*)pLast = item;
}

static const ShortOOP* __getItemPtr(TailNodeIterator& iter, ShortOOP item) {
    while (iter.hasNext()) {
        const ShortOOP* ptr = iter.getAndNext();
        if (*ptr == item) {
            return ptr;
        }
    }
    return NULL;
}

const ShortOOP* ReferrerList::getItemPtr(ShortOOP item) {
    const ShortOOP* pItem;
    if (this->hasMultiChunk()) {
        TailNodeIterator iter(this);
        pItem = __getItemPtr(iter, item);
        if (pItem != NULL) return pItem;
        pItem = _head._items + (MAX_COUNT_IN_CHUNK - 1);
    } else {
        pItem = lastItemPtr();
    }

    for (;; pItem--) {
        if (*pItem == item) return pItem;
    }
    return NULL;
}

void ReferrerList::replaceFirst(ShortOOP new_first) {
    ShortOOP old_first = this->front();
    if (old_first != new_first) {
        const ShortOOP* pItem = getItemPtr(new_first);
        assert(pItem != NULL, "incorrect anchor %p(%s)\n",
            (GCObject*)new_first, RTGC::getClassName((GCObject*)new_first));
        _head._items[0] = new_first;
        *(ShortOOP*)pItem = old_first;
    }
}

const void* ReferrerList::remove(ShortOOP item) {
    const ShortOOP* pItem = getItemPtr(item);
    if (pItem != NULL) {
        cut_tail_end((ShortOOP*)pItem);
        return pItem;
    }
    return NULL;
}


const void* ReferrerList::removeMatchedItems(ShortOOP item) {
    const void* last_removed = NULL;
    const ShortOOP* pItem;
    if (this->hasMultiChunk()) {
        TailNodeIterator iter(this);
        while ((pItem = __getItemPtr(iter, item)) != NULL) {
            cut_tail_end((ShortOOP*)pItem);
            last_removed = pItem;
        }
        pItem = _head._items + (MAX_COUNT_IN_CHUNK - 1);
    } else {
        pItem = lastItemPtr();
    }

    for (;; pItem--) {
        if (*pItem == item) {
            cut_tail_end((ShortOOP*)pItem);
            last_removed = pItem;
        }
        if (pItem == _head._items) break;
    }

    return last_removed;
}

void AnchorIterator::initialize(GCObject* obj) {
    if (!obj->isAnchored()) {
        this->initEmpty();
    }
    else if (!obj->hasMultiRef()) {
        this->initSingleIterator((ShortOOP*)(void*)&obj->_refs);
    }
    else {
        ReferrerList* referrers = obj->getReferrerList();
        this->initIterator(referrers);
    }
    // obj->initIterator(this);
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
