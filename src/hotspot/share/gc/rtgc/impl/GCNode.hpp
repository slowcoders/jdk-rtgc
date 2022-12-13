#ifndef SHARE_GC_RTGC_IMPL_RTGCNODE_HPP
#define SHARE_GC_RTGC_IMPL_RTGCNODE_HPP

#include "../RTGC.hpp"
#include "../rtgcDebug.hpp"
#include "oops/oop.hpp"

#define ZERO_ROOT_REF 		0
static const int NO_SAFE_ANCHOR = 0;
static const int INVALID_SHORTCUT = 1;

static const bool USE_EXPLICIT_TRACKABLE_MARK = true;
namespace RTGC {

class ReferrerList;
static const int 	TRACKABLE_BIT = 1;

struct NodeInfo {
	struct {
		uint32_t _jvmFlags: 7;
		uint32_t _hasMultiRef: 1;
#ifdef ASSERT
		uint32_t _shortCutId: 23;
		uint32_t _isModified: 1;
#else
		uint32_t _shortCutId: 24;
#endif
	}; 

	NodeInfo(uintptr_t v) {
		*(uintptr_t*)this = v;
		precond(!_isModified);
	}

	ShortOOP _refs;

	bool isAnchored() {
		return _refs.getOffset() != 0;
	}

	bool hasMultiRef() {
		return _hasMultiRef != 0;
	}

	// void setHasMultiRef(bool multiRef) {
	// 	_hasMultiRef = multiRef;
	// }

	ShortOOP& getSingleAnchor() {
		precond(!_hasMultiRef);
		return _refs;
	}

	void setSingleAnchor(ShortOOP anchor) {
		precond(!_hasMultiRef);
		_refs = anchor;
		debug_only(_isModified = true;)
	}

	void removeSingleAnchor() {
		precond(!_hasMultiRef);
		*(OffsetType*)&_refs = 0;
		debug_only(_isModified = true;)
	}

	void invalidateAnchorList_unsafe() {
		_hasMultiRef = 0;
		removeSingleAnchor();
	}

	ReferrerList* getAnchorList();

	void setAnchorList(ReferrerList* anchors);

	ReferrerList* getAnchorList();



	bool hasShortcut() {
		return getShortcutId() > INVALID_SHORTCUT;
	}

	SafeShortcut* getShortcut();

	int getShortcutId() {
		return this->_shortcutId;
	}

	void setShortcutId_unsafe(int shortcutId) {
		this->_shortcutId = shortcutId;
	}


	GCObject* getSafeAnchor();

	void setSafeAnchor(GCObject* anchor);

	void invalidateSafeAnchor() {
		// no-shortcut. no safe-anchor.
		setShortcutId_unsafe(NO_SAFE_ANCHOR);
	}

	bool hasSafeAnchor() {
		return getShortcutId() > NO_SAFE_ANCHOR;
	}

	void invalidateShortcutId() {
		precond(hasSafeAnchor());
		// no-shortcut. but this has valid safe-anchor.
		setShortcutId_unsafe(INVALID_SHORTCUT);
	}


#ifdef ASSERT
	void clearModified() {
		_isModified = false;
	}

	~NodeInfo() {
		precond(!_isModified);
	}
#endif
};

struct GCFlags {
	uint32_t isTrackableOrDestroyed: 1;
	uint32_t isYoungRoot: 1;
	uint32_t isGarbage: 1;
	uint32_t dirtyReferrerPoints: 1;
	uint32_t isUnstable: 1;

	uint32_t contextFlag: 1;
	uint32_t isPublished: 1;
	uint32_t unused: 1;
#if ZERO_ROOT_REF < 0	
	int32_t rootRefCount: 24;
#else
	uint32_t rootRefCount: 24;
#endif
};

class GCNode : private oopDesc {

	GCFlags& flags() {
		return *(GCFlags*)((uintptr_t)this + oopDesc::klass_gap_offset_in_bytes());
	}

public:
	void clearFlags() { 
		flags() = 0;
	}

	NodeInfo getNodeInfo() {
		markWord m;
		if (has_displaced_mark()) {
			m = displaced_mark();
		} else {
			m = m();
		}
		return NodeInfo(m.value());
	}

	void setNodeInfo(NodeInfo& nx) {
		nx.clearModified();
		markWord m = *(markWord*)&nx;
		if (has_displaced_mark()) {
			set_displaced_mark(m);
		} else {
			set_mark(m);
		}
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
		return isStrongRootReachable() || isAnchored();
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
		return flags().rootRefCount == ZERO_ROOT_REF && !getNode().isAnchored();
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
};

};
#endif // SHARE_GC_RTGC_IMPL_RTGCNODE_HPP