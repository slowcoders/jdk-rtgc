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

static const bool _EnableShortcut = false;

class GCObject;
class SafeShortcut;
class AnchorIterator;

typedef SimpleVector<GCObject*> ScanVector;

class ReferrerList : public SimpleVector<GCObject*> {
public:    
	void init(int initialSize);
};

static const int INVALID_SHORTCUT = 1;

class GCObject : public GCNode {
	friend class GCRuntime;
	friend class GarbageProcessor;

public:

	GCObject() {
		// *((int64_t*)this) = 0;
		// _nodeType = (int)type;
	} 

	// virtual ~GCObject();

	void initIterator(AnchorIterator* iterator);

	ReferrerList* getReferrerList();

	SafeShortcut* getShortcut();

	GCObject* getSafeAnchor();

	void setSafeAnchor(GCObject* anchor);

	void setShortcutId_unsafe(int shortcutId);

	int getShortcutId() {
		return this->_shortcutId;
	}

	GCObject* getSingleAnchor();

	void invaliateSurvivalPath(GCObject* newTail);

	GCObject* getLinkInside(SafeShortcut* container);

	bool visitLinks(LinkVisitor visitor, void* callbackParam);

	void addReferrer(GCObject* referrer);

	int removeReferrer(GCObject* referrer);

	void removeAllReferrer();

	bool removeMatchedReferrers(GCObject* referrer);
};


class SafeShortcut {
	union {
		GCObject* _tail;
		intptr_t  _mark;
	};
	GCObject* _anchor;
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

	void markInTracing() {
		_mark |= 1;
	}
	void unmarkInTracing() {
		_mark &= ~1;
	}
	bool inTracing() {
		return (_mark & 1);
	}

	GCObject* getAnchor() { return _anchor; }

	GCObject* getTail() { return _tail; }

	void split(GCObject* newTail, GCObject* newAnchor);

	bool clearTooShort(GCObject* anchor, GCObject* tail);

	void adjustPointUnsafe(GCObject* anchor, GCObject* tail) {
		_anchor = anchor; _tail = tail;
	}

	void setAnchor(GCObject* anchor, int cntNode) { this->_anchor = anchor; this->_cntNode = cntNode; }

	void moveAnchorTo(GCObject* newAnchor);

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