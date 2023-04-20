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

void ReferrerList::initialize() {
    char* memory = (char*)VirtualMemory::reserve_memory(g_chunkPool.getReservedMemorySize());
    g_chunkPool.initialize((Chunk*)memory);
}

template <typename Closure>
static const ShortOOP* __getItemPtr(TailNodeIterator& iter, Closure* closure) {
    while (iter.hasNext()) {
        const ShortOOP* ptr = iter.next_ptr();
        rt_assert(!rtHeap::useModifyFlag() || (ptr->getOffset() & 1) == 0); 
        rt_assert(ptr != NULL && ptr->getOffset() != 0);
        if (closure->visit(ptr)) {
            return ptr;
        }
    }
    return NULL;
}

template <typename Closure>
const ShortOOP* reverse_iterate(ReferrerList* list, Closure* closure) {
    const ShortOOP* pItem;
    if (list->hasMultiChunk()) {
        TailNodeIterator iter(list);
        pItem = __getItemPtr(iter, closure);
        if (pItem != NULL) return pItem;
        pItem = list->_head._items + (ReferrerList::MAX_COUNT_IN_CHUNK - 1);
    } else {
        pItem = list->lastItemPtr();
    }

    for (; pItem >= list->_head._items; pItem--) {
        rt_assert_f(pItem->getOffset() != 0, "pItem %p, base=%p count=%d\n", pItem, list, list->approximated_item_count());
        if (closure->visit(pItem)) return pItem;
    }
    return NULL;
}

const ShortOOP* ReferrerList::extend_tail(Chunk* last_chunk) {
    // 주의) chnkPool 의 startOffset 은 1이다. 
    ChunkPool* chunkPool = &g_chunkPool;
    rt_assert(chunkPool->contains(this));
    rt_assert(chunkPool->contains(last_chunk));

    Chunk* tail = chunkPool->allocate();
    if (MARK_ALIVE_CHUNK) {
        *(uintptr_t*)tail = 0; // clear nextFree address 
    }
    rt_assert(((uintptr_t)tail & CHUNK_MASK) == 0);
    tail->_last_item_offset = last_chunk->_items - &tail->_items[MAX_COUNT_IN_CHUNK];
    rt_assert(tail->getNextChunk() == last_chunk); 
    rt_assert(tail->isAlive());
    return &tail->_items[MAX_COUNT_IN_CHUNK-1];
}

void ReferrerList::dealloc_chunk(Chunk* chunk) {
    // rtgc_log(true, "dealloc_chunk %p %d\n", chunk, chunk->_last_item_offset);
    if (MARK_ALIVE_CHUNK) {
        rt_assert(sizeof(Chunk) / sizeof(uintptr_t) == 4);
        uintptr_t* mem = (uintptr_t*)chunk;
        mem[1] = 0;
        mem[2] = 0;
        mem[3] = 0;
    }
    g_chunkPool.delete_(chunk);
    rt_assert(!chunk->isAlive());
}

void ReferrerList::delete_(ReferrerList* list) {
    if (list->empty()) {
        dealloc_chunk(&list->_head);
        return;
    }
    Chunk* chunk = getContainingChunck(list->lastItemPtr());
    while (true) {
        Chunk* nextChunk = chunk->getNextChunk();
        dealloc_chunk(chunk);
        if (chunk == &list->_head) break;
        chunk = nextChunk;
    };
}


void ReferrerList::cut_tail_end(ShortOOP* copy_to) {
    rt_assert(!this->empty());
    const ShortOOP* pLast = lastItemPtr();
    if (copy_to != pLast) {
        *copy_to = *pLast;
    }
    // for opt scan
    if (MARK_ALIVE_CHUNK) {
        *(ShortOOP::OffsetType*)pLast = 0;
    }
    Chunk* last_chunk = getContainingChunck(pLast);
    if (last_chunk == &_head) {
        _head._last_item_offset --;
    } else if (pLast - last_chunk->_items == MAX_COUNT_IN_CHUNK - 1) {
        Chunk* tail = last_chunk->getNextChunk();
        Chunk* prev = (Chunk*)(&last_chunk->_items[MAX_COUNT_IN_CHUNK] + last_chunk->_last_item_offset);
        rt_assert(prev == tail);
        dealloc_chunk(last_chunk);
        if (tail == &_head) {
            _head._last_item_offset = -1;
        } else {
            set_last_item_ptr(&tail->_items[0]);
        }
    } else {
        _head._last_item_offset ++;
    }
    rt_assert(_head.isAlive());
}

ReferrerList* ReferrerList::allocate() {
    ReferrerList* list;
    list = (ReferrerList*)g_chunkPool.allocate();
    return list;
}

static const ShortOOP* __getFirstDirtyItemPtr(TailNodeIterator& iter) {
    const ShortOOP* pLastItem = NULL;
    while (iter.hasNext()) {
        const ShortOOP* ptr = iter.next_ptr();
        rt_assert(!rtHeap::useModifyFlag() || (ptr->getOffset() & 1) == 0); 
        rt_assert(ptr != NULL && ptr->getOffset() != 0);
        if (ptr[0]->isTrackable_unsafe()) {
            return pLastItem;
        }
        pLastItem = ptr;
    }
    return NULL;
}

const ShortOOP* ReferrerList::getFirstDirtyItemPtr() {
    const ShortOOP* pItem;
    const ShortOOP* pLastItem = NULL;
    if (this->hasMultiChunk()) {
        TailNodeIterator iter(this);
        pItem = __getFirstDirtyItemPtr(iter);
        if (pItem != NULL) return pItem;
        pItem = _head._items + (MAX_COUNT_IN_CHUNK - 1);
    } else {
        pItem = lastItemPtr();
    }

    for (; pItem >= _head._items; pItem--) {
        if (pItem[0]->isTrackable_unsafe()) break;
        pLastItem = pItem;
    }
    rt_assert(pLastItem != NULL);
    return pLastItem;
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
    *(ShortOOP*)pLast = item;
}




class ItemFinder {
    ShortOOP _item;
public:
    ItemFinder(ShortOOP item) : _item(item) {}

    bool visit(const ShortOOP* pItem) {
        return *pItem == _item; 
    }
};

const ShortOOP* ReferrerList::getItemPtr(ShortOOP item) {
    ItemFinder finder(item);
    return reverse_iterate(this, &finder);
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

class MatcheItemEraser {
public:
    ShortOOP _item;
    ReferrerList* _list;
    const void* _last_removed;
    MatcheItemEraser(ShortOOP item, ReferrerList* list) : _item(item), _list(list), _last_removed(NULL) {}

    bool visit(const ShortOOP* pItem) {
        if (*pItem == _item) {
            _list->cut_tail_end((ShortOOP*)pItem);
            _last_removed = pItem;
        }
        return false;
    }
};

const void* ReferrerList::removeMatchedItems(ShortOOP item) {
    MatcheItemEraser eraser(item, this);
    reverse_iterate(this, &eraser);
    return eraser._last_removed;
}

class DirtyItemRemover {
public:
    ReferrerList* _list;
    ShortOOP _dirtyStartMark;
    debug_only(bool _end);
    DirtyItemRemover(ReferrerList* list, ShortOOP dirtyStartMark) : _list(list), _dirtyStartMark(dirtyStartMark) {
        debug_only(_end = false;)
    }

    bool visit(const ShortOOP* pItem) {
        debug_only(if (_end) { rt_assert(pItem[0]->isTrackable()); return false; })
        bool end = *pItem == _dirtyStartMark;
        if (end || !pItem[0]->isTrackable_unsafe()) {
            _list->cut_tail_end((ShortOOP*)pItem);
        }
        debug_only(_end = end; return false;)
        return end;
    }
};


void ReferrerList::removeDirtyItems(ShortOOP dirtyStartMark) {
    DirtyItemRemover remover(this, dirtyStartMark);
    reverse_iterate(this, &remover);
    if (false) {
        const void* last_removed = NULL;
        const ShortOOP* pFirstDirty = NULL;
        if (pFirstDirty == NULL) {
            pFirstDirty = this->firstItemPtr();
        }
        const ShortOOP* pItem = lastItemPtr();
        int cnt_removed = 0;
        if (this->hasMultiChunk()) {
            Chunk* chunk = getContainingChunck(pItem);
            do {
                while (pItem != &chunk->_items[MAX_COUNT_IN_CHUNK]) {
                    if (!pItem[0]->isDirtyAnchor()) {
                        this->set_last_item_ptr(pItem);
                        return;
                    }
                    cnt_removed ++;
                    // rtgc_log(rtHeap::in_full_gc, "dirty anchor removed in multi-chunk %d", cnt_removed);
                    pItem ++;
                }
                Chunk* next_chunk = chunk->getNextChunk();
                dealloc_chunk(chunk);
                chunk = next_chunk;
                pItem = chunk->_items;
            } while (pItem != &this->_head._items[0]);
            // rtgc_log(rtHeap::in_full_gc, "dirty chunk overflow %d", cnt_removed);
            pItem = _head._items + (MAX_COUNT_IN_CHUNK - 1);
        } 
        for (; pItem >= _head._items && pItem[0]->isDirtyAnchor(); pItem--) {
            cnt_removed ++;
            // rtgc_log(true, "dirty anchor removed %d", cnt_removed);
        }
        this->set_last_item_ptr(pItem);
    }
    return;
}


void AnchorIterator::initialize(GCObject* obj) {
    if (!obj->mayHaveAnchor()) {
        this->initEmpty();
    }
    else if (!obj->hasMultiRef()) {
        this->initSingleIterator(&obj->getSingleAnchor());
    }
    else {
        ReferrerList* referrers = obj->getAnchorList();
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
