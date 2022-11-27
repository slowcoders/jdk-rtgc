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
    assert(!this->isGarbageMarked(), "wrong ref-count " PTR_DBG_SIG, PTR_DBG_INFO(this));
    return (_flags.rootRefCount += 2);
}

int GCNode::decrementRootRefCount() {
    assert(!this->isGarbageMarked(), "wrong ref-count " PTR_DBG_SIG, PTR_DBG_INFO(this)); 
    assert(_flags.rootRefCount > 1, "wrong ref-count " PTR_DBG_SIG, PTR_DBG_INFO(this)); 
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
    assigned->addReferrer(owner);
}

void GCRuntime::disconnectReferenceLink(
    GCObject* erased, 
    GCObject* owner 
) {
    erased->removeReferrer(owner);
}

bool GCRuntime::tryDisconnectReferenceLink(
    GCObject* erased, 
    GCObject* owner 
) {
    return erased->tryRemoveReferrer(owner);
}


void GCRuntime::onAssignRootVariable_internal(GCObject* assigned) {
    assert(RTGC::heap_locked_bySelf() ||
         (SafepointSynchronize::is_at_safepoint() && Thread::current()->is_VM_thread()),
         "not locked");

    assigned->incrementRootRefCount();
    rtgc_debug_log(assigned, "root assigned %p(%s) rc=%d\n", assigned, RTGC::getClassName(assigned), assigned->getRootRefCount());
    // postcond(!RTGC::is_debug_pointer(assigned));// || assigned->getRootRefCount() == ZERO_ROOT_REF);
}

void GCRuntime::onAssignRootVariable(GCObject* assigned) {
    if (assigned == nullptr) return;
    bool isPublished = RTGC::lock_if_published(assigned);
    onAssignRootVariable_internal(assigned);
    RTGC::unlock_heap(isPublished);
}

void GCRuntime::onEraseRootVariable_internal(GCObject* erased) {
    assert(RTGC::heap_locked_bySelf() ||
         (SafepointSynchronize::is_at_safepoint() && Thread::current()->is_VM_thread()),
         "not locked");

    assert(!erased->isGarbageMarked() && erased->isStrongRootReachable(), 
        "wrong ref-count %p rc=%d garbage=%d\n", 
        erased, erased->getRootRefCount(), erased->isGarbageMarked());
    if (erased->decrementRootRefCount() <= ZERO_ROOT_REF) {
        detectUnsafeObject(erased);
    }
    rtgc_debug_log(erased, "root erased %p(%s) rc=%d\n", erased, RTGC::getClassName(erased), erased->getRootRefCount());
    // precond(!RTGC::is_debug_pointer(erased));// || erased->getRootRefCount() != 2);
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
    if (assigned == erased) return;
    RTGC::lock_heap();
    if (assigned != NULL) onAssignRootVariable_internal(assigned);
    if (erased   != NULL) onEraseRootVariable_internal(erased);
    RTGC::unlock_heap(true);
}

#if 0
void GCRuntime::replaceMemberVariable(
    GCObject* owner, 
    // not impl // OffsetPointer<GCObject>* pField,
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
    printf("Shorcut: %d, ReferrerList::Chunk: %d\n", 
        _rtgc.g_shortcutPool.getAllocatedItemCount(), ReferrerList::getAllocatedItemCount());
    #endif
}

