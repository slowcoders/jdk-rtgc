#include "GCObject.hpp"
#include "gc/rtgc/RTGC.hpp"
#include "GCRuntime.hpp" 


using namespace RTGC;
namespace RTGC {
    GCRuntime _rtgc;
    volatile int g_mv_lock = (0);
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
    assert(0, "ghghghgh");
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
    return _rtgc.gTinyPool.getAllocationCount();
}

int GCRuntime::getReferrerListCount() {
    return _rtgc.gRefListPool.getAllocationCount();
}
#endif

void GCRuntime::detectUnsafeObject(GCObject* erased) {
    if (erased->isUnsafe()) {
        GCRuntime::detectGarbages(erased);
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

void GCRuntime::onAssignRootVariable_internal(GCObject* assigned) {
    assigned->incrementRootRefCount();
}

void GCRuntime::onAssignRootVariable(GCObject* assigned) {
    if (assigned == nullptr) return;
    bool isPublished = RTGC::lock_if_published(assigned);
    onAssignRootVariable_internal(assigned);
    RTGC::unlock_heap(isPublished);
}

void GCRuntime::onEraseRootVariable_internal(GCObject* erased) {
    if (erased->decrementRootRefCount() <= ZERO_ROOT_REF) {
        detectUnsafeObject(erased);
    }
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
		*(uint32_t*)pField = (assigned == nullptr) ? 0 : _pointer2offset(assigned, 0);
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

void GCRuntime::detectGarbages(GCObject* unsafeObj) {
    assert(0, "not impl");
    // g_garbageProcessor.scanGarbages(unsafeObj);
}

#if GC_DEBUG
int GCRuntime::getCircuitCount() {
	return 0;//g_shortcutPool.getAllocationCount();
}
#endif

void GCRuntime::dumpDebugInfos() {
    #if GC_DEBUG
    printf("Circuit: %d, TinyChunk: %d, ReferrerList: %d\n", 
        getCircuitCount(), getTinyChunkCount(), getReferrerListCount());
    #endif
}

