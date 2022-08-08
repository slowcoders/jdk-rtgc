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

// class GarbageProcessor {
// public:    
//     SimpleVector<GCObject*> _unsafeObjects;
//     HugeArray<GCObject*>& _visitedNodes;
//     SimpleVector<AnchorIterator> _trackers;
//     SafeShortcut* reachableShortcurQ;

//     bool findSurvivalPath(ShortOOP& tail);

// public:
//     GarbageProcessor(HugeArray<GCObject*>& visitedNodes)
//       : _visitedNodes(visitedNodes), reachableShortcurQ(NULL) {}

//     ~GarbageProcessor() { clearReachableShortcutMarks(); }

//     bool scanSurvivalPath(GCObject* tail);
//     void constructShortcut();
//     void clearReachableShortcutMarks();
// };

void GarbageProcessor::clearReachableShortcutMarks() {
#if ENABLE_REACHBLE_SHORTCUT_CACHE    
    for (SafeShortcut* path = reachableShortcurQ; path != NULL; path = path->nextReachable()) {
        path->unmarkReachable();
    }
#endif
}

bool GarbageProcessor::scanSurvivalPath(GCObject* node) {
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
        for (int i = _visitedNodes.size(); --i >= trace_top; ) {
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

bool GarbageProcessor::findSurvivalPath(ShortOOP& tail) {
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

void GarbageProcessor::constructShortcut() {
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
            precond(ss->inTracing() || (ait+1 == end && obj->getRootRefCount() > ZERO_ROOT_REF));
            ss->unmarkInTracing();
            if (cntNode > 0) {
                if (obj == ss->tail()) {
                #ifdef ASSERT
                    for (GCObject* n = tail; n != obj; n = n->getSafeAnchor()) {
                        precond(n != ss->anchor());
                    }
                #elif 0
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
                    rtgc_log(LOG_OPT(10), "extend Shortcut tail %d\n", ss->getIndex());
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


bool GarbageProcessor::clear_garbage_links(GCObject* link, GCObject* garbageAnchor) {
    rtgc_debug_log(garbageAnchor, "clear_garbage_links %p->%p\n", garbageAnchor, link);
    if (!link->removeMatchedReferrers(garbageAnchor)) {
        rtgc_debug_log(garbageAnchor, "unknown link %p->%p\n", garbageAnchor, link);
        return false;
    }
    if (link->isTrackable() && link->isUnsafe()) {
        rtgc_log(LOG_OPT(14), "Add unsafe objects %p\n", link);
        return true;
    } 
    return false;
}


void GarbageProcessor::addUnstable(GCObject* obj) {
    precond(obj->isUnsafe());
    if (!obj->isUnstableMarked()) {
        obj->markUnstable();
        _unsafeObjects.push_back(obj);
    }
}

void GarbageProcessor::collectGarbage() {
    collectGarbage(_unsafeObjects.adr_at(0), _unsafeObjects.size());
}

void GarbageProcessor::collectGarbage(GCObject** ppNode, int cntUnsafe) {
    while (cntUnsafe > 0) {
        rtgc_log(LOG_OPT(14), "collectGarbage cntUnsafe %d\n", cntUnsafe); 
        GCObject** end = ppNode + cntUnsafe;
        for (; ppNode < end; ppNode ++) {
            GCObject* node = *ppNode;
            if (node->isGarbageMarked()) {
                precond(node->isDestroyed() || _visitedNodes.contains(node));
            } else if (node->isUnreachable()) {
                node->markGarbage("collectGarbage");
                _visitedNodes.push_back(node);
            } else {
                scanSurvivalPath(node);
            }
        }

        _unsafeObjects.resize(0);

        ppNode = _visitedNodes.adr_at(0);
        end = ppNode + _visitedNodes.size();
        for (; ppNode < end; ppNode++) {
            GCObject* obj = *ppNode;
            destroyObject(obj, (RTGC::RefTracer2)clear_garbage_links);
        }
        _visitedNodes.resize(0);

        cntUnsafe = _unsafeObjects.size();
        ppNode = _unsafeObjects.adr_at(0);
    }
}

void GarbageProcessor::destroyObject(GCObject* obj, RefTracer2 instanceScanner) {
    obj->removeAnchorList();
    rtgc_debug_log(obj, "destroyObject %p(%s) YR=%d\n", 
        obj, RTGC::getClassName(obj), obj->isYoungRoot());    
    RuntimeHeap::scanInstanceGraph(obj, instanceScanner, &this->_unsafeObjects);
    RuntimeHeap::reclaimObject(obj);
}

bool GarbageProcessor::detectGarbage(GCObject* unsafeObj) {
    fatal("not impl");
    return false;
}

