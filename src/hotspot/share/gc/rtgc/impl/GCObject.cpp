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
    //rtgc_log(true, "addReferrer %p<-%p hasRef=%d hasMulti=%d\n", 
    //        this, referrer, hasReferrer(), _hasMultiRef);
    if (!hasReferrer()) {
        precond(!hasMultiRef());
        this->_refs = _pointer2offset(referrer, &_refs);
        postcond(referrer == _offset2Object(_refs, &_refs));
        postcond(!hasMultiRef());
    }
    else {
        ReferrerList* referrers;
        if (!hasMultiRef()) {
            referrers = _rtgc.gRefListPool.allocate();
            referrers->init(2);
            GCObject* front = _offset2Object(_refs, &_refs);
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
    assert(hasReferrer(), "no referrer %p(%s) in empty %p(%s) \n", 
        referrer, RTGC::getClassName(referrer, true),
        this, RTGC::getClassName(this));

    if (!hasMultiRef()) {
        assert(_refs == _pointer2offset(referrer, &_refs), 
            "referrer %p(%s) != %p in %p(%s) \n", 
            referrer, RTGC::getClassName(referrer),
            _offset2Object(_refs, &_refs),
            this, RTGC::getClassName(this));
        this->_refs = 0;
    }
    else {
        ReferrerList* referrers = getReferrerList();
        int idx = referrers->indexOf(referrer);
        rtgc_log(idx < 0, "referrer %p(%s) is not found in %p(%s) \n", 
            referrer, RTGC::getClassName(referrer),
            this, RTGC::getClassName(this));
        if (idx < 0) {
            for (int i = 0; i < referrers->size(); i ++) {
                rtgc_log(true, "at[%d] %p\n", i, (void*)referrers->at(i));
            }
        }
        precond(idx >= 0);
        if (referrers->size() == 2) {
            GCObject* remained = referrers->at(1 - idx);
            _rtgc.gRefListPool.delete_(referrers);
            this->_refs = _pointer2offset(remained, &_refs);
            setHasMultiRef(false);
        }
        else {
            referrers->removeFast(idx);
        }
        if (idx != 0) {
            return idx;
        }
    }
    if (this->getShortcutId() > INVALID_SHORTCUT) {
		SafeShortcut* shortcut = this->getShortcut();
        precond(this->getShortcutId() == referrer->getShortcutId()
            ||  referrer == shortcut->getAnchor());
    //this->invaliateSurvivalPath(referrer);
        shortcut->split(referrer, this);
    }
    return 0;
}

void GCObject::removeAllReferrer() {
    rtgc_log(LOG_OPT(1), "refList of garbage cleaned %p\n", this);
    if (hasMultiRef()) {
        ReferrerList* referrers = getReferrerList();
        _rtgc.gRefListPool.delete_(referrers);
        setHasMultiRef(false);
    }    
    this->_refs = 0;
}

bool GCObject::removeMatchedReferrers(GCObject* referrer) {
    if (!hasMultiRef()) {
        if (_refs != 0 && _refs == _pointer2offset(referrer, &_refs)) {
            this->_refs = 0;
            return true;
        }
    }
    else {
        ReferrerList* referrers = _rtgc.gRefListPool.getPointer(_refs);
        if (referrers->removeMatchedItems(referrer)) {
            if (referrers->size() <= 1) {
                if (referrers->size() == 0) {
                    _rtgc.gRefListPool.delete_(referrers);
                    this->_refs = 0;
                }
                else {
                    GCObject* remained = referrers->at(0);
                    _rtgc.gRefListPool.delete_(referrers);
                    this->_refs = _pointer2offset(remained, &_refs);
                }
                setHasMultiRef(false);
            }
            return true;
        }
    }
    return false;
}

GCObject* GCObject::getSingleAnchor() {
    precond(!this->hasMultiRef());
    GCObject* front = _offset2Object(_refs, &_refs);
    return front;
}

GCObject* GCObject::getSafeAnchor() {
    precond(hasReferrer()); 
    GCObject* front;
    if (hasMultiRef()) {
        ReferrerList* referrers = getReferrerList();
        front = referrers->front();
    }
    else {
        front = _offset2Object(_refs, &_refs);
    }
    return front;
}

void GCObject::setSafeAnchor(GCObject* anchor) {
    assert(hasReferrer(), "incorrect anchor(%p) for empty obj(%p)",
        anchor, this);
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
        GCObject* front = _offset2Object(_refs, &_refs);
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
