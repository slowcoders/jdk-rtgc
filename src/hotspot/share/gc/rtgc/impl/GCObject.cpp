#include "precompiled.hpp"
#include "oops/klass.inline.hpp"

#include "GCObject.hpp"
#include "GCRuntime.hpp"
#include "../rtgcGlobals.hpp"

using namespace RTGC;

void* GCNode::g_trackable_heap_start = 0;
bool GCNode::in_progress_adjust_pointers = false;

static const int LOG_OPT(int function) {
  return LOG_OPTION(RTGC::LOG_GCNODE, function);
}

void GCObject::removeReferrer(GCObject* referrer) {
    GCRuntime::onEraseRootVariable_internal(this);
}

// return true if safe_anchor removed;
void GCObject::removeReferrerWithoutReallocaton(GCObject* referrer) {
    GCRuntime::onEraseRootVariable_internal(this);
}


#if 0
static void assert_valid_link(oopDesc* link, oopDesc* anchor) {
    rt_assert_f(link != anchor ||
        (rtHeap::in_full_gc && link->forwardee() != NULL && (void*)link->forwardee() != anchor), 
            "recursive link %p", link);
}

ReferrerList* GCNode::getAnchorList() const {
	rt_assert_f(_hasMultiRef, "_refs %x", _refs);
    return ReferrerList::getPointer(_refs & ANCHOR_LIST_INDEX_MASK);
}

bool GCNode::hasAnchor() const {
    if (_refs == 0) return false;
    return _refs > 0 || !_hasMultiRef || !getAnchorList()->empty();
}

void GCNode::setAnchorList(ReferrerList* anchors) {
    rt_assert(!_hasMultiRef);
    _refs = ReferrerList::getIndex(anchors);
    _hasMultiRef = true;
}

GCObject* GCNode::getSafeAnchor() const {
	rt_assert_f(hasAnchor(), "no anchors %p(%s)", this, RTGC::getClassName(this)); 
    GCObject* front;
    if (hasMultiRef()) {
        ReferrerList* anchors = getAnchorList();
        front = anchors->front();
    }
    else {
        front = getSingleAnchor();
    }
    return front;
}

void GCNode::setSafeAnchor(GCObject* anchor) {
    // rt_assert_f(this->hasAnchor() && CircuitNode::isValidIndex(this->getCircuitId()), 
    //     "incorrect anchor(%p) for empty obj(%p:%d)", anchor, this, this->getCircuitId());
    rt_assert(!this->getCircuit()->isValid());

    if (hasMultiRef()) {
        ReferrerList* anchors = getAnchorList();
        anchors->replaceFirst(anchor);
    }
    else {
        rt_assert_f(getSingleAnchor() == anchor, "incorrect safe anchor(%p) for this(%p). it must be (%p)",
            anchor, this, (GCObject*)getSingleAnchor());
    }
}


int GCNode::getAnchorCount() {
    if (!this->mayHaveAnchor()) return 0;
    if (!this->hasMultiRef()) return 1;
    ReferrerList* anchors = this->getAnchorList();
    int count = anchors->approximated_item_count();
    return count;
}

static void* __getAdjustedAnchorPoint(GCObject* obj) {
    if (!GCNode::in_progress_adjust_pointers) return obj;
    oop new_p = cast_to_oop(obj)->forwardee();
    if (new_p == NULL) return obj;
    return to_obj(new_p);
}
#endif

#if 0
template <bool tenuredSelf, bool tenuredAnchor>
void GCObject::addAnchor(GCObject* anchor) {
    /**
     * 주의!) anchor 는 아직, memory 내용이 복사되지 않은 주소일 수 있다.
     */
#ifdef ASSERT    
    if (RTGC::is_debug_pointer(this)) {// || 
    // if (!rtHeap::in_full_gc && RTGC::is_debug_pointer(anchor)) {
        rtgc_log(1, "anchor %p added to %p(acyclic=%d rc=%d refs_=%x)", 
                anchor, this, this->isAcyclic(), this->getRootRefCount(), this->getAnchorCount());
    }
#endif

    rt_assert_f(tenuredSelf ? this->is_adjusted_trackable() : !this->isTrackable_unsafe(), 
            "bad anchor %p added to %p/%p (in_progress_adjust_pointers=%d rc=%d refs_=%x)", 
            anchor, this, __getAdjustedAnchorPoint(this), in_progress_adjust_pointers, this->getRootRefCount(), this->getAnchorCount());

    assert_valid_link(cast_to_oop(this), cast_to_oop(anchor));

    if (!this->mayHaveAnchor()) {
        this->setSingleAnchor(anchor);
        rt_assert(anchor == this->getSingleAnchor());
    }
    else {
        ReferrerList* anchors;
        if (!this->hasMultiRef()) {
            anchors = ReferrerList::allocate();
            anchors->init(this->getSingleAnchor(), anchor);
            this->setAnchorList(anchors);
            rt_assert_f(getAnchorList()->firstItemPtr()->getOffset() != 0,
                "anchors %p %x: %p %x", 
                anchors, anchors->firstItemPtr()->getOffset(),
                getAnchorList(), getAnchorList()->firstItemPtr()->getOffset());
        }
        else {
            anchors = this->getAnchorList();
            anchors->push_back(anchor);
        }
    }
}

void GCObject::addTrackableAnchor(GCObject* anchor) {
    this->addAnchor<true, true>(anchor);
}

void GCObject::addTemporalAnchor(GCObject* anchor) {
    this->addAnchor<false, false>(anchor);
    // rtgc_debug_log(this, "addTemporalAnchor %p -> " PTR_DBG_SIG, anchor, PTR_DBG_INFO(this));
}

bool GCObject::addDirtyAnchor(GCObject* anchor) {
    rt_assert_f(anchor->hasAnchor(), "dirty anchor must have dirty anchor or young root " PTR_DBG_SIG "anchor: "
        PTR_DBG_SIG, PTR_DBG_INFO(this), PTR_DBG_INFO(anchor));
    bool was_clean = !this->isDirtyReferrerPoints();
    // rtgc_debug_log(this, "addDirtyAnchor %p -> %p(c=%d/mluti=%d/y-r=%d)", 
    //     anchor, this, was_clean, this->hasMultiRef(), anchor->isYoungRoot());
    if (was_clean) {
      if (this->hasMultiRef()) {
        ReferrerList* anchors = getAnchorList();
        // add dummy anchor for detect dirty anchor start
        anchors->add(this);
      }
      this->markDirtyReferrerPoints();
    }
    this->addAnchor<true, false>(anchor);
    return was_clean;
}

void GCObject::removeDirtyAnchors() {
    if (!this->isDirtyReferrerPoints()) return;

    this->unmarkDirtyReferrerPoints();
    // rtgc_debug_log(this, "removing dirty anchors from " PTR_DBG_SIG, PTR_DBG_INFO(this));
    rt_assert(this->hasAnchor());
    rt_assert(!this->hasSafeAnchor() || this->getSafeAnchor()->isTrackable());
    if (!this->hasMultiRef()) {
        rt_assert (this->getSingleAnchor()->isDirtyAnchor());
        this->removeSingleAnchor();    
    }
    else {
        ReferrerList* anchors = this->getAnchorList();
        anchors->removeDirtyItems(this);
        clearEmptyAnchorList();
    }
}

bool GCObject::hasReferrer(GCObject* anchor) {
    if (!this->mayHaveAnchor()) return false;

    if (!this->hasMultiRef()) {
        return this->getSingleAnchor() == anchor;
    }
    else {
        ReferrerList* anchors = this->getAnchorList();
        return anchors->contains(anchor);
    }
}


template <bool reallocReferrerList, bool must_exist, bool remove_mutiple_items>
int  GCObject::removeReferrer_impl(GCObject* anchor) {
    rt_assert(anchor != this);
    rt_assert(this->is_adjusted_trackable());

#ifdef ASSERT
    if (RTGC::is_debug_pointer(this)) {// || 
    // if (!rtHeap::in_full_gc && RTGC::is_debug_pointer(anchor)) {
        rtgc_log(true, "removing anchor %p(%s) from %p(%s acyclic=%d)", 
            anchor, RTGC::getClassName(anchor), 
            this, RTGC::getClassName(this), this->isAcyclic()); 
    }
#endif

    if (!must_exist && !this->mayHaveAnchor()) return -1;

    rt_assert_f(this->mayHaveAnchor(), "no anchor %p(%s) in empty %p(%s)", 
        anchor, RTGC::getClassName(anchor, true),
        this, RTGC::getClassName(this));

    if (!this->hasMultiRef()) {
        if (!must_exist &&this->getSingleAnchor() != anchor) return -1;

        rt_assert_f(this->getSingleAnchor() == anchor, 
            "anchor %p(%s) != %p in %p(%s)", 
            anchor, RTGC::getClassName(anchor),
            (GCObject*)this->getSingleAnchor(),
            this, RTGC::getClassName(this));
        this->removeSingleAnchor();
        rtgc_debug_log(this, "anchor-list cleared by removeReferrer %p", this);
    }
    else {
        ReferrerList* anchors = this->getAnchorList();
        const void* removed;
        if (remove_mutiple_items) {
            removed = anchors->removeMatchedItems(anchor);
        } else {
            removed = anchors->remove(anchor);
        }

        if (!must_exist && removed == NULL) return -1;

#ifdef ASSERT            
        if (removed == NULL) {
            rtgc_debug_log(this, "anchor not found %p(%s) -> %p(%s)", 
                anchor, RTGC::getClassName(anchor), this, RTGC::getClassName(this));
            for (AnchorIterator it(this); it.hasNext(); ) {
                GCObject* anchor = it.next();
                rtgc_log(1, "at %p(%s)", anchor, RTGC::getClassName(anchor));
            }
        }
#endif        
        rt_assert_f(removed != NULL, "anchor %p(%s) is not found in %p(%s)", 
            anchor, RTGC::getClassName(anchor),
            this, RTGC::getClassName(this));

        bool first_item_removed = removed == anchors->firstItemPtr();
        if (reallocReferrerList && !this->isAnchorListLocked() && anchors->isTooSmall()) {
            this->setHasMultiRef(false);
            if (anchors->empty()) {
                this->removeSingleAnchor();
            } else {
                this->setSingleAnchor(anchors->front());
            }
            ReferrerList::deleteSingleChunkList(anchors);
        } else {
            rt_assert(!reallocReferrerList || !anchors->empty());
        }

        if (!first_item_removed) {
            return +1;
        }
    }

    if (!this->hasSafeAnchor()) {
        return this->hasAnchor() ? 1 : 0; 
    }

    if (this->hasCircuit()) {
		CircuitNode* circuit = this->getCircuit();
        circuit->split(anchor, this);
    } 
    else {
        rt_assert(this->hasSafeAnchor());
        this->invalidateSafeAnchor();
    }

    return 0;
}

void GCObject::removeReferrer(GCObject* anchor) {
    if (removeReferrer_impl<true, true, false>(anchor) == 0) {
        GCRuntime::detectUnsafeObject(this);
    }
}

void GCObject::removeReferrerWithoutReallocaton(GCObject* anchor) {
    if (removeReferrer_impl<false, true, false>(anchor) == 0) {
        // GCRuntime::detectUnsafeObject(this);
    }
}

bool GCObject::tryRemoveReferrer(GCObject* anchor) {
    int res = removeReferrer_impl<true, false, false>(anchor);
    if (res == 0) {
        GCRuntime::detectUnsafeObject(this);
    }
    return res != -1;
}

bool GCObject::removeMatchedReferrers(GCObject* anchor) {
    rt_assert_f(!RTGC_ENABLE_ACYCLIC_REF_COUNT, "Should not reach here in Acylic support mode");
    return removeReferrer_impl<true, false, true>(anchor) == 0;
}

bool GCObject::containsReferrer(GCObject* anchor) {
    rt_assert(anchor != this);
    if (!this->mayHaveAnchor()) return false;

    if (!this->hasMultiRef()) {
        return this->getSingleAnchor() == anchor;
    }
    else {
        ReferrerList* anchors = this->getAnchorList();
        return anchors->contains(anchor);
    }
}

void GCObject::invalidateAnchorList_unsafe() {
    this->setHasMultiRef(false);
	this->removeSingleAnchor();
}



void GCObject::clearAnchorList() {
    // rtgc_debug_log(this, "clearAnchorList from " PTR_DBG_SIG, PTR_DBG_INFO(this));
    rt_assert(!this->hasCircuit());
    if (this->hasMultiRef()) {
        ReferrerList* anchors = this->getAnchorList();
        ReferrerList::delete_(anchors);
        this->setHasMultiRef(false);
    }
    this->removeSingleAnchor();    
}

bool GCObject::clearEmptyAnchorList() {
    if (!this->hasMultiRef()) {
        return !this->mayHaveAnchor();
    }

    ReferrerList* anchors = this->getAnchorList();
    int cntAnchor = anchors->approximated_item_count();
    if (cntAnchor <= (this->isAnchorListLocked() ? 0 : 1)) {
        this->setHasMultiRef(false);
        if (anchors->empty()) {
            rt_assert_f(!this->hasCircuit(), " circuit attached %p %d", this, this->getCircuitId());
            this->removeSingleAnchor();
            // rtgc_log(rtHeap::in_full_gc, "all anchor removed from %p", this);
        } else {
            this->setSingleAnchor(anchors->front());
        }
        ReferrerList::deleteSingleChunkList(anchors);
    }
    return cntAnchor == 0;
}

void GCObject::removeAllAnchors() {
    rtgc_log(LOG_OPT(1), "refList of garbage cleaned %p", this);

    if (this->hasCircuit()) {
        CircuitNode* circuit = this->getCircuit();
        circuit->shrinkTailTo(this->getSafeAnchor());
    }  
    clearAnchorList();
    rtgc_debug_log(this, "anchor-list cleared by removeAllAnchors %p", this);
}



void GCObject::invaliateSurvivalPath(GCObject* newTail) {
    if (this->getCircuitId() > 0) {
		CircuitNode* circuit = this->getCircuit();
        circuit->split(newTail, this);
    	this->setCircuitId_unsafe(0);
    }
}

#endif