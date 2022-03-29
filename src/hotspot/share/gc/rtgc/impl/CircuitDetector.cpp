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

static const int LOG_OPT(int function) {
  return RTGC::LOG_OPTION(RTGC::LOG_SCANNER, function);
}

SafeShortcut* SafeShortcut::getPointer(int idx) {
	return _rtgc.g_shortcutPool.getPointer(idx);
}

bool SafeShortcut::isValidIndex(int idx) {
	return idx >= 0 && idx < _rtgc.g_shortcutPool.getAllocatedSize();
}

int SafeShortcut::getIndex(SafeShortcut* circuit) {
	return circuit == nullptr ? 0 : _rtgc.g_shortcutPool.getIndex(circuit);
}

void SafeShortcut::operator delete(void* ptr) {
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
        rtgc_log(true || LOG_OPT(0x10), "findSurvivalPath %p[%d]\n", R, _trackers.size());
        SafeShortcut* shortcut = R->getShortcut();
        precond(shortcut != NULL);
        precond(R != NULL);

        /* 중복 추적 회피 단계(S341) */
        if (R->isGarbageMarked() ||
            shortcut->inTracing()) {
            continue;
        }

        if (R->getRootRefCount() > ZERO_ROOT_REF) {
            rtgc_log(LOG_OPT(0x10), "SurvivalPath found [%d]\n", _trackers.size());
            return true;
        }

        if (R->hasReferrer()) {
            it = _trackers.push_empty();
            if (shortcut->isValid()) {
                shortcut->markInTracing();
                _tempOops.push_back(shortcut->getAnchor());
                rtgc_log(true || LOG_OPT(0x10), "shortcut pushed %p(%p)\n", R, shortcut->getAnchor());
                it->initSingleIterator(&_tempOops.back());
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
            precond(link->hasReferrer());
            link->setSafeAnchor(obj);
        }

        precond(SafeShortcut::isValidIndex(obj->getShortcutId()));
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
            postcond(ait+1 == end || ait[+1].peekPrev() == ss->getAnchor());
            precond(ss->inTracing() || (ait+1 == end && obj->getRootRefCount() > 0));
            ss->unmarkInTracing();
            if (cntNode > 0) {
                if (obj == ss->getTail()) {
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
    //         link->removeAllReferrer();
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

    rtgc_log(true || LOG_OPT(0x10), "detectUnreachable %p\n", unsafeObj);
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
                //obj->removeAllReferrer();
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


void SafeShortcut::split(GCObject* newTail, GCObject* newAnchor) {
    precond(newAnchor->getShortcut() == this);
    newAnchor->setShortcutId_unsafe(0);
    if (newTail == this->_anchor) {
        if (clearTooShort(newAnchor, _tail)) {
            delete this;
        } else {
            this->_anchor = newAnchor;
            this->vailidateShortcut();
        }
        return;
    }
    if (newAnchor == this->_tail) {
        if (clearTooShort(_anchor, newTail)) {
            delete this;
        } else {
            newTail->setShortcutId_unsafe(0);
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
        this->_anchor = newAnchor;
    }
    else if (cut_tail) {
        this->_tail = newTail;
    }
    else {
        this->_tail = newTail;
        SafeShortcut::create(newAnchor, _tail, MIN_SHORTCUT_LENGTH+1);
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
    for (GCObject* obj = newAnchor; obj != _anchor; obj = obj->getSafeAnchor()) {
        precond(obj->hasReferrer());
        obj->setShortcutId_unsafe(INVALID_SHORTCUT);
    }
    if (clearTooShort(newAnchor, _tail)) {
        delete this;
    } else {
        this->_anchor = newAnchor;
    }
}