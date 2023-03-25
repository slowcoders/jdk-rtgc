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

static void assert_valid_link(oopDesc* link, oopDesc* anchor) {
    rt_assert_f(link != anchor ||
        (rtHeap::in_full_gc && link->is_gc_marked() && 
         link->forwardee() != NULL && (void*)link->forwardee() != anchor), 
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
        ReferrerList* referrers = getAnchorList();
        front = referrers->front();
    }
    else {
        front = getSingleAnchor();
    }
    return front;
}

void GCNode::setSafeAnchor(GCObject* anchor) {
    // rt_assert_f(this->hasAnchor() && SafeShortcut::isValidIndex(this->getShortcutId()), 
    //     "incorrect anchor(%p) for empty obj(%p:%d)", anchor, this, this->getShortcutId());
    rt_assert(!this->getShortcut()->isValid());

    if (hasMultiRef()) {
        ReferrerList* referrers = getAnchorList();
        referrers->replaceFirst(anchor);
    }
    else {
        rt_assert_f(getSingleAnchor() == anchor, "incorrect safe anchor(%p) for this(%p). it must be (%p)",
            anchor, this, (GCObject*)getSingleAnchor());
    }
}

SafeShortcut* GCNode::getShortcut() const {
	int p_id = this->getShortcutId();
    return SafeShortcut::getPointer(p_id);
}


int GCNode::getAnchorCount() {
    if (!this->mayHaveAnchor()) return 0;
    if (!this->hasMultiRef()) return 1;
    ReferrerList* referrers = this->getAnchorList();
    int count = referrers->approximated_item_count();
    return count;
}

void GCObject::addReferrer(GCObject* referrer) {
    /**
     * 주의!) referrer 는 아직, memory 내용이 복사되지 않은 주소일 수 있다.
     */
    rtgc_debug_log(this, "referrer %p added to %p(acyclic=%d rc=%d refs_=%x)", 
        referrer, this, this->isAcyclic(), this->getRootRefCount(), this->getAnchorCount());

    // rtgc_log(this->klass()->is_acyclic(), 
    //     "referrer %p added to %p(%s)", referrer, this, this->klass()->name()->bytes());
    // rt_assert(this->is_adjusted_trackable());
    rt_assert(RTGC_FAT_OOP || !cast_to_oop(this)->is_gc_marked());
    assert_valid_link(cast_to_oop(this), cast_to_oop(referrer));

    if (RTGC_ENABLE_ACYCLIC_REF_COUNT && this->isAcyclic()) {
        rt_assert_f(cast_to_oop(this)->klass()->is_acyclic(), "wrong acyclic mark %s", RTGC::getClassName(this));
        this->incrementRootRefCount();
        return;
    }

    rt_assert_f(!RTGC_ENABLE_ACYCLIC_REF_COUNT || !cast_to_oop(this)->klass()->is_acyclic(), 
        "wrong acyclic mark %s", RTGC::getClassName(this));

    if (!this->mayHaveAnchor()) {
        this->setSingleAnchor(referrer);
        rt_assert(referrer == this->getSingleAnchor());
    }
    else {
        ReferrerList* referrers;
        if (!this->hasMultiRef()) {
            referrers = ReferrerList::allocate();
            // old_p 가 항상 앞 쪽에
            rt_assert_f(!this->is_adjusted_trackable() ||
                      !referrer->is_adjusted_trackable() || 
                      this->getSingleAnchor()->is_adjusted_trackable(),
                      "mixed dirty anchor %p -> %p (anchored=%p)", 
                      referrer, this, (GCObject*)this->getSingleAnchor());
            referrers->init(this->getSingleAnchor(), referrer);
            this->setAnchorList(referrers);
        }
        else {
            referrers = this->getAnchorList();
            // old_p 가 항상 앞 쪽에
            if (this->isDirtyReferrerPoints()) {
                referrers->add(referrer, referrer->isTrackable_unsafe());
            } else {
                rt_assert_f(!this->is_adjusted_trackable() ||
                      !referrer->is_adjusted_trackable() || 
                      referrers->lastItemPtr()[0]->is_adjusted_trackable(),
                      "mixed dirty anchor %p -> %p (last_anchor=%p)", 
                      referrer, this, (GCObject*)referrers->lastItemPtr()[0]);
                referrers->push_back(referrer);
            }
        }
    }
}

bool GCObject::hasReferrer(GCObject* referrer) {
    if (!this->mayHaveAnchor()) return false;

    if (!this->hasMultiRef()) {
        return this->getSingleAnchor() == referrer;
    }
    else {
        ReferrerList* referrers = this->getAnchorList();
        return referrers->contains(referrer);
    }
}


template <bool reallocReferrerList, bool must_exist, bool remove_mutiple_items>
int  GCObject::removeReferrer_impl(GCObject* referrer) {
    rt_assert(referrer != this);
    rt_assert(this->is_adjusted_trackable());

    rtgc_debug_log(this, "removing anchor %p(%s)(gc_m=%d) from acyclic=%d " PTR_DBG_SIG, 
            referrer, RTGC::getClassName(referrer), 
            cast_to_oop(referrer)->is_gc_marked(), this->isAcyclic(), PTR_DBG_INFO(this)); 

    if (RTGC_ENABLE_ACYCLIC_REF_COUNT && this->isAcyclic()) {
        rt_assert_f(cast_to_oop(this)->klass()->is_acyclic(), "wrong acyclic mark %s", RTGC::getClassName(this));
        return this->decrementRootRefCount();
    }

    rt_assert_f(!RTGC_ENABLE_ACYCLIC_REF_COUNT || !cast_to_oop(this)->klass()->is_acyclic(), 
        "wrong acyclic mark %s", RTGC::getClassName(this));

    if (!must_exist && !this->mayHaveAnchor()) return -1;

    rt_assert_f(this->mayHaveAnchor(), "no referrer %p(%s) in empty %p(%s)", 
        referrer, RTGC::getClassName(referrer, true),
        this, RTGC::getClassName(this));

    if (!this->hasMultiRef()) {
        if (!must_exist &&this->getSingleAnchor() != referrer) return -1;

        rt_assert_f(this->getSingleAnchor() == referrer, 
            "referrer %p(%s) != %p in %p(%s)", 
            referrer, RTGC::getClassName(referrer),
            (GCObject*)this->getSingleAnchor(),
            this, RTGC::getClassName(this));
        this->removeSingleAnchor();
        rtgc_debug_log(this, "anchor-list cleared by removeReferrer %p", this);
    }
    else {
        ReferrerList* referrers = this->getAnchorList();
        const void* removed;
        if (remove_mutiple_items) {
            removed = referrers->removeMatchedItems(referrer);
        } else {
            removed = referrers->remove(referrer);
        }

        if (!must_exist && removed == NULL) return -1;

#ifdef ASSERT            
        if (removed == NULL) {
            rtgc_debug_log(this, "anchor not found %p(%s) -> %p(%s)", 
                referrer, RTGC::getClassName(referrer), this, RTGC::getClassName(this));
            for (AnchorIterator it(this); it.hasNext(); ) {
                GCObject* anchor = it.next();
                rtgc_log(1, "at %p(%s)", anchor, RTGC::getClassName(anchor));
            }
        }
#endif        
        rt_assert_f(removed != NULL, "referrer %p(%s) is not found in %p(%s)", 
            referrer, RTGC::getClassName(referrer),
            this, RTGC::getClassName(this));

        bool first_item_removed = removed == referrers->firstItemPtr();
        if (reallocReferrerList && !this->isAnchorListLocked() && referrers->isTooSmall()) {
            this->setHasMultiRef(false);
            if (referrers->empty()) {
                this->removeSingleAnchor();
            } else {
                this->setSingleAnchor(referrers->front());
            }
            ReferrerList::deleteSingleChunkList(referrers);
        } else {
            rt_assert(!reallocReferrerList || !referrers->empty());
        }

        if (!first_item_removed) {
            return +1;
        }
    }

    if (!this->hasSafeAnchor()) {
        return this->hasAnchor() ? 1 : 0; 
    }

    if (this->hasShortcut()) {
		SafeShortcut* shortcut = this->getShortcut();
        shortcut->split(referrer, this);
    } 
    else {
        rt_assert(this->hasSafeAnchor());
        this->invalidateSafeAnchor();
    }

    return 0;
}

void GCObject::removeReferrer(GCObject* referrer) {
    if (removeReferrer_impl<true, true, false>(referrer) == 0) {
        GCRuntime::detectUnsafeObject(this);
    }
}

void GCObject::removeReferrerWithoutReallocaton(GCObject* referrer) {
    if (removeReferrer_impl<false, true, false>(referrer) == 0) {
        // GCRuntime::detectUnsafeObject(this);
    }
}

bool GCObject::tryRemoveReferrer(GCObject* referrer) {
    int res = removeReferrer_impl<true, false, false>(referrer);
    if (res == 0) {
        GCRuntime::detectUnsafeObject(this);
    }
    return res != -1;
}

bool GCObject::removeMatchedReferrers(GCObject* referrer) {
    rt_assert_f(!RTGC_ENABLE_ACYCLIC_REF_COUNT, "Should not reach here in Acylic support mode");
    return removeReferrer_impl<true, false, true>(referrer) == 0;
}

bool GCObject::containsReferrer(GCObject* referrer) {
    rt_assert(referrer != this);
    if (!this->mayHaveAnchor()) return false;

    if (!this->hasMultiRef()) {
        return this->getSingleAnchor() == referrer;
    }
    else {
        ReferrerList* referrers = this->getAnchorList();
        return referrers->contains(referrer);
    }
}

void GCObject::invalidateAnchorList_unsafe() {
    this->setHasMultiRef(false);
	this->removeSingleAnchor();
}

void GCObject::removeDirtyAnchors() {
    if (!this->hasMultiRef()) {
        GCObject* anchor = this->getSafeAnchor();
        if (anchor->isDirtyAnchor()) {
            this->removeSingleAnchor();    
        }
    }
    else {
        ReferrerList* referrers = this->getAnchorList();
        referrers->removeDirtyItems();
        clearEmptyAnchorList();
    }
    rtgc_log(this->getAnchorCount() == 0, "removed dirty anchors from " PTR_DBG_SIG, PTR_DBG_INFO(this));
}

void GCObject::clearAnchorList() {
    rtgc_debug_log(this, "clearAnchorList from " PTR_DBG_SIG, PTR_DBG_INFO(this));
    rt_assert(!this->hasShortcut());
    if (this->hasMultiRef()) {
        ReferrerList* referrers = this->getAnchorList();
        ReferrerList::delete_(referrers);
        this->setHasMultiRef(false);
    }
    this->removeSingleAnchor();    
}

bool GCObject::clearEmptyAnchorList() {
    if (!this->hasMultiRef()) {
        return !this->mayHaveAnchor();
    }

    ReferrerList* referrers = this->getAnchorList();
    int cntAnchor = referrers->approximated_item_count();
    if (cntAnchor <= (this->isAnchorListLocked() ? 0 : 1)) {
        rt_assert(!this->hasShortcut());
        this->setHasMultiRef(false);
        if (referrers->empty()) {
            this->removeSingleAnchor();
            rtgc_log(true, "all anchor removed from %p", this);
        } else {
            this->setSingleAnchor(referrers->front());
        }
        ReferrerList::deleteSingleChunkList(referrers);
    }
    return cntAnchor == 0;
}

void GCObject::removeAllAnchors() {
    rtgc_log(LOG_OPT(1), "refList of garbage cleaned %p", this);

    if (this->hasShortcut()) {
        SafeShortcut* shortcut = this->getShortcut();
        shortcut->shrinkTailTo(this->getSafeAnchor());
    }  
    clearAnchorList();
    rtgc_debug_log(this, "anchor-list cleared by removeAllAnchors %p", this);
}



void GCObject::invaliateSurvivalPath(GCObject* newTail) {
    if (this->getShortcutId() > 0) {
		SafeShortcut* shortcut = this->getShortcut();
        shortcut->split(newTail, this);
    	this->setShortcutId_unsafe(0);
    }
}

