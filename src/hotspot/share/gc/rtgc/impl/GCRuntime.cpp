#include "GCObject.hpp"
#include "gc/rtgc/RTGC.hpp"
#include "GCRuntime.hpp" 
#include "oops/oop.inline.hpp"
#include "classfile/vmClasses.hpp"

using namespace RTGC;
namespace RTGC {
    GCRuntime _rtgc;
  //  GarbageProcessor g_garbageProcessor;
}

const static bool USE_TINY_MEM_POOL = true;
const static bool IS_MULTI_LAYER_NODE = false;

void ReferrerList::init(int initialSize) {
    TinyChunk* chunk = _rtgc.gTinyPool.allocate();;
    *(void**)this = chunk;
    chunk->_capacity = (int)sizeof(TinyChunk) / sizeof(void*) -1;
    chunk->_size = initialSize;
    precond(chunk->_size <= chunk->_capacity);
}


void* DefaultAllocator::alloc(size_t size) {
    if (USE_TINY_MEM_POOL && size <= sizeof(TinyChunk)) {
        return _rtgc.gTinyPool.allocate();
    }
    else {
        void * mem = ::malloc(size);
        postcond(mem != nullptr);
        return mem;
    }
}

void* DefaultAllocator::realloc(void* mem, size_t size) {
    if (USE_TINY_MEM_POOL && _rtgc.gTinyPool.isInside(mem)) {
        if (size <= sizeof(TinyChunk)) {
            return mem;
        }
        void* new_mem = ::malloc(size);
        postcond(mem != nullptr);
        memcpy(new_mem, mem, sizeof(TinyChunk));
        _rtgc.gTinyPool.delete_((TinyChunk*)mem);
        return new_mem;
    }
    else {
        void* mem2 = ::realloc(mem, size);
        postcond(mem != nullptr);
        return mem2;
    }
}

void DefaultAllocator::free(void* mem) {
    if (USE_TINY_MEM_POOL && _rtgc.gTinyPool.isInside(mem)) {
        _rtgc.gTinyPool.delete_((TinyChunk*)mem);
    }
    else {
        ::free(mem);
    }
}

#if GC_DEBUG
int GCRuntime::getTinyChunkCount() {
    return _rtgc.gTinyPool.getAllocatedItemCount();
}

int GCRuntime::getReferrerListCount() {
    return _rtgc.gRefListPool.getAllocatedItemCount();
}
#endif

void GCRuntime::detectUnsafeObject(GCObject* erased) {
    if (erased->isUnsafeTrackable() && !erased->isUnstableMarked()) {
        _rtgc.g_pGarbageProcessor->addUnstable(erased);
        // GCRuntime::detectGarbages(erased);
        //earlyDetectedUnsafeObjects.add(erased);
    }
}

void GCRuntime::connectReferenceLink(
    GCObject* assigned, 
    GCObject* owner 
) {
    assigned->addReferrer(owner);
}

void GCRuntime::disconnectReferenceLink(
    GCObject* erased, 
    GCObject* owner 
) {
    int idx = erased->removeReferrer(owner);
    if (idx == 0) {
        detectUnsafeObject(erased);
    }
}

bool GCRuntime::tryDisconnectReferenceLink(
    GCObject* erased, 
    GCObject* owner 
) {
    int idx = erased->tryRemoveReferrer(owner);
    if (idx == 0) {
        detectUnsafeObject(erased);
    }
    return idx >= 0;
}

void GCRuntime::onAssignRootVariable_internal(GCObject* assigned) {
//    precond(!RTGC::is_debug_pointer(assigned) || assigned->getRootRefCount() == ZERO_ROOT_REF);
    assigned->incrementRootRefCount();
    rtgc_debug_log(assigned, "root assigned %p(%d)\n", assigned, assigned->getRootRefCount());

}

void GCRuntime::onAssignRootVariable(GCObject* assigned) {
    if (assigned == nullptr) return;
    bool isPublished = RTGC::lock_if_published(assigned);
    onAssignRootVariable_internal(assigned);
    RTGC::unlock_heap(isPublished);
}

void GCRuntime::onEraseRootVariable_internal(GCObject* erased) {
    assert(!erased->isGarbageMarked() && erased->isStrongRootReachable(), "wrong ref-count %p(%s) garbage=%d\n", 
        erased, "ss"/*RTGC::getClassName(erased)*/, erased->isGarbageMarked());
    if (erased->decrementRootRefCount() <= ZERO_ROOT_REF) {
        detectUnsafeObject(erased);
    }
    // precond(RTGC::debugOptions[0] == 1 || !RTGC::is_debug_pointer(erased) || erased->getRootRefCount() != ZERO_ROOT_REF);
    // precond(erased->isStrongRootReachable() || 
    //     cast_to_oop(erased)->klass() != vmClasses::Class_klass());
    rtgc_debug_log(erased, "root erased %p(%d)\n", erased, erased->getRootRefCount());
}

void GCRuntime::onEraseRootVariable(GCObject* erased) {
    if (erased == nullptr) return;

    bool isPublished = RTGC::lock_if_published(erased);
    onEraseRootVariable_internal(erased);
    RTGC::unlock_heap(isPublished);
}

void GCRuntime::replaceStaticVariable(
    GCObject** pField,
    GCObject* assigned 
) {
    RTGC::publish_and_lock_heap(assigned, true);
    GCObject* erased = *pField; 
    *pField = assigned;
    if (assigned != erased) {    
        if (assigned != nullptr) {
            onAssignRootVariable_internal(assigned);
        }
        if (erased != nullptr) {
            onEraseRootVariable_internal(erased);
        }
    }
    RTGC::unlock_heap(true);
}

void GCRuntime::onReplaceRootVariable(
    GCObject* assigned, 
    GCObject* erased 
) {
    onAssignRootVariable(assigned);
    onEraseRootVariable(erased);
}

void GCRuntime::replaceMemberVariable(
    GCObject* owner, 
    OffsetPointer<GCObject>* pField,
    GCObject* assigned
) {
    precond(owner != nullptr);
    RTGC::publish_and_lock_heap(assigned, owner->isPublished());
    GCObject* erased = pField->getPointer();
	if (sizeof(*pField) < sizeof(void*)) {
		*(uint32_t*)pField = (assigned == nullptr) ? 0 : _pointer2offset(assigned);
	}
    else {
		*(void**)pField = assigned;
	}
    if (assigned != nullptr && assigned != owner) {
        connectReferenceLink(assigned, owner);
    }
    if (erased != nullptr && erased != owner) {
        disconnectReferenceLink(erased, owner);
    }
    RTGC::unlock_heap(true);
}

#if 0
void GCRuntime::publishInstance(GCObject* obj) {
    if (!obj->markPublished()) return;

    // TODO Thread 로컬로 변경
    SimpleVector<LinkIterator> _traceStack;
    _traceStack.push_back(obj);    
    LinkIterator* it = &_traceStack.back();
    while (true) {
        GCObject* link = it->next();
        if (link == nullptr) {
            _traceStack.pop_back();
            if (_traceStack.empty()) break;
            it = &_traceStack.back();
        }
        else if (link->markPublished()) {
            _traceStack.push_back(link);
            it = &_traceStack.back();
        }
    }    
}
#endif

#if GC_DEBUG
int GCRuntime::getCircuitCount() {
	return 0;//g_shortcutPool.getAllocatedItemCount();
}
#endif

void GCRuntime::adjustShortcutPoints() {
    int allocSize = _rtgc.g_shortcutPool.size();
    allocSize -= INVALID_SHORTCUT + 1;
    if (allocSize <= 0) return;
    //rtgc_log(true, "g_shortcutPool allocSize %d\n", allocSize);
    SafeShortcut* p = SafeShortcut::getPointer(INVALID_SHORTCUT + 1);
    SafeShortcut* end = p + allocSize;
    for (; p < end; p++) {
        if (p->isValid()) {
#ifdef ASSERT
            if (!cast_to_oop((GCObject*)p->anchor())->is_gc_marked()) {
                for (GCObject* node = p->tail(); node != p->anchor(); node = node->getSafeAnchor()) {
                    rtgc_log(1, "node %p g=%d\n", node, node->isGarbageMarked());
                }
            }
#endif            
            GCObject* anchor = RTGC::getForwardee(p->anchor(), "anchor");
            GCObject* tail = RTGC::getForwardee(p->tail(), "tail");
            // rtgc_log(LOG_OPT(10), "adjustShortcutPoints[%d] %p->%p, %p->%p\n", 
            //     p->getIndex(p), (void*)p->anchor(), anchor, (void*)p->tail(), tail);
            p->adjustPointUnsafe(anchor, tail);
        }
    }
}

void GCRuntime::dumpDebugInfos() {
    #if GC_DEBUG
    printf("Circuit: %d, TinyChunk: %d, ReferrerList: %d\n", 
        getCircuitCount(), getTinyChunkCount(), getReferrerListCount());
    #endif
}

