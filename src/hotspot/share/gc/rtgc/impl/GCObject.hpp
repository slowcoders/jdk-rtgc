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

class ReferrerList : public SimpleVector<ShortOOP> {
public:    
	void init(int initialSize);
};

class GCObject : public GCNode {
	friend class GCRuntime;
	friend class GarbageProcessor;

public:

	GCObject() {
		// *((int64_t*)this) = 0;
		// _nodeType = (int)type;
	} 

	void initIterator(AnchorIterator* iterator);

	ReferrerList* getReferrerList();

	int getReferrerCount();

	SafeShortcut* getShortcut();

	GCObject* getSafeAnchor();

	void setSafeAnchor(GCObject* anchor);

	int getShortcutId() {
		return this->_shortcutId;
	}

	void setShortcutId_unsafe(int shortcutId) {
		this->_shortcutId = shortcutId;
	}

	void invalidateSafeAnchor() {
		// no-shortcut. no safe-anchor.
		setShortcutId_unsafe(NO_SAFE_ANCHOR);
	}

	bool hasReferrer(GCObject* referrer);

	bool hasSafeAnchor() {
		return getShortcutId() > NO_SAFE_ANCHOR;
	}

	bool isUnsafeTrackable() {
		return isTrackable() && getRootRefCount() <= 1 && !hasSafeAnchor();
	}

	void invalidateShortcutId() {
		precond(hasSafeAnchor());
		// no-shortcut. but this has valid safe-anchor.
		setShortcutId_unsafe(INVALID_SHORTCUT);
	}

	bool hasShortcut() {
		return getShortcutId() > INVALID_SHORTCUT;
	}

	GCObject* getSingleAnchor();

	void invaliateSurvivalPath(GCObject* newTail);

	GCObject* getLinkInside(SafeShortcut* container);

	bool visitLinks(LinkVisitor visitor, void* callbackParam);

	bool containsReferrer(GCObject* node);

	void addReferrer(GCObject* referrer);

	int removeReferrer(GCObject* referrer);

	int removeReferrerWithoutReallocaton(GCObject* referrer);

	int tryRemoveReferrer(GCObject* referrer);

	void clearAnchorList();

	bool clearEmptyAnchorList();

	void removeAllAnchors();

	bool removeMatchedReferrers(GCObject* referrer);

	void removeBrokenAnchors();

private:
	template <bool reallocReferrerList> 
	int removeReferrer_impl(GCObject* referrer);
};

static const int MIN_SHORTCUT_LENGTH = 3;
static const bool _EnableShortcut = true;
#define ENABLE_REACHBLE_SHORTCUT_CACHE false

class SafeShortcut {
	GCObject* _inTracing;
	ShortOOP _anchor;
	ShortOOP _tail;

	SafeShortcut(GCObject* anchor, GCObject* tail) :  
			_inTracing(NULL), _anchor(anchor), _tail(tail) {}
public:
	~SafeShortcut() { *(int32_t*)&_anchor = 0; }

	static void initialize();

	static SafeShortcut* create(GCObject* anchor, GCObject* tail, int cntNode, bool replace_shorcut = false) {
		int s_id = INVALID_SHORTCUT;
		SafeShortcut* shortcut = NULL;
		if (_EnableShortcut && cntNode > MIN_SHORTCUT_LENGTH) {
			shortcut = new SafeShortcut(anchor, tail);
			s_id = getIndex(shortcut);
		}
		int cc = 0;
		for (GCObject* node = tail; node != anchor; node = node->getSafeAnchor()) {
			debug_only(precond(++cc < 10000));
			precond(replace_shorcut || node->getShortcutId() <= INVALID_SHORTCUT);
			node->setShortcutId_unsafe(s_id);
		}
		if (shortcut != NULL) {
	        rtgc_log(RTGC::LOG_OPTION(LOG_SCANNER, 10), "shotcut[%d:%d] created %p->%p\n", 
				s_id, cntNode, anchor, tail);
			shortcut->vailidateShortcut();
		}
		return shortcut;
	}

	bool isValid() { return *(int32_t*)&_anchor != 0; }

	int getIndex() { return getIndex(this); }

	static int getIndex(SafeShortcut* circuit);

	static SafeShortcut* getPointer(int idx);

	void* operator new (std::size_t size);

	void operator delete(void* ptr);

#if ENABLE_REACHBLE_SHORTCUT_CACHE
	bool isReachable() {
		return _mark & 2;
	}

	SafeShortcut* nextReachable() {
		return getPointer(_next);
	}

	void markReachable(SafeShortcut* next) {
		_mark |= 2;
		_next = getIndex(next);
	}

	void unmarkReachable() {
		_mark &= ~2;
	}
#endif

	void markInTracing(GCObject* obj) {
		assert(!inTracing(), "aleady in tracing %p", obj);
		_inTracing = obj;
	}
	void unmarkInTracing() {
		_inTracing = NULL;
	}
	bool inTracing() {
		return _inTracing != NULL;
	}
	bool inContiguousTracing(GCObject* obj, SafeShortcut** ppShortcut);

	const ShortOOP& anchor() { return _anchor; }

	const ShortOOP& tail() { return _tail; }

	ShortOOP& anchor_ref() { return _anchor; }

	void split(GCObject* newTail, GCObject* newAnchor);

	static bool clearTooShort(GCObject* anchor, GCObject* tail);

	void adjustPointUnsafe(GCObject* anchor, GCObject* tail) {
		_anchor = anchor; _tail = tail;
	}

	void vailidateShortcut();

	void extendTail(GCObject* tail);

	void extendAnchor(GCObject* anchor);

	void shrinkAnchorTo(GCObject* newAnchor);

	void shrinkTailTo(GCObject* newTail);

	static bool isValidIndex(int idx);
};



class AnchorIterator : public NodeIterator<ShortOOP> {
public:
	AnchorIterator() {}

	AnchorIterator(GCObject* const node) {
		node->initIterator(this);
	}
};


}

#endif // __GCOBJECT_HPP