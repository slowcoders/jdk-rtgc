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
	SimpleVector<GCObject*> _unsafeObjects;
    HugeArray<GCObject*>& _visitedNodes;
    SimpleVector<AnchorIterator> _trackers;
    SafeShortcut* reachableShortcurQ;

    bool findSurvivalPath(ShortOOP& tail);

public:
    PathFinder(HugeArray<GCObject*>& visitedNodes)
      : _visitedNodes(visitedNodes), reachableShortcurQ(NULL) {}

    ~PathFinder() { clearReachableShortcutMarks(); }

    bool scanSurvivalPath(GCObject* tail);
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

bool PathFinder::scanSurvivalPath(GCObject* node) {
    ShortOOP tail = node;
    precond(EnableRTGC);
    int trace_top = _visitedNodes.size();
    bool hasSurvivalPath = findSurvivalPath(tail);
    if (hasSurvivalPath) {
        if (_visitedNodes.size() != trace_top) {
            for (int i = _visitedNodes.size(); --i >= trace_top; ) {
                GCObject* obj = (GCObject*)_visitedNodes.at(i);
                obj->unmarkGarbage();
            }
            // rtgc_log(true, "resize %d to %d\n", _visitedNodes.size(), trace_top);
            _visitedNodes.resize(trace_top);
        }
        constructShortcut();
    }
#ifdef ASSERT        
    else {
        for (int i = _visitedNodes.size(); --i >= 0; ) {
            GCObject* obj = (GCObject*)_visitedNodes.at(i);
            precond(obj->isTrackable());
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
                //postcond(!_visitedNodes.contains(top));

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
        // postcond(!_visitedNodes.contains(R));
        _visitedNodes.push_back(R);
    }
}

void PathFinder::constructShortcut() {
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
                rtgc_log(LOG_OPT(10), "SafeShortcut::create max len [%d]\n", lastShortcut->getIndex());
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
                    rtgc_log(LOG_OPT(10), "extend Shortcut tail %d\n", ss->getIndex());
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
                    rtgc_log(LOG_OPT(10), "extend Shortcut anchor %d\n", ss->getIndex());
                    lastShortcut->extendAnchor(obj);
                }
                else {
                    SafeShortcut* s2 = SafeShortcut::create(obj, tail, cntNode);
                    rtgc_log(LOG_OPT(10), "insert Shortcut %d\n", s2->getIndex());
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
            lastShortcut->extendAnchor(root);
        } else {
            rtgc_log(LOG_OPT(10), "SafeShortcut::create 3\n")
            SafeShortcut::create(root, tail, cntNode);
        }
    }
    // last anchor may not have safe-anchor
}

static bool clear_garbage_links(GCObject* link, GCObject* garbageAnchor, PathFinder* pf) {
    rtgc_log(LOG_OPT(4), "clear_garbage_links %p->%p\n", 
        garbageAnchor, link);    
    if (link->isGarbageMarked()) {
        // precond(pf->_visitedNodes.contains(link));
        return false;
    }

    link->removeMatchedReferrers(garbageAnchor);
    if (link->isTrackable() && link->isUnsafe()) {
        if (!link->isAnchored()) {
            link->markGarbage();
            pf->_visitedNodes.push_back(link);
        } else {
            pf->_unsafeObjects->push_back(link);
        }
        rtgc_log(LOG_OPT(4), "Add unsafe objects %p\n", link);
    } else {
        rtgc_log(LOG_OPT(4), "unkown link %p->%p\n", garbageAnchor, link);
    }

    return false;
}



void GarbageProcessor::collectGarbage(GCObject** ppNode, int cntNode, HugeArray<GCObject*>& garbages, int cntGarbage) {
    GCObject** end = ppNode + cntNode;
    PathFinder pf(garbages);

    while (true) {
        for (; ppNode < end; ppNode ++) {
            GCObject* node = *ppNode;
            rtgc_log(LOG_OPT(4), "tr node %p\n", node);
            if (node->isGarbageMarked()) {
                // precond(garbages.contains(node));
            } else if (!node->isAnchored()) {
                node->markGarbage();
                garbages.push_back(node);
            } else {
                pf.scanSurvivalPath(node);
            }
        }

        pf._unsafeObjects.resize(0);
        for (;cntGarbage < garbages.size(); cntGarbage++) {
            GCObject* obj = garbages.at(cntGarbage);
            obj->removeAnchorList();
            rtgc_log(LOG_OPT(4), "clear_garbage_links %p(%s)\n", 
                obj, RTGC::getClassName(obj));    
            RTGC::scanInstanceGraph(obj, (RTGC::RefTracer3)clear_garbage_links, &pf);

        }

        int cntUnsafe = pf._unsafeObjects.size();
        if (cntUnsafe == 0) {
            return;
        }

        rtgc_log(LOG_OPT(4), "unsafe %d\n", cntUnsafe);
        cntGarbage = garbages.size();
        
        ppNode = pf._unsafeObjects.adr_at(0);
        end = ppNode + cntUnsafe;
    }
#ifdef ASSERT
    ppNode = garbages.adr_at(0);
    end = ppNode + garbages.size();
    for (; ppNode < end; ppNode ++) {
        postcond(!ppNode[0]->isAnchored() && ppNode[0]->isGarbageMarked());
    }
#endif    
}

bool GarbageProcessor::detectGarbage(GCObject* unsafeObj) {
    fatal("not impl");
    return false;
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
                        fatal("not impl!");
                        // _unsafeObjects.push_back(link);
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
    rtgc_log(RTGC::is_debug_pointer(leftTail) && java_lang_ref_Reference::is_phantom(cast_to_oop(leftTail)), 
        "REF_PHANTOM[%d] shotcut split=%p(%s) rightAnchor=%p(%s)\n", 
        getIndex(this), leftTail, RTGC::getClassName(leftTail), 
                        rightAnchor, RTGC::getClassName(rightAnchor));

    assert(this->isValid(), "shotcut[%d] is invalid leftTail=%p(%s) rightAnchor=%p(%s)\n", 
        getIndex(this), leftTail, RTGC::getClassName(leftTail), 
                        rightAnchor, RTGC::getClassName(rightAnchor));
    precond(rightAnchor->getShortcut() == this);
    precond(leftTail->getShortcut() == this || leftTail == this->anchor());
    rightAnchor->setShortcutId_unsafe(0);

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
        obj->setShortcutId_unsafe(INVALID_SHORTCUT);
    }
    return true;
}

void SafeShortcut::shrinkAnchorTo(GCObject* newAnchor) {
    assert(newAnchor->getShortcut() == this, "invalid anchor %p[%d]\n", newAnchor, this->getIndex(this));
    precond(!this->inTracing());
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
