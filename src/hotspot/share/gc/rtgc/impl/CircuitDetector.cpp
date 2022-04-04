#include "GCObject.hpp" 
#include "GCRuntime.hpp" 
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
  return RTGC::LOG_OPTION(RTGC::LOG_SCANNER, function);
}

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

class PathFinder {
    SimpleVector<GCObject*>& _visitedNodes;
    SimpleVector<AnchorIterator> _trackers;
    SimpleVector<ShortOOP> _tempOops;
    GCObject* _temp;
public:
    PathFinder(SimpleVector<GCObject*>& nodes) : _visitedNodes(nodes) {}

    bool findSurvivalPath(GCObject* tracingNode);

    void constructShortcut();
};


bool PathFinder::findSurvivalPath(GCObject* tracingNode) {
    _tempOops.push_back(tracingNode);
    AnchorIterator* it = _trackers.push_empty();
    it->initSingleIterator(&_tempOops.back());
    while (true) {
        GCObject* R;
        while (!it->hasNext()) {
            _trackers.pop_back();
            rtgc_log(LOG_OPT(0x10), "SurvivalPath pop [%d]\n", _trackers.size());
            if (_trackers.empty()) return false;
            it = &_trackers.back();

            R = it->peekPrev();
            if (!R->isGarbageMarked()) {
                SafeShortcut* shortcut = R->getShortcut();
                precond(shortcut->inTracing());
                precond(R->hasReferrer());
                shortcut->unmarkInTracing();
                rtgc_log(LOG_OPT(0x10), "shortcut poped %p:%d\n", R, R->getShortcutId());
                shortcut->shrinkAnchorTo(R);
                it = _trackers.push_empty();
                R->initIterator(it);
                postcond(!_visitedNodes.contains(R));
                R->markGarbage();
                _visitedNodes.push_back(R);
                break;
            }
        }

        R = it->next();
        SafeShortcut* shortcut = R->getShortcut();
        precond(shortcut != NULL);
        precond(R != NULL);

        /* 중복 추적 회피 단계(S341) */
        if (R->isGarbageMarked() ||
            shortcut->inTracing()) {
            rtgc_log(LOG_OPT(0x10), "pass marked %p:%d[%d](gm=%d)\n", 
                R, R->getShortcutId(), _trackers.size(), R->isGarbageMarked());
            continue;
        }

        if (R->getRootRefCount() > ZERO_ROOT_REF) {
            rtgc_log(LOG_OPT(0x10), "SurvivalPath found %p[%d]\n", R, _trackers.size());
            return true;
        }

        rtgc_log(LOG_OPT(0x10), "findSurvivalPath %p:%d[%d]\n", 
            R, R->getShortcutId(), _trackers.size());

        if (R->hasReferrer()) {
            it = _trackers.push_empty();
            if (shortcut->isValid()) {
                shortcut->markInTracing();
                it->initSingleIterator(&shortcut->anchor_ref());
                rtgc_log(LOG_OPT(0x10), "shortcut[%d] pushed %p(anchor = %p->%p)\n", 
                    shortcut->getIndex(shortcut), R, (void*)shortcut->anchor(), (void*)shortcut->tail());
                shortcut->vailidateShortcut();
            }
            else {
                R->initIterator(it);
                R->markGarbage();
                _visitedNodes.push_back(R);
            }
            continue;
        }

        R->markGarbage();
        _visitedNodes.push_back(R);
    }
}

void PathFinder::constructShortcut() {
    for (int i = _visitedNodes.size(); --i >= 0; ) {
        GCObject* obj = (GCObject*)_visitedNodes.at(i);
        obj->unmarkGarbage();
    }

    const int MAX_SHORTCUT_LEN = 256;
    // for (int i = 0; i < _trackers.size(); i ++) {
    //     AnchorIterator* it = _trackers.adr_at(i);
    //     rtgc_log(LOG_OPT(0x10), "- link(%p)[%d]\n", it->peekPrev(), i);
    // }
    AnchorIterator* ait = _trackers.adr_at(0);
    AnchorIterator* end = ait + _trackers.size();
    if (ait + 1 >= end) {
        return;
    }
    GCObject* tail = NULL;
    GCObject* link = NULL;
    SafeShortcut* lastShortcut = NULL;
    int cntNode = 0;
    for (; ait < end; ait++) {
        GCObject* obj = ait->peekPrev();        
        rtgc_log(LOG_OPT(0x10), "link(%p) to anchor(%p)%d\n", link, obj, obj->getShortcutId());
        if (link != NULL) {
            assert(link->hasReferrer(),
                "link has no anchor %p:%d\n", obj, obj->getShortcutId());
            link->setSafeAnchor(obj);
        }

        assert(SafeShortcut::isValidIndex(obj->getShortcutId()),
            "invalid shortcut id %p:%d\n", obj, obj->getShortcutId());
		SafeShortcut* ss = obj->getShortcut();
        if (!ss->isValid()) {
            if (++cntNode >= MAX_SHORTCUT_LEN) {
                lastShortcut = SafeShortcut::create(obj, tail, cntNode);
                cntNode = 1;
            }
            if (cntNode == 1) {
                tail = obj;
            }
            link = obj;
        } else {
            assert(ait+1 == end || ait[+1].peekPrev() == ss->anchor(), 
                "invalid shortcut %p:%d\n", obj, obj->getShortcutId());
            precond(ss->inTracing() || (ait+1 == end && obj->getRootRefCount() > 0));
            ss->unmarkInTracing();
            if (cntNode > 0) {
                if (obj == ss->tail()) {
                    ss->extendTail(tail);
                }
                else if (lastShortcut != NULL) {
                    lastShortcut->extendAnchor(obj);
                }
                else {
                    SafeShortcut::create(obj, tail, cntNode);
                }
                cntNode = 0;
            }
            link = NULL;
            lastShortcut = ss;
        }
    }
    
    if (cntNode > 1) {
        if (lastShortcut != NULL) {
            lastShortcut->extendAnchor(link);
        } else {
            SafeShortcut::create(link, tail, cntNode);
        }
    }
    // last anchor may not have safe-anchor
}

static bool clear_garbage_links(GCObject* link, GCObject* garbageAnchor, SimpleVector<GCObject*>* unsafeObjects) {
    // if (link->isGarbageMarked() || !link->isTrackable()) return false;

    // if (link->removeReferrer(garbageAnchor) == 0
    // &&  link->isUnsafe()) {
    //     if (link->isGarbage()) {
    //         link->markGarbage();
    //         link->removeAnchorList();
    //         rtgc_log(LOG_OPT(0x10), "Mark new garbage.\n");
    //         return true;
    //     }
    //     rtgc_log(LOG_OPT(0x10), "Add unsafe objects.\n");
    //     unsafeObjects->push_back(link);
    // }
    return false;
}


bool GarbageProcessor::detectUnreachable(GCObject* unsafeObj, SimpleVector<GCObject*>& unreachableNodes) {
    precond(!unsafeObj->isGarbageMarked());
    int cntUnreachable = unreachableNodes.size();

    rtgc_log(LOG_OPT(0x10), "detectUnreachable %p\n", unsafeObj);
    PathFinder pf(unreachableNodes);
    bool hasSurvivalPath = pf.findSurvivalPath(unsafeObj);

    if (hasSurvivalPath) {
        pf.constructShortcut();
        unreachableNodes.resize(cntUnreachable);
        return false;
    }
        unreachableNodes.resize(cntUnreachable);
    // for (int i = unreachableNodes.size(); --i >= 0; ) {
    //     GCObject* obj = (GCObject*)unreachableNodes.at(i);
    //     obj->unmarkGarbage();
    // }

    return true;
}

void GarbageProcessor::scanGarbages(GCObject* unsafeObj) {
    while (true) {
        rtgc_log(LOG_OPT(1), "scan Garbage %p\n", unsafeObj);

        PathFinder pf(_visitedNodes);
        bool hasSurvivalPath = pf.findSurvivalPath(unsafeObj);

        if (hasSurvivalPath) {
            pf.constructShortcut();
        }
        else {
            for (int i = _visitedNodes.size(); --i >= 0; ) {
                GCObject* obj = (GCObject*)_visitedNodes.at(i);
                //rtgc_log(LOG_OPT(0x10), "garbage deteted %p(%s)\n", obj, RTGC::getClassName(obj));
                //obj->removeAnchorList();
                //RTGC::scanInstanceGraph(obj, (RTGC::RefTracer3)clear_garbage_links, &_unsafeObjects);
                //destroyObject(obj);
            }
        }
        _visitedNodes.resize(0);
        while (true) {
            if (_unsafeObjects.empty()) {
                reclaimObjects();
                return;
            }
            unsafeObj = _unsafeObjects.back();
            _unsafeObjects.pop_back();
            if (!unsafeObj->isGarbageMarked()) break;
        }
    }
}

#if RECLAIM_GARBAGE_IMMEDIATELY
#define DESTROY_OBJECT(garbage) \
    garbage->markDestroyed(); \
    ((void**)garbage)[2] = delete_q; \
    delete_q = garbage;
#else
#define DESTROY_OBJECT(garbage)  // do nothing
#endif

void GarbageProcessor::destroyObject(GCObject* garbage) {

    if (garbage->isDestroyed()) return;
    DESTROY_OBJECT(garbage);
    _traceStack.push_back(garbage);
    LinkIterator* it = &_traceStack.back();
    while (true) {
        GCObject* link = it->next();
        if (link == nullptr) {
            _traceStack.pop_back();
            if (_traceStack.empty()) break;
            it = &_traceStack.back();
        }
        else if (!link->isDestroyed()) {
            if (!link->isGarbageMarked()) {
                if (link->removeReferrer(it->getContainerObject()) > 0
                ||  !link->isGarbage()) {
                    if (link->isUnsafe()) {
                        _unsafeObjects.push_back(link);
                    }
                    continue;
                }
            }
            DESTROY_OBJECT(link);
            _traceStack.push_back(link);
            it = &_traceStack.back();
        }
    }    
}


static int cntDelete = 0;
void GarbageProcessor::reclaimObjects() {
    if (GC_VERBOSE_LOG) printf("reclaimObjects\n");

    for (GCObject* node = delete_q; node != nullptr;) {
        GCObject* next = ((GCObject**)node)[2];
        //if (GC_VERBOSE_LOG) printf("delete %p %s %d\n", node, node->getDebugId(), ++cntDelete);
        assert(0, "delete not impl");
        //delete node;
        node = next;
    }
    delete_q = nullptr;
}

void SafeShortcut::vailidateShortcut() {
    precond(_anchor->getShortcut() != this);
    precond(_anchor->isTrackable());
    GCObject* anchor = _anchor;
    for (GCObject* obj = _tail; obj != anchor; obj = obj->getSafeAnchor()) {
        precond(obj->isTrackable());
        assert(obj->getShortcut() == this, "invalid anchor %p(%s) in shortcut[%d]", 
            obj, RTGC::getClassName(obj), getIndex(this));
    }
}

void SafeShortcut::split(GCObject* newTail, GCObject* newAnchor) {
    assert(this->isValid(), "shotcut[%d] is invalid newAnchar=%p tail=%p\n", 
        getIndex(this), newAnchor, (void*)_tail);
    precond(newAnchor->getShortcut() == this);
    precond(newTail->getShortcut() == this || newTail == this->anchor());
    newAnchor->setShortcutId_unsafe(0);

    if (newTail == this->_anchor) {
        if (clearTooShort(newAnchor, _tail)) {
            delete this;
        } else {
            this->_anchor = newAnchor;
            rtgc_log(LOG_OPT(10), "cut anchor\n");
            this->vailidateShortcut();
        }
        return;
    }
    if (newAnchor == this->_tail) {
        if (clearTooShort(_anchor, newTail)) {
            delete this;
        } else {
            rtgc_log(LOG_OPT(10), "cut tail\n");
            this->_tail = newTail;
            this->vailidateShortcut();
        }
        return;
    }

    bool cut_tail = clearTooShort(newAnchor, _tail);
    bool cut_head = clearTooShort(_anchor, newTail);
    if (cut_head) {
        if (cut_tail) {
            delete this;
            return;
        }
        rtgc_log(LOG_OPT(10), "cut heads\n");
        this->_anchor = newAnchor;
    }
    else if (cut_tail) {
        rtgc_log(LOG_OPT(10), "cut tails\n");
        this->_tail = newTail;
    }
    else {
        rtgc_log(LOG_OPT(10), "spilt shortcut\n");
        SafeShortcut::create(newAnchor, _tail, MIN_SHORTCUT_LENGTH+1);
        this->_tail = newTail;
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
        obj->setShortcutId_unsafe(INVALID_SHORTCUT);
    }
    return true;
}

void SafeShortcut::shrinkAnchorTo(GCObject* newAnchor) {
    precond(newAnchor->getShortcut() == this);
    //rtgc_log(LOG_OPT(10), "shrink anchor to %p [%d]", newAnchor, this->getIndex(this));
    GCObject* old_anchor = _anchor;
    for (GCObject* obj = newAnchor; obj != old_anchor; obj = obj->getSafeAnchor()) {
        obj->setShortcutId_unsafe(INVALID_SHORTCUT);
    }
    if (clearTooShort(newAnchor, _tail)) {
        rtgc_log(LOG_OPT(10), "shortcut deleted[%d] %p->%p\n", getIndex(this), (void*)_anchor, (void*)_tail);
        delete this;
    } else {
        this->_anchor = newAnchor;
        rtgc_log(LOG_OPT(10), "shortcut shrinked[%d] %p->%p\n", getIndex(this), (void*)_anchor, (void*)_tail);
    }
}