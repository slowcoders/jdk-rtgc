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

class GCObject;
class SafeShortcut;
class AnchorIterator;

typedef SimpleVector<GCObject*> ScanVector;

class ReferrerList : public SimpleVector<ShortOOP> {
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

	void setShortcutId_unsafe(int shortcutId) {
		this->_shortcutId = shortcutId;
	}

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

static const int MIN_SHORTCUT_LENGTH = 3;
static const bool _EnableShortcut = false;


class SafeShortcut {
	union {
		GCObject* _tail;
		intptr_t  _mark;
	};
	GCObject* _anchor;
	int _cntNode;

	SafeShortcut(GCObject* anchor, GCObject* tail) {
		_tail = tail;
		_anchor = anchor;
	}
public:

	static SafeShortcut* create(GCObject* anchor, GCObject* tail, int cntNode) {
		int s_id = INVALID_SHORTCUT;
		SafeShortcut* shortcut = NULL;
		if (_EnableShortcut && cntNode > MIN_SHORTCUT_LENGTH) {
			shortcut = new SafeShortcut(anchor, tail);
			s_id = getIndex(shortcut);
		}
		for (GCObject* node = tail; node != anchor; node = node->getSafeAnchor()) {
			node->setShortcutId_unsafe(s_id);
		}
		if (shortcut != NULL) {
	        // rtgc_log(true, "shotcut[%d:%d] assigned %p->%p\n", s_id, cntNode, anchor, tail);
			shortcut->vailidateShortcut();
		}
		return shortcut;
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

	void vailidateShortcut() {
		precond(_anchor->getShortcut() != this);
		for (GCObject* obj = _tail; obj != _anchor; obj = obj->getSafeAnchor()) {
			precond(obj->getShortcut() == this);
		}
	}

	void extendTail(GCObject* tail) { 
		int s_id = getIndex(this);
	    rtgc_log(true, "extendTail shotcut[%d] %p->%p\n", s_id, _tail, tail);
		for (GCObject* node = tail; node != _tail; node = node->getSafeAnchor()) {
			node->setShortcutId_unsafe(s_id);
		}
		this->_tail = tail;
		vailidateShortcut();
	}

	void extendAnchor(GCObject* anchor) { 
		int s_id = getIndex(this);
	    rtgc_log(true, "extendAnchor shotcut[%d] %p->%p\n", s_id, _anchor, anchor);
		for (GCObject* node = _anchor; node != anchor; node = node->getSafeAnchor()) {
			node->setShortcutId_unsafe(s_id);
		}
		this->_anchor = anchor;
		vailidateShortcut();
	}

	void shrinkAnchorTo(GCObject* newAnchor);

	bool isValid() { return this->_anchor != nullptr; }
};



class AnchorIterator : public RefIterator<ShortOOP> {
public:
	AnchorIterator() {}

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