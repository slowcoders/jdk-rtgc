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
    // _trackers.initialize();
}


bool GarbageProcessor::hasUnsafeObjects() {
    rtgc_log(_unsafeObjects.size() + _visitedNodes.size() != 0, "unsafe %d, garbage %d\n", _unsafeObjects.size(), _visitedNodes.size());
    return _unsafeObjects.size() + _visitedNodes.size() > 0;
}


bool GarbageProcessor::clear_garbage_links(GCObject* link, GCObject* garbageAnchor) {
    rt_assert(!rtHeapEx::g_lock_unsafe_list);
    rt_assert(garbageAnchor->isTrackable());
    if (true) {
        link->removeReferrer(garbageAnchor);
        // rtgc_debug_log(link, "clear_garbage_links %p->%p(ac=%d, remain=%d)\n", 
        //         garbageAnchor, link, link->getAnchorCount(), link->hasReferrer(garbageAnchor));
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
        if (detectGarbage(node)) {
            node->markGarbage();
            _visitedNodes.push_back(node);
        } else {
            node->unmarkUnstable();
        }
        // rt_assert(node->isGarbageMarked() || !node->isUnstableMarked());
    }
}

void GarbageProcessor::destroyDetectedGarbage(bool isTenured) {
    rtgc_log(LOG_OPT(1), "destroyGarbages %d\n", _visitedNodes.size()); 
    GCObject** ppNode = _visitedNodes.adr_at(0);
    GCObject** end = ppNode + _visitedNodes.size();
    for (; ppNode < end; ppNode++) {
        GCObject* obj = *ppNode;
        if (obj->isGarbageMarked()) {
                destroyObject(obj, isTenured);
        } else {
            // garbage resurrected!
        }
    }
    _visitedNodes.resize(0);
}

void GarbageProcessor::destroyObject(GCObject* obj, bool isTenured) {
    rt_assert(!obj->hasCircuit());
    rt_assert(obj->isGarbageMarked());
    obj->clearAnchorList();
    rtgc_debug_log(obj, "destroyObject %p(%s) YR=%d", 
        obj, RTGC::getClassName(obj), obj->isYoungRoot());    
    RuntimeHeap::scanInstanceGraph(obj, (RTGC::RefTracer2)clear_garbage_links, &this->_unsafeObjects, isTenured);
    RuntimeHeap::reclaimObject(obj);
}

void GarbageProcessor::validateGarbageList() {
    GCObject** ppStart = _visitedNodes.adr_at(0);
    int cntNode = _visitedNodes.size();
    GCObject** ppNode = ppStart + cntNode;
    for (int i = cntNode; --i >= 0; ) {
        GCObject* obj = *(--ppNode);
        if (!obj->isUnreachable()) {
            _visitedNodes->removeFast(i);
        }
    }
}


bool GarbageProcessor::detectGarbage(GCObject* node) {
    rt_assert(!rtHeapEx::g_lock_garbage_list);
    rt_assert(node->isTrackable());
    return node->isUnreachable();
}


