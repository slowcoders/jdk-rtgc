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


    class TailNodeIterator : public NodeIterator<false> {
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
    rt_assert(((uintptr_t)tail & CHUNK_MASK) == 0);
    tail->_last_item_offset = last_chunk->_items - &tail->_items[MAX_COUNT_IN_CHUNK];
    rt_assert_f(tail->_last_item_offset != -8, "empty chunk %p", tail); 
    rt_assert(tail->isAlive());

    return &tail->_items[MAX_COUNT_IN_CHUNK-1];
}


void ReferrerList::dealloc_chunk(Chunk* chunk) {
    rtgc_log(true, "dealloc_chunk %p %d\n", chunk, chunk->_last_item_offset);
    chunk->setDestroyed();
    g_chunkPool.delete_(chunk);
    rt_assert(!chunk->isAlive());
}


void ReferrerList::cut_tail_end(ShortOOP* copy_to) {
    const ShortOOP* pLast = lastItemPtr();
    if (copy_to != NULL) {
        *copy_to = *pLast;
    }
    Chunk* last_chunk = getContainingChunck(pLast);
    if (last_chunk == &_head) {
        _head._last_item_offset --;
    } else if (pLast - last_chunk->_items == MAX_COUNT_IN_CHUNK - 1) {
        Chunk* tail = last_chunk->getNextChunk();
        dealloc_chunk(last_chunk);
        if (tail == &_head) {
            _head._last_item_offset = -1;
        } else {
            set_last_item_ptr(&tail->_items[0]);
        }
        rt_assert_f(tail->_last_item_offset != -8, "empty chunk %p", tail); 
    } else {
        _head._last_item_offset ++;
    }
    rt_assert(_head.isAlive());
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
            rt_assert(((uintptr_t)pLast & ~CHUNK_MASK) == (uintptr_t)pLast);
            pLast = extend_tail((Chunk*)pLast);
            set_last_item_ptr(pLast);
        } else {
            pLast --;
            _head._last_item_offset --;
        }
    }
    rt_assert(_head.isAlive());
    if (this->hasMultiChunk()) {
        rt_assert_f(_head.getNextChunk()->_last_item_offset != -8, "empty chunk %p", this); 
    }
    *(ShortOOP*)pLast = item;
}

static const ShortOOP* __getItemPtr(TailNodeIterator& iter, ShortOOP item) {
    while (iter.hasNext()) {
        const ShortOOP* ptr = iter.next_ptr();
        rt_assert(!rtHeap::useModifyFlag() || (ptr->getOffset() & 1) == 0); 
        rt_assert(ptr != NULL && ptr->getOffset() != 0);
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

    for (; pItem >= _head._items; pItem--) {
        rt_assert_f(pItem->getOffset() != 0, "pItem %p, base=%p count=%d\n", pItem, this, this->approximated_item_count());
        if (*pItem == item) return pItem;
    }
    return NULL;
}

void ReferrerList::replaceFirst(ShortOOP new_first) {
    ShortOOP old_first = this->front();
    if (old_first != new_first) {
        const ShortOOP* pItem = getItemPtr(new_first);
        rt_assert_f(pItem != NULL, "incorrect anchor %p(%s)\n",
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

const void* ReferrerList::replace(ShortOOP old_p, ShortOOP new_p) {
    rt_assert(new_p.getOffset() != 0);
    const ShortOOP* pItem = getItemPtr(old_p);
    rt_assert(pItem != NULL);
    *(ShortOOP::OffsetType*)pItem = new_p.getOffset();
    return pItem;
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
        rt_assert(!this->empty());
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
    const RtNode* nx = obj->node_();
    if (!nx->mayHaveAnchor()) {
        this->initEmpty();
    }
    else if (!nx->hasMultiRef()) {
        this->initSingleIterator(&nx->getSingleAnchor());
    }
    else {
        ReferrerList* referrers = nx->getAnchorList();
        this->initIterator(referrers);
    }
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
    rt_assert(bytes % MEM_BUCKET_SIZE == 0);
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
    rt_assert(bytes % MEM_BUCKET_SIZE == 0);
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
    rt_assert_f(0, "OutOfMemoryError:E009");
}

void RTGC::VirtualMemory::free(void* addr, size_t bytes) {
    rt_assert(bytes % MEM_BUCKET_SIZE == 0);
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
