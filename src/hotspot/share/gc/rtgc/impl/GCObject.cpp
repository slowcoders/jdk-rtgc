#include "precompiled.hpp"
#include "oops/klass.inline.hpp"

#include "GCObject.hpp"
#include "GCRuntime.hpp"

using namespace RTGC;


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

const char* __getClassName(GCObject* obj) {
    Klass* klass = cast_to_oop(obj)->klass();
    if (vmClasses::Class_klass() == klass) {
        printf("Class of class\n");
        cast_to_oop(obj)->print_on(tty);
    }
    return (const char*)klass->name()->bytes();
}

int GCObject::removeReferrer(GCObject* referrer) {
    assert(hasReferrer(), "no referrer %p(%s) in empty %p(%s) \n", 
        referrer, __getClassName(referrer),
        this, cast_to_oop(this)->klass()->name()->bytes());

    if (!hasMultiRef()) {
        assert(_refs == _pointer2offset(referrer, &_refs), 
            "referrer %p(%s) != %p in %p(%s) \n", 
            referrer, cast_to_oop(referrer)->klass()->name()->bytes(),
            _offset2Object(_refs, &_refs),
            this, cast_to_oop(this)->klass()->name()->bytes());
        this->_refs = 0;
    }
    else {
        ReferrerList* referrers = getReferrerList();
        int idx = referrers->indexOf(referrer);
        rtgc_log(idx < 0, "referrer %p(%s) is not found in %p(%s) \n", 
            referrer, cast_to_oop(referrer)->klass()->name()->bytes(),
            this, cast_to_oop(this)->klass()->name()->bytes());
        if (idx < 0) {
            for (int i = 0; i < referrers->size(); i ++) {
                rtgc_log(true, "at[%d] %p\n", i, referrers->at(i));
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
    this->invaliateSurvivalPath();
    return 0;
}

bool GCObject::removeAllReferrer(GCObject* referrer) {
    if (!hasMultiRef()) {
        if (_refs != 0 && _refs == _pointer2offset(referrer, &_refs)) {
            this->_refs = 0;
            return true;
        }
    }
    else {
        ReferrerList* referrers = _rtgc.gRefListPool.getPointer(_refs);
        if (referrers->removeAll(referrer)) {
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
    precond(hasReferrer()); 
    precond(!this->getShortcut()->isValid());

    if (hasMultiRef()) {
        ReferrerList* referrers = getReferrerList();
        int idx = referrers->indexOf(anchor);
        precond(idx >= 0);
        if (idx != 0) {
            GCObject* tmp = referrers->at(0);
            referrers->at(0) = anchor;
            referrers->at(idx) = tmp;
        }
    }
    else {
        GCObject* front = _offset2Object(_refs, &_refs);
        precond(front == anchor);
    }
    setShortcutId_unsafe(INVALID_SHORTCUT);
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
        iterator->_temp = _offset2Object(_refs, &_refs);
        iterator->_current = &iterator->_temp;
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

void GCObject::setShortcutId_unsafe(int shortcutId) {
	precond(!getShortcut()->isValid());
	this->_shortcutId = shortcutId;
}

void GCObject::invaliateSurvivalPath() {
	if (this->_shortcutId > 0) {
		this->getShortcut()->invalidate();
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
