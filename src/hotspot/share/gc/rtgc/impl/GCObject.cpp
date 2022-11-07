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
    return referrers->size();
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
            referrers = _rtgc.gRefListPool.allocate();
            referrers->init(2);
            GCObject* front = _offset2Object(_refs);
            referrers->at(0) = front;
            referrers->at(1) = referrer;
            _refs = _rtgc.gRefListPool.getIndex(referrers);
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
        int idx = referrers->indexOf(referrer);
        return idx >= 0;
    }
}


template <bool reallocReferrerList>
int GCObject::removeReferrer_impl(GCObject* referrer) {
    precond(referrer != this);
    assert(isAnchored(), "no referrer %p(%s) in empty %p(%s) \n", 
        referrer, RTGC::getClassName(referrer, true),
        this, RTGC::getClassName(this));
    rtgc_debug_log(this, "removing anchor %p(%s)(gc_m=%d) from %p isMulti=%d tr=%d rc=%d\n", 
            referrer, RTGC::getClassName(referrer), 
            cast_to_oop(referrer)->is_gc_marked(), this, hasMultiRef(), 
            isTrackable(), getRootRefCount());

    if (!hasMultiRef()) {
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
        int idx = referrers->indexOf(referrer);
        rtgc_log(idx < 0, "referrer %p(%s) is not found in %p(%s) \n", 
            referrer, RTGC::getClassName(referrer),
            this, RTGC::getClassName(this));
#ifdef ASSERT            
        if (idx < 0) {
            for (int i = 0; i < referrers->size(); i ++) {
                GCObject* anchor = referrers->at(i);
                rtgc_log(1, "at[%d] %p(%s)\n", i, anchor, RTGC::getClassName(anchor));
            }
        }
#endif        
        precond(idx >= 0);
        if (reallocReferrerList && referrers->size() == 2) {
            GCObject* remained = referrers->at(1 - idx);
            _rtgc.gRefListPool.delete_(referrers);
            this->_refs = _pointer2offset(remained);
            setHasMultiRef(false);
        }
        else {
            referrers->removeFast(idx);
        }
        if (idx != 0) {
            return idx;
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

int GCObject::removeReferrer(GCObject* referrer) {
    return removeReferrer_impl<true>(referrer);
}

int GCObject::removeReferrerWithoutReallocaton(GCObject* referrer) {
    return removeReferrer_impl<false>(referrer);
}

int GCObject::tryRemoveReferrer(GCObject* referrer) {
    precond(referrer != this);
    if (!isAnchored()) return -1;

    if (!hasMultiRef()) {
        if (_refs != _pointer2offset(referrer)) return -1;
        this->_refs = 0;
        rtgc_debug_log(this, "anchor-list cleared by tryRemoveReferrer %p\n", this);
    }
    else {
        ReferrerList* referrers = getReferrerList();
        int idx = referrers->indexOf(referrer);
        if (idx < 0) return idx;

        precond(idx >= 0);
        if (referrers->size() == 2) {
            GCObject* remained = referrers->at(1 - idx);
            _rtgc.gRefListPool.delete_(referrers);
            this->_refs = _pointer2offset(remained);
            setHasMultiRef(false);
        }
        else {
            referrers->removeFast(idx);
        }
        if (idx != 0) {
            return idx;
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

bool GCObject::containsReferrer(GCObject* referrer) {
    precond(referrer != this);
    if (!isAnchored()) return false;

    if (!hasMultiRef()) {
        return _refs == _pointer2offset(referrer);
    }
    else {
        ReferrerList* referrers = getReferrerList();
        int idx = referrers->indexOf(referrer);
        return idx >= 0;
    }
}

void GCObject::clearAnchorList() {
    rtgc_debug_log(this, "all anchor removed from %p isMulti=%d tr=%d rc=%d\n",
        this, hasMultiRef(), isTrackable(), getRootRefCount());
    precond(!this->hasShortcut());
    if (hasMultiRef()) {
        ReferrerList* referrers = getReferrerList();
        _rtgc.gRefListPool.delete_(referrers);
        setHasMultiRef(false);
    }    
    this->_refs = 0;
}

bool GCObject::clearEmptyAnchorList() {
    if (hasMultiRef()) {
        ReferrerList* referrers = getReferrerList();
        if (referrers->size() == 0) {
            precond(!this->hasShortcut());
            _rtgc.gRefListPool.delete_(referrers);
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

template<bool removeDirty>
static bool is_match(GCObject* node, GCObject* referrer) {
    if (removeDirty) {
        return node->isDirtyReferrerPoints();
    } else {
        return node == referrer;
    }
}

template<bool removeDirty>
static bool remove_matched_referrers(GCObject* node, GCObject* referrer) {
    if (!node->hasMultiRef()) {
        if (node->_refs == 0) return false;
        if (removeDirty) {
            if (!node->getSingleAnchor()->isDirtyReferrerPoints()) return false;
        }
        else {
            if (node->_refs != _pointer2offset(referrer)) return false;
        }
        node->_refs = 0;
        rtgc_debug_log(node, "anchor-list cleared by removeMatchedReferrers %p rc=%d tr=%d\n", 
            node, node->getRootRefCount(), node->isTrackable());
        if (node->hasShortcut()) {
            node->getShortcut()->split(referrer, node);
        } else {
            node->invalidateSafeAnchor();
        }
        postcond(!node->hasSafeAnchor());
        return true;
    }
    else {
        ReferrerList* referrers = _rtgc.gRefListPool.getPointer(node->_refs);
        if (node->hasSafeAnchor() && is_match<removeDirty>(referrers->at(0), referrer)) {
            if (node->hasShortcut()) {
                node->getShortcut()->split(referrer, node);
            } else {
                node->invalidateSafeAnchor();
            }
            postcond(!node->hasSafeAnchor());
        }
        if (removeDirty) {
            ReferrerList* referrers = node->getReferrerList();
            for (int i = referrers->size(); --i >= 0; ) {
                if (referrers->at(i)->isDirtyReferrerPoints()) {
                    referrers->removeFast(i);
                }
            }
        }
        else if (!referrers->removeMatchedItems(referrer)) return false;

        if (referrers->size() <= 1) {
            if (referrers->size() == 0) {
                _rtgc.gRefListPool.delete_(referrers);
                node->_refs = 0;
                postcond(!node->hasSafeAnchor());
                rtgc_debug_log(node, "anchor-list cleared by removeMatchedReferrers multi %p\n", node);
            }
            else {
                GCObject* remained = referrers->at(0);
                _rtgc.gRefListPool.delete_(referrers);
                node->_refs = _pointer2offset(remained);
            }
            node->setHasMultiRef(false);
        }
        return true;
    }
    return false;
}

bool GCObject::removeMatchedReferrers(GCObject* referrer) {
    return remove_matched_referrers<false>(this, referrer);
}

void GCObject::removeBrokenAnchors() {
    remove_matched_referrers<true>(this, NULL);
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
        int idx = referrers->indexOf(anchor);
        assert(idx >= 0, "incorrect anchor %p(%s) for this %p(%s)",
            anchor, RTGC::getClassName(anchor), this, RTGC::getClassName(this));
        if (idx != 0) {
            GCObject* tmp = referrers->at(0);
            referrers->at(0) = anchor;
            referrers->at(idx) = tmp;
        }
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
}


ReferrerList* GCObject::getReferrerList() {
    precond(hasMultiRef());
    return _rtgc.gRefListPool.getPointer(_refs);
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
