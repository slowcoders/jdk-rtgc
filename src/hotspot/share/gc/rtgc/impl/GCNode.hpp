#ifndef SHARE_GC_RTGC_IMPL_RTGCNODE_HPP
#define SHARE_GC_RTGC_IMPL_RTGCNODE_HPP

#include "../RTGC.hpp"
#include "../rtgcDebug.hpp"
#include "oops/oop.hpp"
#include "GCPointer.hpp"

#define ZERO_ROOT_REF 		0
static const int NO_SAFE_ANCHOR = 0;
static const int INVALID_SHORTCUT = 1;

static const bool USE_EXPLICIT_TRACKABLE_MARK = true;
namespace RTGC {

class ReferrerList;
class SafeShortcut;
static const int 	TRACKABLE_BIT = 1;

class NodeInfo {
protected:	
	struct {
		uint32_t _jvmFlags: 7;
		uint32_t _hasMultiRef: 1;
#ifdef ASSERT
		uint32_t _shortcutId: 23;
		uint32_t _isModified: 1;
#else
		uint32_t _shortcutId: 24;
#endif
	}; 

	uint32_t _refs;

public:
	NodeInfo(GCNode* obj);

	void ensureLocked() {
		precond(_refs != (uint32_t)-1);
	}

	bool isAnchored() {
		ensureLocked();
		return _refs != 0;
	}

	bool hasMultiRef() {
		ensureLocked();
		return _hasMultiRef != 0;
	}

	ShortOOP& getSingleAnchor() {
		ensureLocked();
		precond(!_hasMultiRef && _refs != 0);
		return *(ShortOOP*)&_refs;
	}

	ReferrerList* getAnchorList();

	bool hasShortcut() {
		ensureLocked();
		return getShortcutId() > INVALID_SHORTCUT;
	}

	SafeShortcut* getShortcut();

	int getShortcutId() {
		ensureLocked();
		return this->_shortcutId;
	}

	GCObject* getSafeAnchor();

	bool hasSafeAnchor() {
		ensureLocked();
		return getShortcutId() > NO_SAFE_ANCHOR;
	}
};

class LockedNodeInfo : public NodeInfo {
	GCNode* _obj;
public:
	LockedNodeInfo(GCNode* obj);
	~LockedNodeInfo();

	void updateNow();
	void release();

	void setHasMultiRef(bool multiRef) {
		_hasMultiRef = multiRef;
		markModified();
	}

	void setSingleAnchor(ShortOOP anchor) {
		precond(!_hasMultiRef);
		_refs = anchor.getOffset();
		markModified();
	}

	void removeSingleAnchor() {
		precond(!_hasMultiRef);
		_refs = 0;
		markModified();
	}

	void setAnchorList(ReferrerList* anchors);


	void setShortcutId_unsafe(int shortcutId) {
		this->_shortcutId = shortcutId;
		markModified();
	}

	void setSafeAnchor(GCObject* anchor);

	void invalidateSafeAnchor() {
		// no-shortcut. no safe-anchor.
		setShortcutId_unsafe(NO_SAFE_ANCHOR);
		markModified();
	}

	void invalidateShortcutId() {
		precond(hasSafeAnchor());
		// no-shortcut. but this has valid safe-anchor.
		setShortcutId_unsafe(INVALID_SHORTCUT);
		markModified();
	}

	void markModified() {
		ensureLocked();
		debug_only(_isModified = true;)
	}
};

struct GCFlags {
	uint32_t isTrackableOrDestroyed: 1;
	uint32_t isYoungRoot: 1;
	uint32_t isGarbage: 1;
	uint32_t dirtyReferrerPoints: 1;
	uint32_t isUnstable: 1;

	uint32_t contextFlag: 1;
	uint32_t isPublished: 1;
	uint32_t isNodeLocked: 1;
#if ZERO_ROOT_REF < 0	
	int32_t rootRefCount: 24;
#else
	uint32_t rootRefCount: 24;
#endif
};

static const bool FAT_OOP = true;

class GCNode : public oopDesc {
friend class LockedNodeInfo;
	GCFlags& flags() {
		return *(GCFlags*)((uintptr_t)this + flags_offset());
	}

public:
	static int _cntTrackable;
	static void* g_trackable_heap_start;

	static int flags_offset() {
		if (FAT_OOP) {
			return sizeof(int64_t) * 2;
		}
		return oopDesc::klass_gap_offset_in_bytes();
	}

	void clearFlags() { 
		if (FAT_OOP) {
			((int64_t*)this)[1] = 0;
		}
		*(uint64_t*)&flags() = 0;
	}

	NodeInfo getNodeInfo() {
		return NodeInfo(this);
	}

	void markDirtyReferrerPoints() {
		flags().dirtyReferrerPoints = true;
	}	

	void unmarkDirtyReferrerPoints() {
		flags().dirtyReferrerPoints = false;
	}	

	bool isDirtyReferrerPoints() {
		return flags().dirtyReferrerPoints;
	}	

	bool isYoungRoot() {
		return flags().isYoungRoot;
	}

	void markYoungRoot() {
		precond(!this->isYoungRoot());
		flags().isYoungRoot = true;
	}

	void unmarkYoungRoot() {
		precond(this->isYoungRoot());
		flags().isYoungRoot = false;
	}

	bool isTrackable() {
		if (USE_EXPLICIT_TRACKABLE_MARK) {
			return flags().isTrackableOrDestroyed;
		} else {
			return (void*)this >= g_trackable_heap_start;
		}
	}

	void markTrackable() {
		if (USE_EXPLICIT_TRACKABLE_MARK) {
			precond(!this->isTrackable());
			flags().isTrackableOrDestroyed = true;
		} else {
			fatal("should not reach here.");
		}
	}

	void markDestroyed() {
		precond(flags().isGarbage);
#ifdef ASSERT		
		_cntTrackable--;
#endif		
		if (USE_EXPLICIT_TRACKABLE_MARK) {
			flags().isTrackableOrDestroyed = false;
		} else {
			flags().isTrackableOrDestroyed = true;
		}
	}

	bool isDestroyed() {
		return flags().isGarbage && 
			flags().isTrackableOrDestroyed != USE_EXPLICIT_TRACKABLE_MARK;
	}

	bool isActiveFinalizer() {
		return flags().rootRefCount & (1 << 23);
	}

	int unmarkSurvivorReachable() {
		precond(isSurvivorReachable());
		flags().rootRefCount &= ~(1 << 22);
		rtgc_debug_log(this, "unmarkSurvivorReachable %p rc=%d\n", this, this->getRootRefCount());
		return flags().rootRefCount;
	}

	int getReferrerCount();

	void markSurvivorReachable() {
		precond(!isGarbageMarked());
		precond(!isSurvivorReachable());
		rtgc_debug_log(this, "markSurvivorReachable %p rc=%d ac=%d\n",    
			this, this->getRootRefCount(), this->getReferrerCount());
		flags().rootRefCount |= (1 << 22);
	}

	bool isSurvivorReachable() {
		return flags().rootRefCount & (1 << 22);
	}


	void unmarkActiveFinalizer() {
		precond(isActiveFinalizer());
		flags().rootRefCount &= ~(1 << 23);
	}

	void markActiveFinalizer() {
		precond(!isActiveFinalizer());
		flags().rootRefCount |= (1 << 23);
	}

	bool isActiveFinalizerReachable() {
		return flags().rootRefCount & 0x01;
	}

	void unmarkActiveFinalizerReachable() {
		precond(isActiveFinalizerReachable());
		flags().rootRefCount &= ~0x01;
	}

	void markActiveFinalizerReachable() {
		precond(!isActiveFinalizerReachable());
		flags().rootRefCount |= 0x01;
	}

	bool isUnstableMarked() {
		return flags().isUnstable;
	}

	void markUnstable() {
		flags().isUnstable = true;
	}

	void unmarkUnstable() {
		flags().isUnstable = false;
	}

	void markGarbage(const char* reason = NULL);

	void unmarkGarbage(bool resurrected=true) {
		// rtgc_log(resurrected, "resurrected %p\n", this);
		flags().isGarbage = false;
		// flags().isUnstable = false;
		// flags().dirtyReferrerPoints = false;
	}



	bool isStrongRootReachable() {
		return flags().rootRefCount > 1;
	}

	bool isStrongReachable() {
		return isStrongRootReachable() || getNodeInfo().isAnchored();
	}

	int getRootRefCount() {
		return flags().rootRefCount;
	}

	int incrementRootRefCount();

	int decrementRootRefCount();

	bool isGarbageMarked() {
		return flags().isGarbage;
	}

	bool isUnreachable() {
		return flags().rootRefCount == ZERO_ROOT_REF && !getNodeInfo().isAnchored();
	}

	bool isPublished() {
		return flags().isPublished;
	}

	void markPublished() {
		flags().isPublished = true;
	}

	bool getContextFlag() {
		return flags().contextFlag;
	}

	void markContextFlag() {
		flags().contextFlag = true;
	}

	void unmarkContextFlag() {
		flags().contextFlag = false;
	}

#ifdef ASSERT
	bool isNodeLocked() {
		return flags().isNodeLocked;
	}

	void lockNodeInfo() {
		precond(!flags().isNodeLocked);
		debug_only(flags().isNodeLocked = true;)
	}

	void releaseNodeInfo() {
		precond(flags().isNodeLocked);
		debug_only(flags().isNodeLocked = false;)
	}
#endif
};

inline NodeInfo::NodeInfo(GCNode* obj) {
	debug_only(!obj->isNodeLocked();)

	markWord& _mark = *(markWord*)this;
	if (FAT_OOP) {
		_mark = ((markWord*)obj)[1];
	} else if (obj->has_displaced_mark()) {
		_mark = obj->displaced_mark();
	} else {
		_mark = obj->mark();
	}
}

inline LockedNodeInfo::LockedNodeInfo(GCNode* obj) : NodeInfo(obj) {
	_obj = obj;
	debug_only(obj->lockNodeInfo();)
}

inline void LockedNodeInfo::release() {
#ifdef ASSERT
	ensureLocked();
	precond(!_isModified);
	_obj->releaseNodeInfo();
	_refs = -1;
#endif
}

inline void LockedNodeInfo::updateNow() {
	debug_only(_isModified = false;)
	markWord m = *(markWord*)this;
	if (FAT_OOP) {
		((markWord*)_obj)[1] = m;
	} else {
		if (_obj->has_displaced_mark()) {
			_obj->set_displaced_mark(m);
		} else {
			_obj->set_mark(m);
		}
	}
}

inline LockedNodeInfo::~LockedNodeInfo() {
#ifdef ASSERT
	precond(!_isModified);
	if (_refs != (uint32_t)-1) {
		_obj->releaseNodeInfo();
	}
#endif
};

class NodeInfoEditor : public LockedNodeInfo {
public:
	NodeInfoEditor(GCNode* obj) : LockedNodeInfo(obj) {}
	~NodeInfoEditor() { 
		updateNow();
	}
};
}
#endif // SHARE_GC_RTGC_IMPL_RTGCNODE_HPP