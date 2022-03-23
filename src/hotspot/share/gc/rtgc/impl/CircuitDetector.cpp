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
static const int MIN_SHORTCUT_LENGTH = 3;

static const int LOG_OPT(int function) {
  return RTGC::LOG_OPTION(RTGC::LOG_SCANNER, function);
}

SafeShortcut* SafeShortcut::getPointer(int idx) {
	return _rtgc.g_shortcutPool.getPointer(idx);
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

static bool findSurvivalPath(GCObject* tracingNode, SimpleVector<GCObject*>& visitedNodes) {
/* 기반 객체 확인 단계(S310) */
    if (tracingNode->getRootRefCount() > ZERO_ROOT_REF) {
        return true;
    }
 
    SafeShortcut* shortcut = tracingNode->getShortcut();
 
    if (shortcut->isValid()) {
        /* 단축 경로 추적 단계(S320) */
        /* 방문 단축 경로 추가(S321) */
        shortcut->markInTracing();
        // visitedNodes.push_back(shortcut);
        /* 단축 경로 시작점 객체의 생존 경로 탐색(S322) */
        bool survived = findSurvivalPath(shortcut->getAnchor(), visitedNodes);
        shortcut->unmarkInTracing();
        if (survived) {
            return true;
        }
        /* 단축 경로 정보 소거 -> 단축(S323) */
        shortcut->moveAnchorTo(tracingNode);
        // visitedNodes.pop_back();
    }
 
    /* 방문 객체 목록 추가 단계(S330). */
    tracingNode->markGarbage();
    visitedNodes.push_back(tracingNode);
 
    /* 역방향 경로 추적 단계(S340) */
	AnchorIterator it(tracingNode);
    while (it.hasNext()) {
		GCObject* R = it.next();
        /* 중복 추적 회피 단계(S341) */
        if (R->isGarbageMarked() ||
            R->getShortcut()->inTracing()) {
            continue;
        }
 
        /* 참조자 R의 생존 경로 탐색 단계 (S342) */
        bool hasSurvivalPath = findSurvivalPath(R, visitedNodes);
        if (hasSurvivalPath) {
            /* 생존 경로 정보 저장 단계 (S350) */
            tracingNode->setSafeAnchor(R);
            /* 성공값 반환 */
            return true;
        }
    }
    /* 실패값 반환 */
    return false;
}

void constructShortcut(GCObject* obj) {
    if (!_EnableShortcut) return;
                
	int s_id = 0;
	SafeShortcut* newShortcut = nullptr;
    int cntNode = 0;
    while (obj->getRootRefCount() == ZERO_ROOT_REF) {
		SafeShortcut* ss = obj->getShortcut();
        if (!ss->isValid()) {
            if (newShortcut == nullptr) {
                newShortcut = new SafeShortcut(obj);
				s_id = SafeShortcut::getIndex(newShortcut);
            }
            cntNode ++;
            obj->setShortcutId_unsafe(s_id);
            //rtgc_log(true, "shotcut assigned %p = %d (%d)\n", obj, s_id, cntNode);
            obj = obj->getSafeAnchor();
        } else {
            if (newShortcut != nullptr) {
                newShortcut->setAnchor(obj, cntNode);
                s_id = SafeShortcut::getIndex(newShortcut);
                rtgc_log(LOG_OPT(11), "shotcut %d anchor= %p cntNode= %d\n", s_id, obj, cntNode);
                cntNode = 0;
                newShortcut = nullptr;
            }
            obj = ss->getAnchor();
        }
    }
    if (newShortcut != nullptr) {
        rtgc_log(LOG_OPT(11), "shotcut %d anchor= %p cntNode= %d\n", s_id, obj, cntNode);
    }
}

static bool clear_garbage_links(GCObject* link, GCObject* garbageAnchor, SimpleVector<GCObject*>* unsafeObjects) {
    // if (link->isGarbageMarked() || !link->isTrackable()) return false;

    // if (link->removeReferrer(garbageAnchor) == 0
    // &&  link->isUnsafe()) {
    //     if (link->isGarbage()) {
    //         link->markGarbage();
    //         link->removeAllReferrer();
    //         rtgc_log(true, "Mark new garbage.\n");
    //         return true;
    //     }
    //     rtgc_log(true, "Add unsafe objects.\n");
    //     unsafeObjects->push_back(link);
    // }
    return false;
}


bool GarbageProcessor::detectUnreachable(GCObject* unsafeObj, SimpleVector<GCObject*>& unreachableNodes) {
    int cntUnreachable = unreachableNodes.size();
    bool hasSurvivalPath = findSurvivalPath(unsafeObj, unreachableNodes);

    if (hasSurvivalPath) {
        for (int i = unreachableNodes.size(); --i >= cntUnreachable; ) {
            GCObject* obj = (GCObject*)unreachableNodes.at(i);
            obj->unmarkGarbage();
        }
        if (unreachableNodes.size() > MIN_SHORTCUT_LENGTH) {
            constructShortcut(unsafeObj);
        }
        unreachableNodes.resize(cntUnreachable);
        return false;
    }
    return true;
}

void GarbageProcessor::scanGarbages(GCObject* unsafeObj) {
    while (true) {
        rtgc_log(LOG_OPT(1), "scan Garbage %p\n", unsafeObj);

        bool hasSurvivalPath = findSurvivalPath(unsafeObj, _visitedNodes);

        if (hasSurvivalPath) {
            for (int i = _visitedNodes.size(); --i >= 0; ) {
                GCObject* obj = (GCObject*)_visitedNodes.at(i);
                obj->unmarkGarbage();
            }
            if (_visitedNodes.size() > MIN_SHORTCUT_LENGTH) {
                constructShortcut(unsafeObj);
            }
        }
        else {
            for (int i = _visitedNodes.size(); --i >= 0; ) {
                GCObject* obj = (GCObject*)_visitedNodes.at(i);
                //rtgc_log(true, "garbage deteted %p(%s)\n", obj, RTGC::getClassName(obj));
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
    precond(newTail->getShortcut() == this);
    GCObject* tail2;

    newAnchor->setShortcutId_unsafe(0);
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
        SafeShortcut* shortcut = new SafeShortcut(_tail);
        int sid = SafeShortcut::getIndex(shortcut);
        shortcut->setAnchor(newAnchor, 0);
        for (GCObject* o = _tail; o != newAnchor; o = o->getSafeAnchor()) {
            o->setShortcutId_unsafe(sid);
        }
    }
}

bool SafeShortcut::clearTooShort(GCObject* anchor, GCObject* tail) {
    int len = MIN_SHORTCUT_LENGTH;
    for (GCObject* obj = tail; obj != anchor; obj = obj->getSafeAnchor()) {
        if (--len < 0) return false;
    }
    for (GCObject* obj = tail; obj != anchor; obj = obj->getSafeAnchor()) {
        obj->setShortcutId_unsafe(INVALID_SHORTCUT);
    }
    return true;
}

void SafeShortcut::moveAnchorTo(GCObject* newAnchor) {
    precond(newAnchor->getShortcut() == this);
    for (GCObject* obj = newAnchor; obj != _anchor; obj = obj->getSafeAnchor()) {
        obj->setShortcutId_unsafe(INVALID_SHORTCUT);
    }
    if (clearTooShort(newAnchor, _tail)) {
        delete this;
    }
    else {
        this->_anchor = newAnchor;
    }
}