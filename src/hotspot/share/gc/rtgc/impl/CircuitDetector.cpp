#include "GCObject.hpp" 
#include "GCRuntime.hpp" 
#include "classfile/javaClasses.inline.hpp"
#include "../RTGC.hpp"
#include "../rtgcGlobals.hpp"
#include "../rtgcHeap.hpp"
#include "../rtHeapEx.hpp"

#define USE_ITERATOR_STACK false
#define USE_ANCHOR_VISIOR true

#define RECLAIM_GARBAGE_IMMEDIATELY     false
static const int GC_VERBOSE_LOG = false;

using namespace RTGC;


static const int LOG_OPT(int function) {
  return RTGC::LOG_OPTION(RTGC::LOG_SCANNER, function);
}

void GarbageProcessor::initialize() {
    this->delete_q = NULL;
    _unsafeObjects.initialize();
    _visitedNodes.initialize();
    _trackers.initialize();
}

void GarbageProcessor::clearReachableShortcutMarks() {
#if ENABLE_REACHBLE_SHORTCUT_CACHE    
    for (SafeShortcut* path = reachableShortcurQ; path != NULL; path = path->nextReachable()) {
        path->unmarkReachable();
    }
#endif
}

bool GarbageProcessor::scanSurvivalPath(GCObject* node, bool scanStrongPathOnly) {
    // rtgc_debug_log(node, "scanSurvivalPath %p(%s)", node, getClassName(node));

    ShortOOP tail = node;
    rt_assert(EnableRTGC);
    rt_assert(node->isTrackable());
    int trace_top = _visitedNodes.size();
    bool hasSurvivalPath;
    if (scanStrongPathOnly) {
      hasSurvivalPath = findSurvivalPath<true>(tail);
    } else {
      hasSurvivalPath = findSurvivalPath<false>(tail);
    }
    if (hasSurvivalPath) {
        if (_visitedNodes.size() != trace_top) {
            for (int i = _visitedNodes.size(); --i >= trace_top; ) {
                GCObject* obj = (GCObject*)_visitedNodes.at(i);
                obj->unmarkGarbage();
            }
            _visitedNodes.resize(trace_top);
        }
        constructShortcut();
    }
#if 0 //def ASSERT        
    else {
        for (int i = _visitedNodes.size(); --i >= trace_top; ) {
            GCObject* obj = (GCObject*)_visitedNodes.at(i);
            rt_assert(obj->isTrackable());
            if (is_debug_pointer(obj)) {
                //rtgc_log(1, "garbage marked %p\n", obj);
                AnchorIterator it(obj);
                while (it.hasNext()) {
                    GCObject* anchor = it.next();
                    //rtgc_log(1, " - anchor %p\n", anchor);
                    rt_assert_f(anchor->isGarbageMarked(), "%p[%d], %p[%d]", 
                        anchor, anchor->getShortcutId(), obj, obj->getShortcutId());
                }
            }
        }
    }
#endif        
    _trackers.resize(0);
    return hasSurvivalPath;
}

template<bool scanStrongPathOnly>
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
                rt_assert(shortcut->inTracing());
                rt_assert(top->hasAnchor());
                //rt_assert(!_visitedNodes.contains(top));

                shortcut->unmarkInTracing();
                rtgc_log(LOG_OPT(7), "shortcut poped %p:%d anchor=%p\n", 
                    top, top->getShortcutId(), (void*)shortcut->anchor());
                shortcut->shrinkAnchorTo(top);
                it = _trackers.push_empty();
                it->initialize(top);
                top->markGarbage(NULL);
                _visitedNodes.push_back(top);
                break;
            }
        }

        GCObject* R = it->next();
        SafeShortcut* shortcut = R->getShortcut();

        if (R->isGarbageMarked() || shortcut->inContiguousTracing(R, &shortcut)) {
            rtgc_log(LOG_OPT(7), "pass marked %p:%d[%d](gm=%d)\n", 
                R, R->getShortcutId(), _trackers.size(), R->isGarbageMarked());
            continue;
        }

        if (R->getRootRefCount() > (scanStrongPathOnly ? 1 : 0)) {
            rtgc_log(LOG_OPT(7), "SurvivalPath found %p[%d]\n", R, _trackers.size());
            return true;
        }

        rtgc_log(LOG_OPT(7), "findSurvivalPath %p:%d[%d] vn=%d\n", 
            R, R->getShortcutId(), _trackers.size(), _visitedNodes.size());

        if (R->hasAnchor()) {
            it = _trackers.push_empty();
            if (shortcut->isValid()) {
                debug_only(shortcut->vailidateShortcut(R);)
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
            it->initialize(R);
        }

        R->markGarbage(NULL);
        // rt_assert(!_visitedNodes.contains(R));
        _visitedNodes.push_back(R);
    }
}

bool GarbageProcessor::hasUnsafeObjects() {
    rtgc_log(_unsafeObjects.size() + _visitedNodes.size() != 0, "unsafe %d, garbage %d\n", _unsafeObjects.size(), _visitedNodes.size());
    return _unsafeObjects.size() + _visitedNodes.size() > 0;
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
        rtgc_log(LOG_OPT(10), "link(%p) to anchor(%p)%d\n", link, obj, obj->getShortcutId());

        if (obj->isDirtyAnchor()) {
            if (link != NULL) {
                SafeShortcut* s2 = SafeShortcut::create(link, tail, cntNode);
                if (!link->isSurvivorReachable()) {
                    rt_assert(link->isDirtyReferrerPoints());
                    link->markSurvivorReachable_unsafe();
                }
                rtgc_log(LOG_OPT(10), "dirty anchor  Shortcut %d\n", s2->getIndex());
                cntNode = 0;
                if (lastShortcut != NULL) {
                    if (link != lastShortcut->anchor()) {
                        rtgc_log(LOG_OPT(10), "extend Shortcut anchor 22 %p:%d\n", link, lastShortcut->getIndex());
                        lastShortcut->extendAnchor(link);
                    }
                    lastShortcut = NULL;
                }
                link = NULL;
            }
            else {
                rt_assert(cntNode == 0);
                lastShortcut = NULL;
            }
            // if (tail != NULL) {
            //     rtgc_log(LOG_OPT(10), "mark_dirty_survivor_reachable %p -> %p", obj, tail);
            //     rtHeap::mark_survivor_reachable(cast_to_oop(tail));
            //     tail = NULL;
            // }
            continue;
        }


        if (link != NULL) {
            rt_assert_f(link->hasAnchor(),
                "link has no anchor %p:%d\n", obj, obj->getShortcutId());
            link->setSafeAnchor(obj);
        } else {
            precond (lastShortcut == NULL || obj == lastShortcut->anchor());
        }

        rt_assert_f(SafeShortcut::isValidIndex(obj->getShortcutId()),
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
            rt_assert_f(ait+1 == end || ait[+1].peekPrev() == ss->anchor(), 
                "invalid shortcut %p:%d\n", obj, obj->getShortcutId());
            rt_assert(ss->inTracing() || (ait+1 == end && obj->getRootRefCount() > ZERO_ROOT_REF));
            ss->unmarkInTracing();
            if (cntNode > 0) {
                if (obj == ss->tail()) {
                    rtgc_log(LOG_OPT(10), "extend Shortcut tail %d\n", ss->getIndex());
                    ss->extendTail(tail);
                }
                else if (lastShortcut != NULL) {
                    rtgc_log(LOG_OPT(10), "extend Shortcut anchor %p:%d\n", obj, ss->getIndex());
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
    rt_assert(root == NULL || root->getRootRefCount() > ZERO_ROOT_REF);
    if (link != NULL) {
        if (root->isDirtyAnchor()) {
            SafeShortcut* s2 = SafeShortcut::create(link, tail, cntNode);
            if (!link->isSurvivorReachable()) {
                rt_assert(link->isDirtyReferrerPoints());
                link->markSurvivorReachable_unsafe();
            }
        } 
        else {
            link->setSafeAnchor(root);
            if (lastShortcut != NULL) {
                /** root 가 속한 shortcut 은 무시된다. (shortcut 의 anchor 는 shortcut 에 포함되지 않는다)
                 * 해당 shortcut 이 valid 한 지는 현재 확인되지 않았다. 
                 */
                lastShortcut->extendAnchor(root);
            } else {
                rtgc_log(LOG_OPT(10), "SafeShortcut::create 3\n")
                SafeShortcut::create(root, tail, cntNode);
            }
        }
    }
    // last anchor may not have safe-anchor
}


bool GarbageProcessor::clear_garbage_links(GCObject* link, GCObject* garbageAnchor) {
    rt_assert(!rtHeapEx::g_lock_unsafe_list);
    rt_assert(garbageAnchor->isTrackable());
    if (true) {
        link->removeReferrer(garbageAnchor);
        rtgc_debug_log(link, "clear_garbage_links %p->%p(ac=%d, remain=%d)\n", 
                garbageAnchor, link, link->getAnchorCount(), link->hasReferrer(garbageAnchor));
        return false;//link->isUnstableMarked();
    }

    if (link->removeMatchedReferrers(garbageAnchor)) {
        if (link->isUnsafeTrackable() && !link->isUnstableMarked()) {
            link->markUnstable();
            rtgc_debug_log(link, "marked unsafe to check garbage %p\n", link);
            return true;
        }
    }
    return false;
}


void GarbageProcessor::addUnstable_ex(GCObject* obj) {
    _unsafeObjects.push_back(obj);
}

void GarbageProcessor::addUnstable(GCObject* obj) {
    rtgc_debug_log(obj, "add unsafe=%p", obj);
    rt_assert(!rtHeapEx::g_lock_unsafe_list);
    rt_assert(obj->isTrackable());
    rt_assert(!obj->isUnstableMarked());
    obj->markUnstable();
    addUnstable_ex(obj);
}

void GarbageProcessor::collectAndDestroyGarbage(bool isTenured) {
    GCObject** ppNode = _unsafeObjects.adr_at(0);
    destroyDetectedGarbage(isTenured);

    for (int cntUnsafe; (cntUnsafe = _unsafeObjects.size()) > 0; ) {
        scanGarbage(ppNode, cntUnsafe);
        _unsafeObjects.resize(0);
        destroyDetectedGarbage(isTenured);
    } 
}

void GarbageProcessor::scanGarbage(GCObject** ppNode, int cntUnsafe) {
    rtgc_log(LOG_OPT(1), "scanGarbage cntUnsafe %d\n", cntUnsafe); 
    GCObject** end = ppNode + cntUnsafe;
    for (; ppNode < end; ppNode ++) {
        GCObject* node = *ppNode;
        if (!node->hasSafeAnchor()) {
            bool isGarbage = detectGarbage(node);
        } else {
            node->unmarkUnstable();
        }
        rt_assert(node->isGarbageMarked() || !node->isUnstableMarked());
    }
}

void GarbageProcessor::destroyDetectedGarbage(bool isTenured) {
    rtgc_log(LOG_OPT(1), "destroyGarbages %d\n", _visitedNodes.size()); 
    GCObject** ppNode = _visitedNodes.adr_at(0);
    GCObject** end = ppNode + _visitedNodes.size();
    for (; ppNode < end; ppNode++) {
        GCObject* obj = *ppNode;
        if (obj->isGarbageMarked()) {
            if (!obj->isDirtyAnchor()) {
                destroyObject(obj, (RTGC::RefTracer2)clear_garbage_links, isTenured);
            }
        } else {
            // garbage resurrected!
        }
    }
    _visitedNodes.resize(0);
}

void GarbageProcessor::destroyObject(GCObject* obj, RefTracer2 instanceScanner, bool isTenured) {
    rt_assert(!obj->hasShortcut());
    rt_assert(obj->isGarbageMarked());
    obj->clearAnchorList();
    rtgc_debug_log(obj, "destroyObject %p(%s) YR=%d", 
        obj, RTGC::getClassName(obj), obj->isYoungRoot());    
    RuntimeHeap::scanInstanceGraph(obj, instanceScanner, &this->_unsafeObjects, isTenured);
    RuntimeHeap::reclaimObject(obj);
}

void GarbageProcessor::validateGarbageList() {
    GCObject** ppStart = _visitedNodes.adr_at(0);
    int cntNode = _visitedNodes.size();
    GCObject** ppNode = ppStart + cntNode;
    for (int i = cntNode; --i >= 0; ) {
        GCObject* obj = *(--ppNode);
        if (!obj->isGarbageMarked()) {
            _visitedNodes->removeFast(i);
        }
    }
}


bool GarbageProcessor::detectGarbage(GCObject* node) {
    rt_assert(!rtHeapEx::g_lock_garbage_list);
    if (node->isGarbageMarked()) {
        // rt_assert_f(checkBrokenLink || node->isDestroyed() || _visitedNodes.contains(node), 
        //     "incorrect marked garbage %p(%s)\n", node, getClassName(node));
        return true;
    }
    rt_assert(node->isTrackable());
    if (node->isUnreachable()) {
        node->markGarbage("scanGarbage");
        _visitedNodes.push_back(node);
        return true;
    }
    node->unmarkUnstable();
    if (node->getRootRefCount() > 0) {
        return false;
    }

    scanSurvivalPath(node, false);
    if (node->isGarbageMarked()) {
        rtgc_debug_log(node, "garbage marked on %p\n", node);
        return true;
    }
    return false;
}


bool GarbageProcessor::resolveStrongSurvivalPath(GCObject* node) {
    rt_assert(!node->isGarbageMarked());
    rt_assert(node->isTrackable());
    node->unmarkUnstable();
    if (node->isStrongRootReachable()) {
        return true;
    }

    int trace_top = _visitedNodes.size();
    if (!scanSurvivalPath(node, true)) {
        if (_visitedNodes.size() != trace_top) {
            for (int i = _visitedNodes.size(); --i >= trace_top; ) {
                GCObject* obj = (GCObject*)_visitedNodes.at(i);
                obj->unmarkGarbage();
            }
            _visitedNodes.resize(trace_top);
        }
        return false;
    }
    return true;
}

bool GarbageProcessor::hasStableSurvivalPath(GCObject* tail) {
    GCObject* node = tail;
    if (node->isGarbageMarked()) {
        //rt_assert_f(node->isDestroyed() || _visitedNodes.contains(node), "incrrect marked garbage %p\n", node);
        return false;
    }
    if (node->isUnstableMarked()) {
        return false;
    }
    while (node->getRootRefCount() == 0) {
        if (!node->hasSafeAnchor()) {
            addUnstable(tail);
            return false;
        }

        if (node->hasShortcut()) {
            node = node->getShortcut()->anchor();
        } else {
            node = node->getSafeAnchor();
        }
    } 
    return true;
}

AnchorState GarbageProcessor::checkAnchorStateFast(GCObject* tail, bool enableShortcut) {
    bool multi_anchor_found = false;
    const int MAX_PATH_LENGTH_FOR_FAST_CHECK = 20;
    GCObject* node = tail;
    for (int i = MAX_PATH_LENGTH_FOR_FAST_CHECK; --i >= 0; ) {
        if (node->isGarbageMarked()) {
            rtgc_log(1, "garbage anchor at %d ", i);
            node = tail;
            for (int j = MAX_PATH_LENGTH_FOR_FAST_CHECK; --j >= i; ) {
                rtgc_log(1, "garbage anchor %d " PTR_DBG_SIG, j, PTR_DBG_INFO(node));
                if (j == i + 1) {
                    for (AnchorIterator ai(node); ai.hasNext(); ) {
                        GCObject* p = ai.next();
                        rtgc_log(true, "__ %p", p);
                    }
                }

                rt_assert(!node->isGarbageMarked());
                if (enableShortcut && node->hasShortcut()) {
                    /* shortcut 은 multi_ref 로 간주한다. */
                    multi_anchor_found = true;
                    node = node->getShortcut()->anchor();
                } else {
                    multi_anchor_found |= node->hasMultiRef();
                    node = node->getSafeAnchor();
                }
            }
        }

        if (node->getRootRefCount() > 0) {
            return AnchorState::AnchoredToRoot;
        }

        if (!node->hasSafeAnchor()) {
            if (!multi_anchor_found && !node->hasAnchor()) {
                return AnchorState::NotAnchored;
            }
            break;
        }
        if (node->hasShortcut()) {
            /* shortcut 은 multi_ref 로 간주한다. */
            multi_anchor_found = true;
            node = node->getShortcut()->anchor();
        } else {
            multi_anchor_found |= node->hasMultiRef();
            node = node->getSafeAnchor();
        }
    } 
    return AnchorState::Unknown;
}