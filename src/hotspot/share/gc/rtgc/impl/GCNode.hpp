#ifndef SHARE_GC_RTGC_IMPL_RTGCNODE_HPP
#define SHARE_GC_RTGC_IMPL_RTGCNODE_HPP

#include "../RTGC.hpp"
#include "../rtgcDebug.hpp"
#include "oops/oop.hpp"
#include "GCPointer.hpp"
#include "../rtgcGlobals.hpp"
#include "../rtgcHeap.hpp"

#define ZERO_ROOT_REF 		0
static const int NO_SAFE_ANCHOR = 0;
static const int INVALID_SHORTCUT = 1;

namespace RTGC {

class ReferrerList;
class SafeShortcut;
static const int 	TRACKABLE_BIT = 1;

class RtNode {
protected:	
	struct {
		uint32_t _jvmFlags: 7;
		uint32_t _hasMultiRef: 1;
		uint32_t _shortcutId: 24;
	}; 

	int32_t _refs;

public:

	bool mayHaveAnchor() const {
		return _refs != 0;
	}

	bool hasAnchor() const;

	bool hasMultiRef() const {
		return _hasMultiRef != 0;
	}

	ShortOOP& getSingleAnchor() const {
		rt_assert(!_hasMultiRef && _refs != 0);
		return *(ShortOOP*)&_refs;
	}

	ReferrerList* getAnchorList() const;

	bool hasShortcut() const {
		return getShortcutId() > INVALID_SHORTCUT;
	}

	SafeShortcut* getShortcut() const;

	int getShortcutId() const {
		return this->_shortcutId;
	}

	bool isAnchorListLocked() {
		rt_assert(_hasMultiRef);
		return (int32_t)_refs > 0;
	}

	int32_t getIdentityHashCode() const {
		rt_assert(_hasMultiRef);
		return _refs;
	}

	GCObject* getSafeAnchor() const;

	bool hasSafeAnchor() const {
		return getShortcutId() > NO_SAFE_ANCHOR;
	}

	void setHasMultiRef(bool multiRef) {
		_hasMultiRef = multiRef;
	}

	void setSingleAnchor(ShortOOP anchor) {
		rt_assert(!_hasMultiRef);
		_refs = anchor.getOffset();
        rt_assert(!rtHeap::useModifyFlag() || (_refs & 1) == 0); 
	}

	void removeSingleAnchor() {
		rt_assert(!_hasMultiRef);
		_refs = 0;
	}

	void setAnchorList(ReferrerList* anchors);

	void setShortcutId_unsafe(int shortcutId) {
		this->_shortcutId = shortcutId;
	}

	void setSafeAnchor(GCObject* anchor);

	void invalidateSafeAnchor() {
		// no-shortcut. no safe-anchor.
		setShortcutId_unsafe(NO_SAFE_ANCHOR);
	}

	void invalidateShortcutId() {
		rt_assert(hasSafeAnchor());
		// no-shortcut. but this has valid safe-anchor.
		setShortcutId_unsafe(INVALID_SHORTCUT);
	}
};

struct GCFlags {
	uint32_t isTrackableOrDestroyed: 1;
	uint32_t isYoungRoot: 1;
#if RTGC_SHARE_GC_MARK
	uint32_t isMarked: 1;
#else
	uint32_t isGarbage: 1;
#endif
	uint32_t dirtyReferrerPoints: 1;
	uint32_t isUnstable: 1;

	uint32_t contextFlag: 1;
	uint32_t isPublished: 1;
	uint32_t immortal: 1;
#if ZERO_ROOT_REF < 0	
	int32_t rootRefCount: 24;
#else
	uint32_t rootRefCount: 24;
#endif
};

class GCNode : private oopDesc {
friend class RtNode;
	GCFlags& flags() {
		return *(GCFlags*)((uintptr_t)this + flags_offset());
	}

public:
	static int   _cntTrackable;
	static void* g_trackable_heap_start;
	static bool in_progress_adjust_pointers;

	static int flags_offset() {
		return oopDesc::klass_gap_offset_in_bytes();
	}

	const RtNode* node_() {
		if (RTGC_FAT_OOP) {
			rt_assert(sizeof(RtNode) == sizeof(markWord));
			return ((RtNode*)this) + 1;
		}
		rt_assert(!this->is_forwarded() || g_in_progress_marking);
		if (this->has_displaced_mark()) {
			return reinterpret_cast<RtNode*>(this->mark_addr()->displaced_mark_addr_at_safepoint());
		} else {
			return reinterpret_cast<RtNode*>(this->mark_addr());
		}
	}

	RtNode* getMutableNode() {
		return (RtNode*)node_(); 
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
		rt_assert(!this->isYoungRoot());
		flags().isYoungRoot = true;
	}

	void unmarkYoungRoot() {
		rt_assert(this->isYoungRoot());
		flags().isYoungRoot = false;
	}

	bool isTrackable() {
		rt_assert(!in_progress_adjust_pointers);
		return this->isTrackable_unsafe();
	}

	bool isTrackable_unsafe() {
		if (!AUTO_TRACKABLE_MARK_BY_ADDRESS) {
			return flags().isTrackableOrDestroyed;
		} else {
			return (void*)this >= g_trackable_heap_start;
		}
	}

	bool is_adjusted_trackable() {
		if (this->isTrackable_unsafe()) return true;
		return AUTO_TRACKABLE_MARK_BY_ADDRESS && 
			rtHeap::in_full_gc && 
			((GCNode*)(void*)this->forwardee())->isTrackable_unsafe();
	}	

	void markTrackable() {
		if (!AUTO_TRACKABLE_MARK_BY_ADDRESS) {
			rt_assert(!this->isTrackable());
			flags().isTrackableOrDestroyed = true;
		} else {
			fatal("should not reach here.");
		}
	}

	void markDestroyed() {
		rt_assert(isGarbageMarked());
#ifdef ASSERT		
		_cntTrackable--;
#endif		
		flags().isTrackableOrDestroyed = AUTO_TRACKABLE_MARK_BY_ADDRESS;
	}

	bool isDestroyed() {
		return isTrackable_unsafe() && !isAlive() && 
			flags().isTrackableOrDestroyed == AUTO_TRACKABLE_MARK_BY_ADDRESS;
	}

	bool isActiveFinalizer() {
		return flags().rootRefCount & (1 << 23);
	}

	int unmarkSurvivorReachable() {
		rt_assert(isSurvivorReachable());
		rt_assert(!isGarbageMarked());
		flags().rootRefCount &= ~(1 << 22);
		rtgc_debug_log(this, "unmarkSurvivorReachable %p rc=%d\n", this, this->getRootRefCount());
		return flags().rootRefCount;
	}

	int getReferrerCount();

	void markSurvivorReachable_unsafe() {
		rt_assert(!isSurvivorReachable());
		rtgc_debug_log(this, "markSurvivorReachable %p rc=%d ac=%d\n",    
			this, this->getRootRefCount(), this->getReferrerCount());
		flags().rootRefCount |= (1 << 22);
	}

	void markSurvivorReachable() {
		rt_assert(!isGarbageMarked());
		markSurvivorReachable_unsafe();
	}

	bool isSurvivorReachable() {
		return flags().rootRefCount & (1 << 22);
	}


	void unmarkActiveFinalizer() {
		rt_assert(isActiveFinalizer());
		flags().rootRefCount &= ~(1 << 23);
	}

	void markActiveFinalizer() {
		rt_assert(!isActiveFinalizer());
		flags().rootRefCount |= (1 << 23);
	}

	bool isActiveFinalizerReachable() {
		return flags().rootRefCount & 0x01;
	}

	void unmarkActiveFinalizerReachable() {
		rt_assert(isActiveFinalizerReachable());
		flags().rootRefCount &= ~0x01;
	}

	void markActiveFinalizerReachable() {
		rt_assert(!isActiveFinalizerReachable());
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
		#if RTGC_SHARE_GC_MARK
			flags().isMarked = true;
		#else
			flags().isGarbage = false;
		#endif
		// flags().isUnstable = false;
		// flags().dirtyReferrerPoints = false;
	}



	bool isStrongRootReachable() {
		return flags().rootRefCount > 1;
	}

	bool isStrongReachable() {
		return isStrongRootReachable() || node_()->hasAnchor();
	}

	int getRootRefCount() {
		return flags().rootRefCount;
	}

	int incrementRootRefCount();

	int decrementRootRefCount();

	bool isAlive() {
		#if RTGC_SHARE_GC_MARK
			return flags().isMarked;
		#else
			return !flags().isGarbage;
		#endif
	}

	bool isGarbageTrackable() {
		if (RTGC_SHARE_GC_MARK) {		
			return this->is_adjusted_trackable() && !this->isAlive();
		} else {
			return !this->isAlive();
		}
	}

	bool isGarbageMarked() {
		rt_assert(!RTGC_SHARE_GC_MARK || this->is_adjusted_trackable());
		return !isAlive();
	}

	bool isUnreachable() {
		return flags().rootRefCount == ZERO_ROOT_REF && !node_()->hasAnchor();
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

	void markImmortal() {
		flags().immortal = false;
	}

	bool isImmortal() {
		return flags().immortal;
	}
};

}
#endif // SHARE_GC_RTGC_IMPL_RTGCNODE_HPP