#include "GCObject.hpp" 
#include "GCRuntime.hpp" 

#define USE_ITERATOR_STACK false
#define USE_ANCHOR_VISIOR true
static const int GC_VERBOSE_LOG = false;


using namespace RTGC;


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

static bool findSurvivalPath(GCObject* tracingNode, SimpleVector<void*>& visitedNodes) {
/* 기반 객체 확인 단계(S310) */
    if (tracingNode->getRootRefCount() > ZERO_ROOT_REF) {
        return true;
    }
 
    SafeShortcut* shortcut = tracingNode->getShortcut();
 
    if (shortcut->isValid()) {
        /* 단축 경로 추적 단계(S320) */
        /* 방문 단축 경로 추가(S321) */
        visitedNodes.push_back(shortcut);
        /* 단축 경로 시작점 객체의 생존 경로 탐색(S322) */
        if (findSurvivalPath(shortcut->getAnchor(), visitedNodes)) {
            return true;
        }
        /* 단축 경로 정보 소거(S323) */
        shortcut->invalidate();
        visitedNodes.pop_back();
    }
 
    /* 방문 객체 목록 추가 단계(S330). */
    visitedNodes.push_back(tracingNode);
 
    /* 역방향 경로 추적 단계(S340) */
	AnchorIterator it(tracingNode);
    while (it.hasNext()) {
		GCObject* R = it.next();
        /* 중복 추적 회피 단계(S341) */
        if (visitedNodes.contains(R) ||
            visitedNodes.contains(R->getShortcut())) {
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
	int s_id = 0;
	SafeShortcut* newShortcut = nullptr;
    while (obj->getRootRefCount() == ZERO_ROOT_REF) {
		SafeShortcut* ss = obj->getShortcut();
        if (!ss->isValid()) {
            if (newShortcut == nullptr) {
                SafeShortcut* newShortcut = new SafeShortcut(obj);
				s_id = SafeShortcut::getIndex(newShortcut);
            }
            obj->setShortcutId_unsafe(s_id);
            obj = obj->getSafeAnchor();
        } else {
            if (newShortcut != nullptr) {
                newShortcut->setAnchor(obj, 0);
                newShortcut = nullptr;
            }
            obj = ss->getAnchor();
        }
    }
}


void GarbageProcessor::scanGarbages(GCObject* unsafeObj) {
    while (true) {
        bool hasSurvivalPath = findSurvivalPath(unsafeObj, visitedNodes);

        if (hasSurvivalPath) {
            constructShortcut(unsafeObj);
        }
        else {
            for (int i = visitedNodes.size(); --i >= 0; ) {
                GCObject* obj = (GCObject*)visitedNodes.at(i);
                //if (GC_VERBOSE_LOG) printf("markGarbage %p %s\n", obj, obj->getDebugId());
                obj->markGarbage();
            }
            for (int i = visitedNodes.size(); --i >= 0; ) {
                GCObject* obj = (GCObject*)visitedNodes.at(i);
                destroyObject(obj);
            }
        }
        visitedNodes.resize(0);
        while (true) {
            if (_unsafeObjects.empty()) {
                reclaimObjects();
                return;
            }
            unsafeObj = _unsafeObjects.back();
            _unsafeObjects.pop_back();
            if (unsafeObj->getNodeType() == NodeType::Reachable) break;
        }
    }

}

#define DESTROY_OBJECT(garbage) \
    garbage->markDestroyed(); \
    ((void**)garbage)[2] = delete_q; \
    delete_q = garbage;
    

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
            if (link->getNodeType() == NodeType::Reachable) {
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
