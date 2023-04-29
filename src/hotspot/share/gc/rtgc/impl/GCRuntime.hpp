#ifndef SHARE_GC_RTGC_IMPL_GCRUNTIME_HPP
#define SHARE_GC_RTGC_IMPL_GCRUNTIME_HPP

#include "GCObject.hpp" 
#include "gc/rtgc/rtgcDebug.hpp"

namespace RTGC {


class RuntimeHeap {
public:	
	static void reclaimObject(GCObject* obj);
	static void reclaimSpace();
	static bool is_broken_link(GCObject* anchor, GCObject* link);
	static void scanInstanceGraph(GCObject* obj, RefTracer2 tracer, HugeArray<GCObject*>* stack, bool isTenured);
};

enum AnchorState {
	NotAnchored,
	Unknown,
	AnchoredToRoot,
};

class GarbageProcessor {
public:
	void initialize();
	void addUnstable(GCObject* node);
	void addUnstable_ex(GCObject* node);
	void destroyObject(GCObject* garbage, bool isTenured);
	void collectAndDestroyGarbage(bool isTenured);
	void scanGarbage(GCObject** ppNode, int cntNode);

	bool detectGarbage(GCObject* node);
	// bool resolveStrongSurvivalPath(GCObject* node);
	void validateGarbageList();
	// bool hasStableSurvivalPath(GCObject* node);
	// AnchorState checkAnchorStateFast(GCObject* node, bool enableCircuit);
	HugeArray<GCObject*>* getGarbageNodes() { return &_visitedNodes; }
	bool hasUnsafeObjects();
private:
    HugeArray<GCObject*> _unsafeObjects;
    HugeArray<GCObject*> _visitedNodes;
    // HugeArray<AnchorIterator> _trackers;
	GCObject* delete_q;
    CircuitNode* reachableShortcurQ;

	// template<bool scanStrongPathOnly>
    // bool findSurvivalPath(ShortOOP& tail);
    // bool scanSurvivalPath(GCObject* tail, bool scanStrongPathOnly);
    // void constructCircuit();
    // void clearReachableCircuitMarks();
	void destroyDetectedGarbage(bool isTenured);

	static bool clear_garbage_links(GCObject* link, GCObject* garbageAnchor);
};

typedef MemoryPool<CircuitNode, 64*1024*1024, 0, -1> CircuitPool;
// typedef MemoryPool<ReferrerList, 64*1024*1024, 1, 0> ReferrerListPool;
// typedef MemoryPool<TinyChunk, 64*1024*1024, 1, -1> TinyMemPool;

class GCRuntime {
public:
	CircuitPool g_circuitPool;
	GarbageProcessor* g_pGarbageProcessor;
	char _gp[sizeof(GarbageProcessor)];

	void initialize() {
		g_circuitPool.initialize();
		CircuitNode::initialize();
		g_pGarbageProcessor = new (_gp)GarbageProcessor();
		g_pGarbageProcessor->initialize();
	}

	static void NO_INLINE onReplaceRootVariable(GCObject* assigned, GCObject* erased);

	static void NO_INLINE onAssignRootVariable(GCObject* assigned);

	static void NO_INLINE onEraseRootVariable(GCObject* erased);

	static void NO_INLINE replaceMemberVariable(GCObject* owner, OffsetPointer<GCObject>* pField, GCObject* v);

	static void NO_INLINE replaceStaticVariable(GCObject** pField, GCObject* assigned);

	static void NO_INLINE dumpDebugInfos();

	static bool markPublished(GCObject* obj) {
		fatal("not implemented");
		if (!obj->isPublished()) {
			// obj->_flags.isPublished = true;
			return true;
		}
		return false;
	}
	
	static void adjustCircuitPoints();

	static void connectReferenceLink(GCObject* assigned, GCObject* owner);

	static void disconnectReferenceLink(GCObject* erased, GCObject* owner);

	static bool tryDisconnectReferenceLink(GCObject* erased, GCObject* owner);

	static void onAssignRootVariable_internal(GCObject* assigned);

	static void onEraseRootVariable_internal(GCObject* assigned);

	static bool detectUnsafeObject(GCObject* erased);

private:
	static void reclaimGarbage(GCObject* garbage, GCObject* garbageNode);
};	

extern GCRuntime _rtgc;
}


#endif // SHARE_GC_RTGC_IMPL_GCRUNTIME_HPP