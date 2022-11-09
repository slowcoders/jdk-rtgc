#include "precompiled.hpp"
#include "oops/klass.inline.hpp"

#include "GCObject.hpp"
#include "GCRuntime.hpp"
#include "../RTGC.hpp"

using namespace RTGC;

static const int LOG_OPT(int function) {
  return LOG_OPTION(RTGC::LOG_GCNODE, function);
}

static void assert_valid_link(oopDesc* link, oopDesc* anchor) {
    assert(link != anchor ||
        (rtHeap::in_full_gc && link->is_gc_marked() && 
         link->forwardee() != NULL && (void*)link->forwardee() != anchor), 
            "recursive link %p\n", link);
}

int GCObject::getReferrerCount() {
    if (!this->isAnchored()) return 0;
    if (!this->hasMultiRef()) return 1;
    ReferrerList* referrers = getReferrerList();
    return referrers->approximated_item_count();
}

void GCObject::addReferrer(GCObject* referrer) {
    /**
     * 주의!) referrer 는 아직, memory 내용이 복사되지 않은 주소일 수 있다.
     */
    // rtgc_debug_log(this, "referrer %p added to %p\n", referrer, this);
    assert_valid_link(cast_to_oop(this), cast_to_oop(referrer));
    if (!isAnchored()) {
        precond(!hasMultiRef());
        this->_refs = _pointer2offset(referrer);
        postcond(referrer == _offset2Object(_refs));
        postcond(!hasMultiRef());
    }
    else {
        ReferrerList* referrers;
        if (!hasMultiRef()) {
            referrers = ReferrerList::allocate();
            referrers->init(*(ShortOOP*)&_refs, referrer);
            // GCObject* front = _offset2Object(_refs);
            // referrers->at(0) = front;
            // referrers->at(1) = referrer;
            _refs = ReferrerList::getIndex(referrers);
            //_refs = ReferrerList::getIndex(referrers);
            setHasMultiRef(true);
        }
        else {
            referrers = getReferrerList();
            referrers->push_back(referrer);
        }
    }
}

bool GCObject::hasReferrer(GCObject* referrer) {
    if (!isAnchored()) return false;

    if (!hasMultiRef()) {
        return _refs == _pointer2offset(referrer);
    }
    else {
        ReferrerList* referrers = getReferrerList();
        return referrers->contains(referrer);
        // int idx = referrers->indexOf(referrer);
        // return idx >= 0;
    }
}


template <bool reallocReferrerList, bool must_exist, bool remove_mutiple_items>
int  GCObject::removeReferrer_impl(GCObject* referrer) {
    precond(referrer != this);
    if (!must_exist && !isAnchored()) return -1;

    assert(isAnchored(), "no referrer %p(%s) in empty %p(%s) \n", 
        referrer, RTGC::getClassName(referrer, true),
        this, RTGC::getClassName(this));
    rtgc_debug_log(this, "removing anchor %p(%s)(gc_m=%d) from %p isMulti=%d tr=%d rc=%d\n", 
            referrer, RTGC::getClassName(referrer), 
            cast_to_oop(referrer)->is_gc_marked(), this, hasMultiRef(), 
            isTrackable(), getRootRefCount());

    if (!hasMultiRef()) {
        if (!must_exist && _refs != _pointer2offset(referrer)) return -1;

        assert(_refs == _pointer2offset(referrer), 
            "referrer %p(%s) != %p in %p(%s) \n", 
            referrer, RTGC::getClassName(referrer),
            _offset2Object(_refs),
            this, RTGC::getClassName(this));
        this->_refs = 0;
        rtgc_debug_log(this, "anchor-list cleared by removeReferrer %p\n", this);
    }
    else {
        ReferrerList* referrers = getReferrerList();
        const void* removed;
        if (remove_mutiple_items) {
            removed = referrers->removeMatchedItems(referrer);
        } else {
            removed = referrers->remove(referrer);
        }
        if (!must_exist && removed == NULL) return -1;

#ifdef ASSERT            
        if (removed == NULL) {
            for (AnchorIterator it(this); it.hasNext(); ) {
                GCObject* anchor = it.next();
                rtgc_log(1, "at %p(%s)\n", anchor, RTGC::getClassName(anchor));
            }
        }
#endif        
        assert(removed != NULL, "referrer %p(%s) is not found in %p(%s) \n", 
            referrer, RTGC::getClassName(referrer),
            this, RTGC::getClassName(this));

        bool first_item_removed = removed == referrers->firstItemPtr();
        if (reallocReferrerList && referrers->isTooSmall()) {
            if (referrers->empty()) {
                precond(!referrers->hasSingleItem());
                this->_refs = 0;
            }
            else {
                precond(referrers->hasSingleItem());
                GCObject* remained = referrers->front();
                this->_refs = _pointer2offset(remained);
            }
            ReferrerList::delete_(referrers);
            setHasMultiRef(false);
        } else {
            postcond(!reallocReferrerList || !referrers->empty());
        }

        if (!first_item_removed) {
            return +1;
        }
    }

    if (this->hasShortcut()) {
		SafeShortcut* shortcut = this->getShortcut();
        shortcut->split(referrer, this);
    } 
    else {
        this->invalidateSafeAnchor();
    }

    return 0;
}

bool GCObject::removeReferrer(GCObject* referrer) {
    return removeReferrer_impl<true, true, false>(referrer) == 0;
}

bool GCObject::removeReferrerWithoutReallocaton(GCObject* referrer) {
    return removeReferrer_impl<false, true, false>(referrer) == 0;
}

bool GCObject::tryRemoveReferrer(GCObject* referrer) {
    return removeReferrer_impl<true, false, false>(referrer) == 0;
}

bool GCObject::removeMatchedReferrers(GCObject* referrer) {
    return removeReferrer_impl<true, false, true>(referrer) != -1;
}

bool GCObject::containsReferrer(GCObject* referrer) {
    precond(referrer != this);
    if (!isAnchored()) return false;

    if (!hasMultiRef()) {
        return _refs == _pointer2offset(referrer);
    }
    else {
        ReferrerList* referrers = getReferrerList();
        return referrers->contains(referrer);
    }
}

void GCObject::clearAnchorList() {
    rtgc_debug_log(this, "all anchor removed from %p isMulti=%d tr=%d rc=%d\n",
        this, hasMultiRef(), isTrackable(), getRootRefCount());
    precond(!this->hasShortcut());
    if (hasMultiRef()) {
        ReferrerList* referrers = getReferrerList();
        ReferrerList::delete_(referrers);
        setHasMultiRef(false);
    }    
    this->_refs = 0;
}

bool GCObject::clearEmptyAnchorList() {
    if (hasMultiRef()) {
        ReferrerList* referrers = getReferrerList();
        if (referrers->empty()) {
            precond(!this->hasShortcut());
            ReferrerList::delete_(referrers);
            setHasMultiRef(false);
            this->_refs = 0;
        }
    }    
    return this->_refs == 0;
}

void GCObject::removeAllAnchors() {
    rtgc_log(LOG_OPT(1), "refList of garbage cleaned %p\n", this);

    if (this->hasShortcut()) {
        precond(this->isTrackable());
        SafeShortcut* shortcut = this->getShortcut();
        shortcut->shrinkTailTo(this->getSafeAnchor());
    }  
    clearAnchorList();
    rtgc_debug_log(this, "anchor-list cleared by removeAllAnchors %p\n", this);
}

GCObject* GCObject::getSingleAnchor() {
    precond(this->isAnchored());
    if (this->hasMultiRef()) return NULL;
    GCObject* front = _offset2Object(_refs);
    return front;
}

GCObject* GCObject::getSafeAnchor() {
    assert(isAnchored(), "no anchors %p(%s)\n", this, RTGC::getClassName(this)); 
    GCObject* front;
    if (hasMultiRef()) {
        ReferrerList* referrers = getReferrerList();
        front = referrers->front();
    }
    else {
        front = _offset2Object(_refs);
    }
    return front;
}

void GCObject::setSafeAnchor(GCObject* anchor) {
    // assert(isAnchored() && SafeShortcut::isValidIndex(this->getShortcutId()), 
    //     "incorrect anchor(%p) for empty obj(%p:%d)", anchor, this, this->getShortcutId());
    precond(!this->getShortcut()->isValid());

    if (hasMultiRef()) {
        ReferrerList* referrers = getReferrerList();
        referrers->replaceFirst(anchor);
    }
    else {
        GCObject* front = _offset2Object(_refs);
        assert(front == anchor, "incorrect safe anchor(%p) for this(%p). it must be (%p)",
            anchor, this, front);
    }
    // this->setShortcutId_unsafe(INVALID_SHORTCUT);
}

SafeShortcut* GCObject::getShortcut() {
    int p_id = this->getShortcutId();
    return SafeShortcut::getPointer(p_id);
}



bool GCObject::visitLinks(LinkVisitor visitor, void* callbackParam) {
    assert(0, "not impl");
    return true;
}


void GCObject::initIterator(AnchorIterator* iterator) {
#if !NEW_GC_UTILS
    if (!isAnchored()) {
        iterator->_current = iterator->_end = nullptr;
    }
    else if (!hasMultiRef()) {
        iterator->_current = (ShortOOP*)(void*)&_refs;//&iterator->_temp;
        iterator->_end = iterator->_current + 1;
    }
    else {
        ReferrerList* referrers = getReferrerList();
        iterator->_current = &referrers->at(0);
        iterator->_end = iterator->_current + referrers->size();
    }
#else 
    if (!isAnchored()) {
        iterator->initEmpty();
    }
    else if (!hasMultiRef()) {
        iterator->initSingleIterator((ShortOOP*)(void*)&_refs);
    }
    else {
        ReferrerList* referrers = getReferrerList();
        iterator->initIterator(referrers);
    }
#endif
}


ReferrerList* GCObject::getReferrerList() {
    precond(hasMultiRef());
    return ReferrerList::getPointer(_refs);
}



void GCObject::invaliateSurvivalPath(GCObject* newTail) {
	if (this->_shortcutId > 0) {
		SafeShortcut* shortcut = this->getShortcut();
        shortcut->split(newTail, this);
    	this->_shortcutId = 0;
	}
}


#if 0
bool GCArray::visitLinks(LinkVisitor visitor, void* callbackParam) {
    fatal("not impl");
    return true;
}


GCObject* GCObject::getLinkInside(SafeShortcut* container) {
    fatal("not impl");
    return nullptr;
}
#endif
