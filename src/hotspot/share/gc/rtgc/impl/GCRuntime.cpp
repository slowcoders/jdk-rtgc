#include "precompiled.hpp"
#include "gc/rtgc/rtgcGlobals.hpp"
#include "GCObject.hpp"
#include "GCRuntime.hpp" 
#include "oops/oop.inline.hpp"
#include "classfile/vmClasses.hpp"
#include "runtime/atomic.hpp"

using namespace RTGC;
namespace RTGC {
    GCRuntime _rtgc;
  //  GarbageProcessor g_garbageProcessor;
}

const static bool USE_TINY_MEM_POOL = true;
const static bool IS_MULTI_LAYER_NODE = false;

int GCNode::incrementRootRefCount() {
    int max_count = survivor_reachable_value - 1;
    rtgc_debug_log(this, "incrementRootRefCount %p(%s) rc=%d\n", this, RTGC::getClassName(this), this->getRootRefCount() + 2);
    rt_assert_f((_flags.rootRefCount & max_count) < max_count/2, PTR_DBG_SIG, PTR_DBG_INFO(this));
    rt_assert_f(!this->isGarbageTrackable(), "wrong ref-count " PTR_DBG_SIG, PTR_DBG_INFO(this));
    return (_flags.rootRefCount += 2);
}

int GCNode::decrementRootRefCount() {
    rt_assert_f(!this->isGarbageTrackable(), "wrong ref-count " PTR_DBG_SIG, PTR_DBG_INFO(this)); 
    rt_assert_f(_flags.rootRefCount > 1, "wrong ref-count " PTR_DBG_SIG, PTR_DBG_INFO(this)); 
    return (_flags.rootRefCount -= 2);
}

bool GCRuntime::detectUnsafeObject(GCObject* erased) {
    if (erased->isUnsafeTrackable() && !erased->isUnstableMarked()) {
        _rtgc.g_pGarbageProcessor->addUnstable(erased);
        return true;
        // GCRuntime::detectGarbages(erased);
    }
    return false;
}

void GCRuntime::connectReferenceLink(
    GCObject* assigned, 
    GCObject* owner 
) {
    fatal("not implemented");
    // assigned->addAnchor(owner);
}

void GCRuntime::disconnectReferenceLink(
    GCObject* erased, 
    GCObject* owner 
) {
    rt_assert(owner->isTrackable_unsafe());
    if (erased->isTrackable_unsafe()) {
        erased->removeReferrer(owner);
    }
}

bool GCRuntime::tryDisconnectReferenceLink(
    GCObject* erased, 
    GCObject* owner 
) {
    return erased->tryRemoveReferrer(owner);
}


void GCRuntime::onAssignRootVariable_internal(GCObject* assigned) {
    rt_assert_f(RTGC::heap_locked_bySelf() ||
         (SafepointSynchronize::is_at_safepoint() && Thread::current()->is_VM_thread()),
         "not locked");

    assigned->incrementRootRefCount();
    // rt_assert(!RTGC::is_debug_pointer(assigned));// || assigned->getRootRefCount() == ZERO_ROOT_REF);
}

void GCRuntime::onAssignRootVariable(GCObject* assigned) {
    if (assigned == nullptr) return;
    bool isPublished = RTGC::lock_if_published(assigned);
    onAssignRootVariable_internal(assigned);
    if (isPublished) RTGC::unlock_heap();
}

void GCRuntime::onEraseRootVariable_internal(GCObject* erased) {
    rt_assert_f(RTGC::heap_locked_bySelf() ||
         (SafepointSynchronize::is_at_safepoint() && Thread::current()->is_VM_thread()),
         "not locked");

    rt_assert_f(!erased->isGarbageTrackable() && erased->isStrongRootReachable(), 
        "wrong ref-count " PTR_DBG_SIG, PTR_DBG_INFO(erased));
        
    if (erased->decrementRootRefCount() <= ZERO_ROOT_REF) {
        detectUnsafeObject(erased);
    }
    rtgc_debug_log(erased, "root erased %p(%s) rc=%d f=%p\n", 
        erased, RTGC::getClassName(erased), erased->getRootRefCount(), (void*)cast_to_oop(erased)->forwardee());
    // rt_assert(!RTGC::is_debug_pointer(erased));// || erased->getRootRefCount() != 2);
}

void GCRuntime::onEraseRootVariable(GCObject* erased) {
    if (erased == nullptr) return;

    bool isPublished = RTGC::lock_if_published(erased);
    onEraseRootVariable_internal(erased);
    if (isPublished) RTGC::unlock_heap();
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
    RTGC::unlock_heap();
}

void GCRuntime::onReplaceRootVariable(
    GCObject* assigned, 
    GCObject* erased 
) {
    if (assigned == erased) return;
    RTGC::lock_heap();
    if (assigned != NULL) onAssignRootVariable_internal(assigned);
    if (erased   != NULL) onEraseRootVariable_internal(erased);
    RTGC::unlock_heap();
}

#if 0
void GCRuntime::replaceMemberVariable(
    GCObject* owner, 
    // not impl // OffsetPointer<GCObject>* pField,
    GCObject* assigned
) {
    rt_assert(owner != nullptr);
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
    RTGC::unlock_heap();
}
#endif 

void GCRuntime::adjustCircuitPoints() {
}

void GCRuntime::dumpDebugInfos() {
    #if GC_DEBUG
    printf("Shorcut: %d, ReferrerList::Chunk: %d\n", 
        _rtgc.g_circuitPool.getAllocatedItemCount(), 0);
    #endif
}

