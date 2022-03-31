#if 0 
#include "utilities/growableArray.hpp"


template <typename T>
class SimpleArray : public GrowableArrayCHeap<T, mtGC> {
public:
    T* adr_last() {
        return this->adr_at(this->length() - 1);
    }
};


class PathFinder {
    SimpleVector<GCObject*>& _visitedNodes;
    SimpleArray<AnchorIterator> _trackers;
    GCObject* _temp;
public:
    PathFinder(SimpleVector<GCObject*>& nodes) : _visitedNodes(nodes) {}

    bool findSurvivalPath(GCObject* tracingNode);

    bool findSurvivalPathThroughShorcut(GCObject* tracingNode);

    void constructShortcut();
};

bool PathFinder::findSurvivalPathThroughShorcut(GCObject* tracingNode) {
    SafeShortcut* shortcut = tracingNode->getShortcut();
    precond(shortcut != NULL);

    if (shortcut->isValid()) {
        /* 단축 경로 추적 단계(S320) */
        /* 방문 단축 경로 추가(S321) */
        shortcut->markInTracing();
        // _visitedNodes.push_back(shortcut);
        /* 단축 경로 시작점 객체의 생존 경로 탐색(S322) */
        bool survived = findSurvivalPath(shortcut->anchor());
        shortcut->unmarkInTracing();
        if (survived) {
            return true;
        }
        /* 단축 경로 정보 소거 -> 단축(S323) */
        shortcut->shrinkAnchorTo(tracingNode);
        // _visitedNodes.pop_back();
    }
    return false;
}

bool PathFinder::findSurvivalPath(GCObject* tracingNode) {
    RefIterator<GCObject> tail(tracingNode);
    _trackers.append(*(AnchorIterator*)&tail);
    AnchorIterator* it = _trackers.adr_last();
    while (true) {
        GCObject* R = it->next();
        rtgc_log(true, "findSurvivalPath %p(%p)[%d]\n", R, it->peekPrev(), _trackers.length());
        SafeShortcut* shortcut = R->getShortcut();
        precond(shortcut != NULL);
        precond(R != NULL);

        /* 중복 추적 회피 단계(S341) */
        if (R->isGarbageMarked() ||
            shortcut->inTracing()) {
            continue;
        }

        if (R->getRootRefCount() > ZERO_ROOT_REF) {
            rtgc_log(true, "SurvivalPath found [%d]\n", _trackers.length());
            return true;
        }

        if (R->hasReferrer()) {
            if (shortcut->isValid()) {
                shortcut->markInTracing();
                RefIterator<GCObject> jump(shortcut->anchor());
                _trackers.append(*(AnchorIterator*)&jump);
            }
            else {
                AnchorIterator ait(R);
                _trackers.append(ait);
                for (int i = 0; i < _trackers.length(); i ++) {
                    AnchorIterator* it = _trackers.adr_at(i);
                    rtgc_log(true, "-* link(%p:%p)[%d]\n", it->getLocation(), it->peekPrev(), i);
                }
                R->markGarbage();
                _visitedNodes.push_back(R);
            }
            it = _trackers.adr_last();
            continue;
        }

                    rtgc_log(true, "------------\n");
        R->markGarbage();
        _visitedNodes.push_back(R);

        while (!it->hasNext()) {
            _trackers.pop();
            rtgc_log(true, "SurvivalPath pop [%d]\n", _trackers.length());
            if (_trackers.is_empty()) return false;
            it = _trackers.adr_last();

            R = it->peekPrev();
            if (!R->isGarbageMarked()) {
                SafeShortcut* shortcut = R->getShortcut();
                precond(shortcut->inTracing());
                shortcut->unmarkInTracing();
                shortcut->shrinkAnchorTo(tracingNode);
                _trackers.append(R);
                R->markGarbage();
                _visitedNodes.push_back(R);
                it = _trackers.adr_last();
                break;
            }
        }
    }
}

void PathFinder::constructShortcut() {
    for (int i = _visitedNodes.size(); --i >= 0; ) {
        GCObject* obj = (GCObject*)_visitedNodes.at(i);
        obj->unmarkGarbage();
    }

    const int MAX_SHORTCUT_LEN = 256;
    for (int i = 0; i < _trackers.length(); i ++) {
        AnchorIterator* it = _trackers.adr_at(i);
        rtgc_log(true, "- link(%p)[%d]\n", it->peekPrev(), i);
    }
    AnchorIterator* ait = _trackers.adr_at(0);
    AnchorIterator* end = ait + _trackers.length();
    GCObject* tail = NULL;
    GCObject* link = NULL;
    SafeShortcut* lastShortcut = NULL;
    int cntNode = 0;
    for (; ait < end; ait++) {
        GCObject* obj = ait->peekPrev();        
        rtgc_log(true, "pop link(%p)\n", obj);
        if (link != NULL) {
            rtgc_log(true, "link(%p) to anchor(%p)\n", link, obj);
            link->setSafeAnchor(obj);
        }

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
            if (obj == ss->tail()) {
                ss->extendTail(tail);
            }
            else if (lastShortcut != NULL) {
                lastShortcut->extendAnchor(obj);
            }
            else {
                SafeShortcut::create(obj, tail, cntNode);
            }
            link = ss->anchor();
            lastShortcut = ss;
            cntNode = 0;
        }
    }
    
    if (cntNode > 0) {
        if (lastShortcut != NULL) {
            lastShortcut->extendAnchor(link);
        } else {
            SafeShortcut::create(link, tail, cntNode);
        }
    }
}


#endif