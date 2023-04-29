#ifndef __GCOBJECT_HPP
#define __GCOBJECT_HPP

#include "GCUtils.hpp"
#include "GCPointer.hpp"
#include "GCUtils2.hpp"
#include "runtime/atomic.hpp"
#include "oops/oop.hpp"
#include "GCNode.hpp"

#if GC_DEBUG
#define debug_printf		printf
#else
#define debug_printf(...)	// do nothing
#endif

namespace RTGC {

class GCObject;
class CircuitNode;

class GCObject : public GCNode {
	friend class GCRuntime;
	friend class GarbageProcessor;

public:

	GCObject() {
		// *((int64_t*)this) = 0;
		// _nodeType = (int)type;
	} 

	bool isUnsafeTrackable() {
		return isTrackable() && getRootRefCount() <= 1;
	}

	void invaliateSurvivalPath(GCObject* newTail) {}

	template <bool isTrackable, bool dirtyAnchor>
	void addAnchor(GCObject* referrer);

	void addTrackableAnchor(GCObject* referrer) {
		GCNode::incrementRootRefCount();
	}

	bool addDirtyAnchor(GCObject* referrer) {
		fatal("deprecated");
		return false;
	}

	void addTemporalAnchor(GCObject* referrer) {
		// inore;
	}

	// return true if safe_anchor removed;
	void removeReferrer(GCObject* referrer);

	// return true if safe_anchor removed;
	void removeReferrerWithoutReallocaton(GCObject* referrer);

	// return true if safe_anchor removed;
	bool tryRemoveReferrer(GCObject* referrer) {
		fatal("deprecated");
		return false;
	}

	// return true if safe_anchor removed;
	bool removeMatchedReferrers(GCObject* referrer) {
		fatal("deprecated");
		return false;
	}

	void removeAllAnchors() {
		// rt_assert(getRootRefCount() == 0);
	}

	void clearAnchorList() {
		// rt_assert(getRootRefCount() == 0);
	}

	void removeDirtyAnchors() {
		// ignore		
	}

	bool clearEmptyAnchorList() {
		// ignore	
		fatal("deprecated");	
		return false;
	}

	void invalidateAnchorList_unsafe() {
		rt_assert(this->getRootRefCount() == 0);	
	}



private:
	// return true if safe_anchor removed;
	template <bool reallocReferrerList, bool must_exist, bool remove_mutiple_items> 
	int  removeReferrer_impl(GCObject* referrer);
};

#if 1
static const bool _EnableCircuit = true;

class CircuitNode {
	int extRefCount;

	CircuitNode() { extRefCount = 0; }
public:

	static void initialize();

	static CircuitNode* create() {
		CircuitNode* circuit = new CircuitNode();
		return circuit;
	}

	bool isAlive() { return extRefCount > 0; }

	int getIndex() { return getIndex(this); }

	static bool isValidIndex(int idx);

	static int getIndex(CircuitNode* circuit);

	static CircuitNode* getPointer(int idx);

	void* operator new (std::size_t size);

	void operator delete(void* ptr);

};
#endif

inline CircuitNode* GCNode::getCircuit() const {
	int p_id = this->getCircuitId();
    return CircuitNode::getPointer(p_id);
}


}

#endif // __GCOBJECT_HPP