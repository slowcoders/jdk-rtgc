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
public:    
    SimpleVector<GCObject*> _visitedNodes;
    SimpleVector<AnchorIterator> _trackers;
    SafeShortcut* reachableShortcurQ;

    bool findSurvivalPath(ShortOOP& tail);

public:
    PathFinder() : reachableShortcurQ(NULL) {}

    ~PathFinder() { clearReachableShortcutMarks(); }

    bool constructSurvivalPath(GCObject* tail);
    void constructShortcut();
    void clearReachableShortcutMarks();
};

void PathFinder::clearReachableShortcutMarks() {
#if ENABLE_REACHBLE_SHORTCUT_CACHE    
    for (SafeShortcut* path = reachableShortcurQ; path != NULL; path = path->nextReachable()) {
        path->unmarkReachable();
    }
#endif
}

bool PathFinder::constructSurvivalPath(GCObject* node) {
    ShortOOP tail = node;
    precond(EnableRTGC);
    bool hasSurvivalPath = findSurvivalPath(tail);
    if (hasSurvivalPath) {
        constructShortcut();
    }
#ifdef ASSERT        
    else {
        for (int i = _visitedNodes.size(); --i >= 0; ) {
            GCObject* obj = (GCObject*)_visitedNodes.at(i);
            if (is_debug_pointer(obj)) {
                //rtgc_log(true, "garbage marked %p\n", obj);
                AnchorIterator it(obj);
                while (it.hasNext()) {
                    GCObject* anchor = it.next();
                    //rtgc_log(true, " - anchor %p\n", anchor);
                    assert(anchor->isGarbageMarked(), "%p[%d], %p[%d]", 
                        anchor, anchor->getShortcutId(), obj, obj->getShortcutId());
                }
            }
        }
    }
#endif        
    _visitedNodes.resize(0);
    _trackers.resize(0);
    return hasSurvivalPath;
}

bool PathFinder::findSurvivalPath(ShortOOP& tail) {
    AnchorIterator* it = _trackers.push_empty();
    it->initSingleIterator(&tail);
    while (true) {
        while (!it->hasNext()) {
            _trackers.pop_back();
            if (_trackers.empty()) return false;

            it = &_trackers.back();
            GCObject* top = it->peekPrev();
            rtgc_log(LOG_OPT(7), "SurvivalPath pop %p[%d]\n", 
                top, _trackers.size());
            if (!top->isGarbageMarked()) {
                /** 
                 * L:SHRINK_ANCHOR 에 의해서 top 이 속한 부분이 shortcut 에서 제외된 경우,
                 * 1) top 역시 garbage marking 되거나, 
                 * 2) 그 전에 root-ref 를 만나 tracing 이 종료된다.
                 */

                SafeShortcut* shortcut = top->getShortcut();
                precond(shortcut->inTracing());
                precond(top->hasReferrer());
                postcond(!_visitedNodes.contains(top));

                shortcut->unmarkInTracing();
                rtgc_log(LOG_OPT(7), "shortcut poped %p:%d anchor=%p\n", 
                    top, top->getShortcutId(), (void*)shortcut->anchor());
                shortcut->shrinkAnchorTo(top);
                it = _trackers.push_empty();
                top->initIterator(it);
                top->markGarbage("poped shortcut ramp");
                _visitedNodes.push_back(top);
                break;
            }
        }

        GCObject* R = it->next();
        SafeShortcut* shortcut = R->getShortcut();
        precond(shortcut != NULL);

        if (R->isGarbageMarked() || shortcut->inContiguousTracing(R, &shortcut)) {
            rtgc_log(LOG_OPT(7), "pass marked %p:%d[%d](gm=%d)\n", 
                R, R->getShortcutId(), _trackers.size(), R->isGarbageMarked());
            continue;
        }

        if (R->getRootRefCount() > ZERO_ROOT_REF) {
            rtgc_log(LOG_OPT(7), "SurvivalPath found %p[%d]\n", R, _trackers.size());
            return true;
        }

        rtgc_log(LOG_OPT(7), "findSurvivalPath %p:%d[%d] vn=%d\n", 
            R, R->getShortcutId(), _trackers.size(), _visitedNodes.size());

        if (R->hasReferrer()) {
            it = _trackers.push_empty();
            if (shortcut->isValid()) {
                debug_only(shortcut->vailidateShortcut();)
                GCObject* warp_target = shortcut->anchor_ref();
                if (!warp_target->isGarbageMarked()) {//} && !warp_target->getShortcut()->inTracing()) {
                    it->initSingleIterator(&shortcut->anchor_ref());
                    shortcut->markInTracing(R);
                    rtgc_log(LOG_OPT(7), "shortcut[%d] pushed %p(anchor = %p->%p)\n", 
                        shortcut->getIndex(shortcut), R, (void*)shortcut->anchor(), (void*)shortcut->tail());
                    continue;
                }
                else {
                    // L:SHRINK_ANCHOR
                    rtgc_log(LOG_OPT(7), "shrink shortcut[%d] anchor to (anchor = %p->%p)\n", 
                        shortcut->getIndex(shortcut), R, (void*)shortcut->tail());
                    shortcut->shrinkAnchorTo(R);
                }
            }
            R->initIterator(it);
        }

        R->markGarbage("path-finder");
        _visitedNodes.push_back(R);
    }
}

void PathFinder::constructShortcut() {
    for (int i = _visitedNodes.size(); --i >= 0; ) {
        GCObject* obj = (GCObject*)_visitedNodes.at(i);
        obj->unmarkGarbage();
    }

    const int MAX_SHORTCUT_LEN = 256;
    AnchorIterator* ait = _trackers.adr_at(0);
    AnchorIterator* end = ait + _trackers.size() - 1;
    if (ait >= end) {
        return;
    }
    GCObject* tail = NULL;
    GCObject* link = NULL;
    SafeShortcut* lastShortcut = NULL;
    int cntNode = 0;
    for (; ait < end; ait++) {
        GCObject* obj = ait->peekPrev();        
        rtgc_log(LOG_OPT(7), "link(%p) to anchor(%p)%d\n", link, obj, obj->getShortcutId());
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
                rtgc_log(LOG_OPT(0x11), "SafeShortcut::create max len [%d]\n", lastShortcut->getIndex());
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
                    rtgc_log(LOG_OPT(0x11), "extend Shortcut tail %d\n", ss->getIndex());
                #ifdef ASSERT
                    for (GCObject* n = tail; n != obj; n = n->getSafeAnchor()) {
                        precond(n != ss->anchor());
                    }
                #else
                    int tail_len = 0;
                    GCObject* next;
                    for (GCObject* n = tail; n != obj; n = next) {
                        tail_len ++;
                        next = n->getSafeAnchor();
                        if (n == ss->anchor()) {
                            SafeShortcut::create(next, tail, cntNode);
                            tail = next;//n->getSafeAnchor();
                            break;
                        }
                    }
                #endif
                    ss->extendTail(tail);
                }
                else if (lastShortcut != NULL) {
                    rtgc_log(LOG_OPT(0x11), "extend Shortcut anchor %d\n", ss->getIndex());
                    lastShortcut->extendAnchor(obj);
                }
                else {
                    SafeShortcut* s2 = SafeShortcut::create(obj, tail, cntNode);
                    rtgc_log(LOG_OPT(0x11), "insert Shortcut %d\n", s2->getIndex());
                }
                cntNode = 0;
            }
            link = NULL;
            lastShortcut = ss;
        }
    }
    
    GCObject* root = ait->peekPrev();        
    precond(root == NULL || root->getRootRefCount() > ZERO_ROOT_REF);
    if (link != NULL) {
        /** root 가 속한 shortcut 은 무시된다.
         * 해당 shortcut 이 valid 한 지는 현재 확인되지 않았다. 
         */
        link->setSafeAnchor(root);
        if (lastShortcut != NULL) {
            rtgc_log(LOG_OPT(0x11), "SafeShortcut::create 3-1\n")
            lastShortcut->extendAnchor(root);
        } else {
            rtgc_log(LOG_OPT(0x11), "SafeShortcut::create 3\n")
            SafeShortcut::create(root, tail, cntNode);
        }
    }
    // last anchor may not have safe-anchor
}

static bool clear_garbage_links(GCObject* link, GCObject* garbageAnchor, SimpleVector<GCObject*>* unsafeObjects) {
    // if (link->isGarbageMarked() || !link->isTrackable()) return false;

    // if (link->removeReferrer(garbageAnchor) == 0
    // &&  link->isUnsafe()) {
    //     if (!link->isAnchored()) {
    //         link->markGarbage();
    //         link->removeAnchorList();
    //         rtgc_log(LOG_OPT(7), "Mark new garbage.\n");
    //         return true;
    //     }
    //     rtgc_log(LOG_OPT(7), "Add unsafe objects.\n");
    //     unsafeObjects->push_back(link);
    // }
    return false;
}


void GarbageProcessor::collectGarbage(GCObject** ppNode, int cntNode) {
    GCObject** end = ppNode + cntNode;
    PathFinder pf;
    for (; ppNode < end; ppNode ++) {
        GCObject* node = *ppNode;
        if (node->isGarbageMarked()) continue;
        pf.constructSurvivalPath(node);

    }
    
}

// bool GarbageProcessor::detectUnreachable(GCObject* unsafeObj, SimpleVector<GCObject*>& unreachableNodes) {
//     precond(!unsafeObj->isGarbageMarked());
//     int cntUnreachable = unreachableNodes.size();

//     rtgc_log(LOG_OPT(7), "detectUnreachable %p\n", unsafeObj);
//     PathFinder pf(unreachableNodes);
//     bool hasSurvivalPath = pf.constructSurvivalPath(unsafeObj);

//     if (hasSurvivalPath) {
//         pf.constructShortcut();
//         unreachableNodes.resize(cntUnreachable);
//         return false;
//     }
//     return true;
// }

bool GarbageProcessor::detectGarbage(GCObject* unsafeObj) {
    PathFinder pf;
    while (true) {
        rtgc_log(LOG_OPT(1), "scan Garbage %p\n", unsafeObj);
        bool hasSurvivalPath = pf.constructSurvivalPath(unsafeObj);

        return hasSurvivalPath;
#if 0        
        else {
            for (int i = _visitedNodes.size(); --i >= 0; ) {
                GCObject* obj = (GCObject*)_visitedNodes.at(i);
                //rtgc_log(LOG_OPT(7), "garbage deteted %p(%s)\n", obj, RTGC::getClassName(obj));
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
#endif         
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
                ||  link->isAnchored()) {
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
    prev->setShortcutId_unsafe(INVALID_SHORTCUT);
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
        SafeShortcut::create(newAnchor, _tail, MIN_SHORTCUT_LENGTH+1, true);
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
    assert(newAnchor->getShortcut() == this, "invalid anchor %p[%d]\n", newAnchor, this->getIndex(this));
    precond(!this->inTracing());
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