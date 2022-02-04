#ifndef __GCOBJECT_HPP
#define __GCOBJECT_HPP

#include "GCUtils.hpp"
#include "GCPointer.hpp"
#include "runtime/atomic.hpp"
#include "oops/oop.hpp"
#include "GCNode.hpp"

#if GC_DEBUG
#define debug_printf		printf
#else
#define debug_printf(...)	// do nothing
#endif

namespace RTGC {

enum class TraceState : int {
	NOT_TRACED,
	IN_TRACING,
	TRACE_FINISHED,
};

enum class NodeType : int {
	Reachable,
	Garbage,
	Destroyed,
};


class GCObject;
class SafeShortcut;
class AnchorIterator;

typedef SimpleVector<GCObject*> ScanVector;

class ReferrerList : public SimpleVector<GCObject*> {
public:    
	void init(int initialSize);
};

static const int INVALID_SHORTCUT = 1;
static const int ZERO_ROOT_REF = -1;

class GCObject : public GCNode {
	friend class GCRuntime;
	friend class GarbageProcessor;

public:

	GCObject() {
		// *((int64_t*)this) = 0;
		// _nodeType = (int)type;
	} 

	// virtual ~GCObject();

	NodeType getNodeType() {
		return (NodeType)_nodeType;
	}

	bool isGarbage() {
		return this->_rootRefCount <= ZERO_ROOT_REF && !this->hasReferrer();
	}

	bool isUnsafe() {
		return this->_rootRefCount <= ZERO_ROOT_REF && this->_shortcutId == 0;
	}

	bool isPublished() {
		return this->_isPublished;
	}

	TraceState getTraceState() {
		return (TraceState)_traceState;
	}

	void setTraceState(TraceState state) {
		_traceState = (int)state;
	}

	void markGarbage() {
		_nodeType = (int)NodeType::Garbage;
	}


	bool hasReferrer() {
		return this->_refs != 0;
	}

	int getRootRefCount() {
		return this->_rootRefCount;
	}

	int incrementRootRefCount() {
		return ++this->_rootRefCount;
	}

	int decrementRootRefCount() {
		return --this->_rootRefCount;
	}

	void initIterator(AnchorIterator* iterator);

	ReferrerList* getReferrerList();

	void markDestroyed() {
		this->_nodeType = (int)NodeType::Destroyed;
	}

	bool isDestroyed() {
		return this->_nodeType == (int)NodeType::Destroyed;;
	}

	SafeShortcut* getShortcut();

	GCObject* getSafeAnchor();

	void setSafeAnchor(GCObject* anchor);

	void setShortcutId_unsafe(int shortcutId);

	int getShortcutId() {
		return this->_shortcutId;
	}

	void invaliateSurvivalPath();

	GCObject* getLinkInside(SafeShortcut* container);

	bool visitLinks(LinkVisitor visitor, void* callbackParam);

	void addReferrer(GCObject* referrer);

	int removeReferrer(GCObject* referrer);

	bool removeAllReferrer(GCObject* referrer);
};


class SafeShortcut {
	GCObject* _anchor;
	GCObject* _tail;
	int _cntNode;
public:

	SafeShortcut(GCObject* tail) {
		_tail = tail;
	}

	void clear() {
		_tail = _anchor = nullptr;
	}

	static int getIndex(SafeShortcut* circuit);

	static SafeShortcut* getPointer(int idx);

	void* operator new (std::size_t size);

	void operator delete(void* ptr);

	GCObject* getAnchor() { return _anchor; }

	void setAnchor(GCObject* anchor, int cntNode) { this->_anchor = anchor; this->_cntNode = cntNode; }

	void invalidate() { this->_anchor = nullptr; }

	bool isValid() { return this->_anchor != nullptr; }
};



class AnchorIterator : public RefIterator<GCObject> {
public:
	AnchorIterator(GCObject* const node) {
		node->initIterator(this);
	}
};

class LinkIterator {
	union {
		GCObject* _anchor;
		GCObjectArray<GCObject>* _array;
	};
	union {
		const uint16_t* _offsets;
		intptr_t _idxField;
	};

public:
	LinkIterator(GCObject* obj);

	GCObject* next();

	GCObject* getContainerObject() { 
		return _anchor;
	}
};


}

#endif // __GCOBJECT_HPP