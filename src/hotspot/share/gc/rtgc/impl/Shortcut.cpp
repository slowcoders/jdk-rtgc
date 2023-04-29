#include "GCObject.hpp" 
#include "GCRuntime.hpp" 
#include "classfile/javaClasses.inline.hpp"
#include "../RTGC.hpp"
#include "../rtgcGlobals.hpp"
#include "../rtgcHeap.hpp"

#define USE_ITERATOR_STACK false
#define USE_ANCHOR_VISIOR true

#define RECLAIM_GARBAGE_IMMEDIATELY     false
static const int GC_VERBOSE_LOG = false;

using namespace RTGC;

static const int LOG_OPT(int function) {
  return RTGC::LOG_OPTION(RTGC::LOG_SHORTCUT, function);
}

void CircuitNode::initialize() {
    //MemoryPool::initialize();
    CircuitNode* no_safe_anchor = _rtgc.g_circuitPool.allocate();
    CircuitNode* no_safe_circuit = _rtgc.g_circuitPool.allocate();
    no_safe_anchor->~CircuitNode();
    no_safe_circuit->~CircuitNode();
};

CircuitNode* CircuitNode::getPointer(int idx) {
	return _rtgc.g_circuitPool.getPointer(idx);
}

bool CircuitNode::isValidIndex(int idx) {
	return idx >= 0 && idx < _rtgc.g_circuitPool.size();
}

int CircuitNode::getIndex(CircuitNode* circuit) {
	return circuit == nullptr ? 0 : _rtgc.g_circuitPool.getIndex(circuit);
}

void CircuitNode::operator delete(void* ptr) {
    rtgc_log(LOG_OPT(10), "circuit[%d] deleted\n", getIndex((CircuitNode*)ptr));
	_rtgc.g_circuitPool.delete_((CircuitNode*)ptr);
}


void* CircuitNode::operator new (std::size_t size) {
	CircuitNode* circuit = _rtgc.g_circuitPool.allocate();
	return circuit;
}

#if 0
static int cntNoTracing = 0;
static int cntInTracing = 0;

bool CircuitNode::inContiguousTracing(GCObject* obj, CircuitNode** ppCircuit) {
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
        rt_assert(obj->getCircuit() == this);
        if (obj == _inTracing) break;
    }
    bool cut_tail = clearTooShort(prev, _tail);
    if (!cut_tail) {
        CircuitNode::create(prev, _tail, MIN_SHORTCUT_LENGTH+1, true);
    }
    prev->invalidateCircuitId();
    rtgc_log(LOG_OPT(7), "split circular circuit %d (%p->%p)\n", 
        (*ppCircuit)->getIndex(*ppCircuit), (void*)_anchor, _inTracing);
    *ppCircuit = jump_in->getCircuit();
    this->_tail = _inTracing;
    return false;
}


void CircuitNode::vailidateCircuit(GCObject* debug_obj) {
#ifdef ASSERT
    rt_assert(_anchor->getCircuit() != this);
    GCObject* anchor = _anchor;
    rt_assert_f(anchor->isTrackable(), "not trackable " PTR_DBG_SIG, PTR_DBG_INFO(anchor));
    debug_only(int cnt = 0;)
    GCObject* tail = this->_tail;
    for (GCObject* obj = _tail; obj != anchor; obj = obj->getSafeAnchor()) {
        rt_assert_f(obj->isTrackable(), "not trackable " PTR_DBG_SIG, PTR_DBG_INFO(obj));
        //rtgc_debug_log(tail, "debug circuit[%d] %d:%p\n", this->getIndex(this), ++cnt, obj);
        if (obj->getCircuit() != this) {
            if (debug_obj != NULL) {
                rtgc_log(1, "invalid circuit referenced by %p(%s:%d)", 
                        debug_obj, RTGC::getClassName(debug_obj), debug_obj->getCircuitId());
    }
            for (GCObject* obj2 = _tail; obj2 != obj; obj2 = obj2->getSafeAnchor()) {
                rtgc_log(1, "   node %p(%s:%d) in circuit[%d]", 
                    obj2, RTGC::getClassName(obj2), obj2->getCircuitId(), getIndex(this));
            }
        }
        rt_assert_f(obj->getCircuit() == this, "invalid node %p(%s:%d) in circuit[%d]", 
            obj, RTGC::getClassName(obj), obj->getCircuitId(), getIndex(this));
    }
#endif
}

void CircuitNode::split(GCObject* leftTail, GCObject* rightAnchor) {
    int s_id = getIndex(this);

    rt_assert_f(this->isValid(), "shotcut[%d] is invalid\nleftTail=" PTR_DBG_SIG "rightAnchor=" PTR_DBG_SIG "\n", 
        getIndex(this), PTR_DBG_INFO(leftTail), PTR_DBG_INFO(rightAnchor));
    rt_assert(rightAnchor->getCircuit() == this);
    rt_assert_f(leftTail->getCircuit() == this || leftTail == this->anchor(), 
        "wrong tail [%d] -> %p(%s) [%d] gm = %d\n", 
        this->getIndex(), leftTail, getClassName(leftTail), 
        leftTail->getCircuitId(), leftTail->isGarbageMarked());

    rightAnchor->invalidateSafeAnchor();
    if (leftTail == this->_anchor) {
        if (clearTooShort(rightAnchor, _tail)) {
            rtgc_log(LOG_OPT(10), "split circuit[%d]: deleted 0\n", s_id);
            delete this;
        } else {
            this->_anchor = rightAnchor;
            rtgc_log(LOG_OPT(10), "split circuit[%d]: cut anchor\n", s_id);
            this->vailidateCircuit();
        }
        return;
    }
    if (rightAnchor == this->_tail) {
        if (clearTooShort(_anchor, leftTail)) {
            rtgc_log(LOG_OPT(10), "split circuit[%d]: deleted 1\n", s_id);
            delete this;
        } else {
            rtgc_log(LOG_OPT(10), "split circuit[%d]: cut tail\n", s_id);
            this->_tail = leftTail;
            this->vailidateCircuit();
        }
        return;
    }

    bool cut_tail = clearTooShort(rightAnchor, _tail);
    bool cut_head = clearTooShort(_anchor, leftTail);
    if (cut_head) {
        if (cut_tail) {
            rtgc_log(LOG_OPT(10), "split circuit[%d]: deleted 2\n", s_id);
            delete this;
            return;
        }
        rtgc_log(LOG_OPT(10), "split circuit[%d]: cut heads\n", s_id);
        this->_anchor = rightAnchor;
    }
    else if (cut_tail) {
        rtgc_log(LOG_OPT(10), "split circuit[%d]: cut tails\n", s_id);
        this->_tail = leftTail;
    }
    else {
        rtgc_log(LOG_OPT(10), "split circuit[%d]: divide\n", s_id);
        CircuitNode::create(rightAnchor, _tail, MIN_SHORTCUT_LENGTH+1, true);
        this->_tail = leftTail;
    }
    this->vailidateCircuit();
}

bool CircuitNode::clearTooShort(GCObject* anchor, GCObject* tail) {
    int len = MIN_SHORTCUT_LENGTH;
    for (GCObject* obj = tail; obj != anchor; obj = obj->getSafeAnchor()) {
        rt_assert(obj->hasAnchor());
        if (--len < 0) return false;
    }

    GCObject* next;
    for (GCObject* obj = tail; obj != anchor; obj = next) {
        obj->invalidateCircuitId();
        next = obj->getSafeAnchor();
    }

    return true;
}

void CircuitNode::shrinkAnchorTo(GCObject* newAnchor) {
    rt_assert_f(newAnchor->getCircuit() == this, "invalid anchor %p[%d]\n", newAnchor, this->getIndex(this));
    rt_assert(!this->inTracing());
    GCObject* old_anchor = _anchor;
    GCObject* next;
    for (GCObject* obj = newAnchor; obj != old_anchor; obj = next) {
        obj->invalidateCircuitId();
        next = obj->getSafeAnchor();
    }
    if (clearTooShort(newAnchor, _tail)) {
        rtgc_log(LOG_OPT(10), "circuit deleted[%d] %p->%p\n", getIndex(this), (void*)_anchor, (void*)_tail);
        delete this;
    } else {
        this->_anchor = newAnchor;
        rtgc_log(LOG_OPT(10), "circuit shrinked[%d] %p->%p\n", getIndex(this), (void*)_anchor, (void*)_tail);
        this->vailidateCircuit();
    }
}

void CircuitNode::shrinkTailTo(GCObject* newTail) {
    rt_assert_f(newTail->getCircuit() == this || newTail == this->anchor(), "invalid tail %p[%d]\n", newTail, this->getIndex(this));
    rt_assert(!this->inTracing());

    GCObject* next;
    for (GCObject* obj = _tail; obj != newTail; obj = next) {
        obj->invalidateCircuitId();
        next = obj->getSafeAnchor();
    }

    if (clearTooShort(_anchor, newTail)) {
        rtgc_log(LOG_OPT(10), "circuit deleted[%d] %p->%p\n", getIndex(this), (void*)_anchor, (void*)_tail);
        delete this;
    } else {
        this->_tail = newTail;
        rtgc_log(LOG_OPT(10), "circuit shrinked[%d] %p->%p\n", getIndex(this), (void*)_anchor, (void*)_tail);
        this->vailidateCircuit();
    }
}

void CircuitNode::extendTail(GCObject* tail) {
    rt_assert(tail != NULL); 
    rt_assert(tail != _tail); 
    int s_id = getIndex(this);
    rtgc_log(RTGC::LOG_OPTION(LOG_SCANNER, 10), "extendTail shotcut[%d] %p->%p\n", s_id, (void*)_tail, tail);
    GCObject* old_tail = _tail;
    GCObject* next;
    for (GCObject* node = tail; node != old_tail; node = next) {
        node->setCircuitId_unsafe(s_id);
        next = node->getSafeAnchor();
    }

    this->_tail = tail;
    vailidateCircuit();
}

void CircuitNode::extendAnchor(GCObject* anchor) { 
    rt_assert(anchor != NULL); 
    rt_assert(anchor != _anchor); 
    int s_id = getIndex(this);
    rtgc_log(RTGC::LOG_OPTION(LOG_SCANNER, 10), "extendAnchor shotcut[%d] %p->%p\n", s_id, (void*)_anchor, anchor);

    GCObject* next;
    for (GCObject* node = _anchor; node != anchor; node = next) {
        node->setCircuitId_unsafe(s_id);
        next = node->getSafeAnchor();
    }
    
    this->_anchor = anchor;
    vailidateCircuit();
}
#endif