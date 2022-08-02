#include "GCObject.hpp" 
#include "gc/rtgc/rtgcDebug.hpp"

namespace RTGC {


typedef SimpleVector<GCObject*> NodeList;



class RuntimeHeap {
public:	
	static void reclaimObject(GCObject* obj);
};

class GarbageProcessor {
	SimpleVector<LinkIterator> _traceStack;
	GCObject* delete_q;

public:
	GarbageProcessor() : _traceStack(255) {
		delete_q = nullptr;
	}

	void addUnstable(GCObject* obj);
	void destroyObject(GCObject* garbage);
	void collectGarbage();
	void collectGarbage(GCObject** ppNode, int cntNode);

	static bool detectGarbage(GCObject* unsafeObj);

private:
    HugeArray<GCObject*> _unsafeObjects;
    HugeArray<GCObject*> _visitedNodes;
    HugeArray<AnchorIterator> _trackers;
    SafeShortcut* reachableShortcurQ;

    bool findSurvivalPath(ShortOOP& tail);
	void addGarbage(GCObject* garbage);
    bool scanSurvivalPath(GCObject* tail);
    void constructShortcut();
    void clearReachableShortcutMarks();

	static bool clear_garbage_links(GCObject* link, GCObject* garbageAnchor);
};

typedef MemoryPool<SafeShortcut, 64*1024*1024, 0, -1> ShortcutPool;
typedef MemoryPool<ReferrerList, 64*1024*1024, 1, 0> ReferrerListPool;
typedef MemoryPool<TinyChunk, 64*1024*1024, 1, -1> TinyMemPool;

class GCRuntime {
public:
	ShortcutPool g_shortcutPool;
    TinyMemPool gTinyPool;
    ReferrerListPool gRefListPool;
	GarbageProcessor* g_pGarbageProcessor;
	char _gp[sizeof(GarbageProcessor)];

	void initialize() {
		g_shortcutPool.initialize();
		gTinyPool.initialize();
		gRefListPool.initialize();
		SafeShortcut::initialize();
		g_pGarbageProcessor = new (_gp)GarbageProcessor();
	}

	static void NO_INLINE onReplaceRootVariable(GCObject* assigned, GCObject* erased);

	static void NO_INLINE onAssignRootVariable(GCObject* assigned);

	static void NO_INLINE onEraseRootVariable(GCObject* erased);

	static void NO_INLINE replaceMemberVariable(GCObject* owner, OffsetPointer<GCObject>* pField, GCObject* v);

	static void NO_INLINE replaceStaticVariable(GCObject** pField, GCObject* assigned);

	static void NO_INLINE dumpDebugInfos();

	static bool markPublished(GCObject* obj) {
		if (!obj->isPublished()) {
			obj->_flags.isPublished = true;
			return true;
		}
		return false;
	}
	
	static void adjustShortcutPoints();

	static void connectReferenceLink(GCObject* assigned, GCObject* owner);

	static void disconnectReferenceLink(GCObject* erased, GCObject* owner);

	static void onAssignRootVariable_internal(GCObject* assigned);

	static void onEraseRootVariable_internal(GCObject* assigned);

private:
	#if GC_DEBUG
	static int getCircuitCount();
	static int getTinyChunkCount();
	static int getReferrerListCount();
	#endif

	static void reclaimGarbage(GCObject* garbage, GCObject* garbageNode);

	static void detectUnsafeObject(GCObject* erased);

};	

extern GCRuntime _rtgc;
}


