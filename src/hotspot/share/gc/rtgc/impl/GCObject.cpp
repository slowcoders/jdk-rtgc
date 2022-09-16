#include "precompiled.hpp"
#include "oops/klass.inline.hpp"

#include "GCObject.hpp"
#include "GCRuntime.hpp"
#include "../RTGC.hpp"

using namespace RTGC;

static const int LOG_OPT(int function) {
  return LOG_OPTION(RTGC::LOG_GCNODE, function);
}

void GCObject::addReferrer(GCObject* referrer) {
    /**
     * 주의!) referrer 는 아직, memory 내용이 복사되지 않은 주소일 수 있다.
     */
    // rtgc_debug_log(this, "referrer %p added to %p\n", referrer, this);
    precond(referrer != this);
    if (!hasReferrer()) {
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


int GCObject::removeReferrer(GCObject* referrer) {
    precond(referrer != this);
#ifdef ASSERT    
    if (RTGC::is_debug_pointer((void*)this) || RTGC::is_debug_pointer((void*)referrer)) {
        rtgc_log(1, "anchor %p removed from %p isMulti=%d tr=%d rc=%d\n", referrer, this, hasMultiRef(), isTrackable(), getRootRefCount());
    }
#endif
    assert(hasReferrer(), "no referrer %p(%s) in empty %p(%s) \n", 
        referrer, RTGC::getClassName(referrer, true),
        this, RTGC::getClassName(this));

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

int GCObject::tryRemoveReferrer(GCObject* referrer) {
    precond(referrer != this);
    if (!hasReferrer()) return -1;

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

void GCObject::clearAnchorList() {
    rtgc_debug_log(this, "all anchor removed from %p isMulti=%d tr=%d rc=%d\n",
        this, hasMultiRef(), isTrackable(), getRootRefCount());
    if (hasMultiRef()) {
        ReferrerList* referrers = getReferrerList();
        _rtgc.gRefListPool.delete_(referrers);
        setHasMultiRef(false);
    }    
    this->_refs = 0;
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

template<bool isGarbage>
static bool is_match(GCObject* node, GCObject* referrer) {
    if (isGarbage) {
        return node->isDirtyReferrerPoints();
    } else {
        return node == referrer;
    }
}

template<bool isGarbage>
static bool remove_matched_referrers(GCObject* node, GCObject* referrer) {
    if (!node->hasMultiRef()) {
        if (node->_refs == 0) return false;
        if (isGarbage) {
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
        if (node->hasSafeAnchor() && is_match<isGarbage>(referrers->at(0), referrer)) {
            if (node->hasShortcut()) {
                node->getShortcut()->split(referrer, node);
            } else {
                node->invalidateSafeAnchor();
            }
            postcond(!node->hasSafeAnchor());
        }
        if (isGarbage) {
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

void GCObject::clearGarbageAnchors() {
    remove_matched_referrers<true>(this, NULL);
}

GCObject* GCObject::getSingleAnchor() {
    precond(this->hasReferrer());
    if (this->hasMultiRef()) return NULL;
    GCObject* front = _offset2Object(_refs);
    return front;
}

GCObject* GCObject::getSafeAnchor() {
    assert(hasReferrer(), "no anchors %p(%s)\n", this, RTGC::getClassName(this)); 
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
    assert(hasReferrer() && SafeShortcut::isValidIndex(this->getShortcutId()), 
        "incorrect anchor(%p) for empty obj(%p:%d)", anchor, this, this->getShortcutId());
    precond(!this->getShortcut()->isValid());

    if (hasMultiRef()) {
        ReferrerList* referrers = getReferrerList();
        int idx = referrers->indexOf(anchor);
        assert(idx >= 0, "incorrect anchor(%p) for this(%p)",
            anchor, this);
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
}

SafeShortcut* GCObject::getShortcut() {
    int p_id = this->getShortcutId();
    return SafeShortcut::getPointer(p_id);
}



bool GCObject::visitLinks(LinkVisitor visitor, void* callbackParam) {
    assert(0, "not impl");
    /*
    int offset;
    for (uint16_t* offsets = this->getFieldOffsets(); (offset = *offsets) != 0; offsets ++) {
        OffsetPointer<GCObject>* field = 
            (OffsetPointer<GCObject>*)((uintptr_t)this + offset);
        GCObject* ref = field->getPointer();
        if (ref != nullptr && ref != this) {
            if (!visitor(this, ref, callbackParam)) return false;
        }
    }
    */
    return true;
}


void GCObject::initIterator(AnchorIterator* iterator) {
    if (!hasReferrer()) {
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
    OffsetPointer<GCObject>* field = (OffsetPointer<GCObject>*)(this + 1);
    for (int idx = this->_cntItem; --idx >= 0; field++) {
        GCObject* ref = field->getPointer();
        if (ref != nullptr && ref != this) {
            if (!visitor(this, ref, callbackParam)) return false;
        }
    }
    return true;
}


GCObject* GCObject::getLinkInside(SafeShortcut* container) {
    int sibling_id = SafeShortcut::getIndex(container);
    uint16_t* offsets = this->getFieldOffsets();
    if (offsets == GCArray::_array_offsets) {
        GCArray* array = (GCArray*)this;
        OffsetPointer<GCObject>* field = (OffsetPointer<GCObject>*)(array + 1);
        for (int idx = array->length(); --idx >= 0; field++) {
            GCObject* ref = field->getPointer();
            if (ref != nullptr && ref->getShortcutId() == sibling_id) {
                return ref;
            }
        }
    }
    else {
        for (int offset; (offset = *offsets) != 0; offsets ++) {
            OffsetPointer<GCObject>* field = 
                (OffsetPointer<GCObject>*)((uintptr_t)this + offset);
            GCObject* ref = field->getPointer();
            if (ref != nullptr && ref->getShortcutId() == sibling_id) {
                return ref;
            }
        }
    }
    return nullptr;
}
#endif
