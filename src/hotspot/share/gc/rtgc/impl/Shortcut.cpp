#include "GCObject.hpp" 
#include "GCRuntime.hpp" 
#include "classfile/javaClasses.inline.hpp"
#include "../RTGC.hpp"
#include "../rtgcDebug.hpp"
#include "../rtgcHeap.hpp"

#define USE_ITERATOR_STACK false
#define USE_ANCHOR_VISIOR true

#define RECLAIM_GARBAGE_IMMEDIATELY     false
static const int GC_VERBOSE_LOG = false;

using namespace RTGC;


// RTGC::MemoryPool<int, 4096, 0, -1> _memPool;
// template<>
// RTGC::MemoryPool<int, 4096, 0, -1>* RTGC::MemoryPool<int, 4096, 0, -1>::HugeAllocator::memPool = &_memPool;
// RTGC::MemoryPool<int, 4096, 0, -1>::HugeArray hugeIntArray(0);

static const int LOG_OPT(int function) {
  return RTGC::LOG_OPTION(RTGC::LOG_SHORTCUT, function);
}

void SafeShortcut::initialize() {
    //MemoryPool::initialize();
    SafeShortcut* no_safe_anchor = _rtgc.g_shortcutPool.allocate();
    SafeShortcut* no_safe_shortcut = _rtgc.g_shortcutPool.allocate();
    no_safe_anchor->~SafeShortcut();
    no_safe_shortcut->~SafeShortcut();
};

SafeShortcut* SafeShortcut::getPointer(int idx) {
	return _rtgc.g_shortcutPool.getPointer(idx);
}

bool SafeShortcut::isValidIndex(int idx) {
	return idx >= 0 && idx < _rtgc.g_shortcutPool.size();
}

int SafeShortcut::getIndex(SafeShortcut* circuit) {
	return circuit == nullptr ? 0 : _rtgc.g_shortcutPool.getIndex(circuit);
}

void SafeShortcut::operator delete(void* ptr) {
    rtgc_log(LOG_OPT(10), "shortcut[%d] deleted\n", getIndex((SafeShortcut*)ptr));
	_rtgc.g_shortcutPool.delete_((SafeShortcut*)ptr);
}


void* SafeShortcut::operator new (std::size_t size) {
	SafeShortcut* circuit = _rtgc.g_shortcutPool.allocate();
	return circuit;
}

static int cntNoTracing = 0;
static int cntInTracing = 0;

bool SafeShortcut::inContiguousTracing(GCObject* obj, SafeShortcut** ppShortcut) {
#ifdef ASSERT
    if (_inTracing) {
        cntInTracing ++;
    }
    else {
        cntNoTracing ++;
        if ((cntNoTracing % 10000) == 0) {
            // rtgc_log(true, "inTracing %d, noTracing %d\n", cntInTracing, cntNoTracing);
        }
    }
#endif
    if (_inTracing == NULL) return false;
    if (obj == _inTracing) return true;
    GCObject* jump_in = obj;
    GCObject* anchor = this->anchor();
    GCObject* prev;
    while (true) {
        prev = obj;
        obj = obj->getSafeAnchor();
        if (obj == anchor) return true;
        precond(obj->getShortcut() == this);
        if (obj == _inTracing) break;
    }
    bool cut_tail = clearTooShort(prev, _tail);
    if (!cut_tail) {
        SafeShortcut::create(prev, _tail, MIN_SHORTCUT_LENGTH+1, true);
    }
    prev->invalidateShortcutId();
    rtgc_log(LOG_OPT(7), "slpit cicular shortcut %d (%p->%p)", 
        (*ppShortcut)->getIndex(*ppShortcut), (void*)_anchor, _inTracing);
    *ppShortcut = jump_in->getShortcut();
    this->_tail = _inTracing;
    return false;
}


void SafeShortcut::vailidateShortcut() {
    precond(_anchor->getShortcut() != this);
    precond(_anchor->isTrackable());
    GCObject* anchor = _anchor;
    debug_only(int cnt = 0;)
    GCObject* tail = this->_tail;
    for (GCObject* obj = _tail; obj != anchor; obj = obj->getSafeAnchor()) {
        precond(obj->isTrackable());
        rtgc_debug_log(tail, "debug shortcut[%d] %d:%p\n", this->getIndex(this), ++cnt, obj);
        assert(obj->getShortcut() == this, "invalid anchor %p(%s) in shortcut[%d]", 
            obj, RTGC::getClassName(obj), getIndex(this));
    }
    rtgc_debug_log(tail, "debug shortcut[%d] end:%p\n", this->getIndex(this), anchor);
}

void SafeShortcut::split(GCObject* leftTail, GCObject* rightAnchor) {
    int s_id = getIndex(this);
    // rtgc_log(RTGC::is_debug_pointer(leftTail) && java_lang_ref_Reference::is_phantom(cast_to_oop(leftTail)), 
    //     "REF_PHANTOM[%d] shotcut split=%p(%s) rightAnchor=%p(%s)\n", 
    //     getIndex(this), leftTail, RTGC::getClassName(leftTail), 
    //                     rightAnchor, RTGC::getClassName(rightAnchor));

    assert(this->isValid(), "shotcut[%d] is invalid leftTail=%p(%s) rightAnchor=%p(%s)\n", 
        getIndex(this), leftTail, RTGC::getClassName(leftTail), 
                        rightAnchor, RTGC::getClassName(rightAnchor));
    precond(rightAnchor->getShortcut() == this);
    assert(leftTail->getShortcut() == this || leftTail == this->anchor(), 
        "wrong tail [%d] -> %p(%s) [%d] gm = %d\n", 
        this->getIndex(), leftTail, getClassName(leftTail), leftTail->getShortcutId(), leftTail->isGarbageMarked());
    rightAnchor->invalidateSafeAnchor();

    if (leftTail == this->_anchor) {
        if (clearTooShort(rightAnchor, _tail)) {
            rtgc_log(LOG_OPT(10), "split shortcut[%d]: deleted 0\n", s_id);
            delete this;
        } else {
            this->_anchor = rightAnchor;
            rtgc_log(LOG_OPT(10), "split shortcut[%d]: cut anchor\n", s_id);
            this->vailidateShortcut();
        }
        return;
    }
    if (rightAnchor == this->_tail) {
        if (clearTooShort(_anchor, leftTail)) {
            rtgc_log(LOG_OPT(10), "split shortcut[%d]: deleted 1\n", s_id);
            delete this;
        } else {
            rtgc_log(LOG_OPT(10), "split shortcut[%d]: cut tail\n", s_id);
            this->_tail = leftTail;
            this->vailidateShortcut();
        }
        return;
    }

    bool cut_tail = clearTooShort(rightAnchor, _tail);
    bool cut_head = clearTooShort(_anchor, leftTail);
    if (cut_head) {
        if (cut_tail) {
            rtgc_log(LOG_OPT(10), "split shortcut[%d]: deleted 2\n", s_id);
            delete this;
            return;
        }
        rtgc_log(LOG_OPT(10), "split shortcut[%d]: cut heads\n", s_id);
        this->_anchor = rightAnchor;
    }
    else if (cut_tail) {
        rtgc_log(LOG_OPT(10), "split shortcut[%d]: cut tails\n", s_id);
        this->_tail = leftTail;
    }
    else {
        rtgc_log(LOG_OPT(10), "split shortcut[%d]: divide\n", s_id);
        SafeShortcut::create(rightAnchor, _tail, MIN_SHORTCUT_LENGTH+1, true);
        this->_tail = leftTail;
    }
    this->vailidateShortcut();
}

bool SafeShortcut::clearTooShort(GCObject* anchor, GCObject* tail) {
    int len = MIN_SHORTCUT_LENGTH;
    for (GCObject* obj = tail; obj != anchor; obj = obj->getSafeAnchor()) {
        precond(obj->hasReferrer());
        if (--len < 0) return false;
    }
    for (GCObject* obj = tail; obj != anchor; obj = obj->getSafeAnchor()) {
        obj->invalidateShortcutId();
    }
    return true;
}

void SafeShortcut::shrinkAnchorTo(GCObject* newAnchor) {
    assert(newAnchor->getShortcut() == this, "invalid anchor %p[%d]\n", newAnchor, this->getIndex(this));
    precond(!this->inTracing());
    GCObject* old_anchor = _anchor;
    for (GCObject* obj = newAnchor; obj != old_anchor; obj = obj->getSafeAnchor()) {
        obj->invalidateShortcutId();
    }
    if (clearTooShort(newAnchor, _tail)) {
        rtgc_log(LOG_OPT(10), "shortcut deleted[%d] %p->%p\n", getIndex(this), (void*)_anchor, (void*)_tail);
        delete this;
    } else {
        this->_anchor = newAnchor;
        rtgc_log(LOG_OPT(10), "shortcut shrinked[%d] %p->%p\n", getIndex(this), (void*)_anchor, (void*)_tail);
        this->vailidateShortcut();
    }
}

void SafeShortcut::shrinkTailTo(GCObject* newTail) {
    assert(newTail->getShortcut() == this || newTail == this->anchor(), "invalid tail %p[%d]\n", newTail, this->getIndex(this));
    precond(!this->inTracing());
    for (GCObject* obj = _tail; obj != newTail; obj = obj->getSafeAnchor()) {
        obj->invalidateShortcutId();
    }
    if (clearTooShort(_anchor, newTail)) {
        rtgc_log(LOG_OPT(10), "shortcut deleted[%d] %p->%p\n", getIndex(this), (void*)_anchor, (void*)_tail);
        delete this;
    } else {
        this->_tail = newTail;
        rtgc_log(LOG_OPT(10), "shortcut shrinked[%d] %p->%p\n", getIndex(this), (void*)_anchor, (void*)_tail);
        this->vailidateShortcut();
    }
}

void SafeShortcut::extendTail(GCObject* tail) {
    precond(tail != NULL); 
    precond(tail != _tail); 
    int s_id = getIndex(this);
    rtgc_log(RTGC::LOG_OPTION(LOG_SCANNER, 10), "extendTail shotcut[%d] %p->%p\n", s_id, (void*)_tail, tail);
    GCObject* old_tail = _tail;
    for (GCObject* node = tail; node != old_tail; node = node->getSafeAnchor()) {
        node->setShortcutId_unsafe(s_id);
    }
    this->_tail = tail;
    vailidateShortcut();
}

void SafeShortcut::extendAnchor(GCObject* anchor) { 
    precond(anchor != NULL); 
    precond(anchor != _anchor); 
    int s_id = getIndex(this);
    rtgc_log(RTGC::LOG_OPTION(LOG_SCANNER, 10), "extendAnchor shotcut[%d] %p->%p\n", s_id, (void*)_anchor, anchor);
    for (GCObject* node = _anchor; node != anchor; node = node->getSafeAnchor()) {
        node->setShortcutId_unsafe(s_id);
    }
    this->_anchor = anchor;
    vailidateShortcut();
}
