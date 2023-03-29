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

static const bool AUTO_TRACKABLE_MARK_BY_ADDRESS = true;
namespace RTGC {

class ReferrerList;
class SafeShortcut;
static const int 	TRACKABLE_BIT = 1;


#define ROOT_REF_COUNT_BITS 	25

struct GCFlags {
	uint32_t isAcyclic: 1;
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
	// uint32_t isPublished: 1;
	// uint32_t immortal: 1;
#if ZERO_ROOT_REF < 0	
	int32_t rootRefCount: ROOT_REF_COUNT_BITS;
#else
	uint32_t rootRefCount: ROOT_REF_COUNT_BITS;
#endif
};


class GCNode {
	static const int is_active_finalizer_value = 1 << (ROOT_REF_COUNT_BITS - 1);
	static const int survivor_reachable_value = 1 << (ROOT_REF_COUNT_BITS - 2);
	static const int finalizer_reachalbe_value = 1;

#if RTGC_FAT_OOP
	markWord _jvm_markword;
#endif	

	struct {
		uint32_t _jvmFlags: 7;
		uint32_t _hasMultiRef: 1;
		uint32_t _shortcutId: 24;
	}; 

	int32_t _refs;

	narrowOop _jvm_klass;
	
	GCFlags _flags;

public:
	static int   _cntTrackable;
	static void* g_trackable_heap_start;
	static bool in_progress_adjust_pointers;

	static int flags_offset() {
		return oopDesc::klass_gap_offset_in_bytes();
	}

	void markDirtyReferrerPoints() {
		rt_assert(!isDirtyReferrerPoints());
		_flags.dirtyReferrerPoints = true;
	}	

	void unmarkDirtyReferrerPoints() {
		_flags.dirtyReferrerPoints = false;
	}	

	bool isDirtyReferrerPoints() {
		return _flags.dirtyReferrerPoints;
	}	

	bool isYoungRoot() {
		return _flags.isYoungRoot;
	}

	void markYoungRoot() {
		rt_assert(!this->isYoungRoot());
		_flags.isYoungRoot = true;
	}

	void unmarkYoungRoot() {
		rt_assert(this->isYoungRoot());
		rtgc_debug_log(this, "unmark young root %p", this);
		_flags.isYoungRoot = false;
	}

	bool isAcyclic() {
		return _flags.isAcyclic;
	}

	bool isTrackable() {
		rt_assert(!in_progress_adjust_pointers);
		return this->isTrackable_unsafe();
	}

	bool isDirtyAnchor() {
		return !isTrackable();
	}
	
	bool isTrackable_unsafe() {
		if (!AUTO_TRACKABLE_MARK_BY_ADDRESS) {
			return _flags.isTrackableOrDestroyed;
		} else {
			return (void*)this >= g_trackable_heap_start;
		}
	}

	bool is_adjusted_trackable() {
		if (!in_progress_adjust_pointers) 
			return this->isTrackable_unsafe();
		oop forwarded_p = cast_to_oop(this)->forwardee();
		if (forwarded_p == NULL) forwarded_p = cast_to_oop(this);
		return to_node(forwarded_p)->isTrackable_unsafe();
	}	

	void markTrackable() {
		if (!AUTO_TRACKABLE_MARK_BY_ADDRESS) {
			rt_assert(!this->isTrackable());
			_flags.isTrackableOrDestroyed = true;
		} else {
			fatal("should not reach here.");
		}
	}

	void markDestroyed() {
		rt_assert(isGarbageMarked());
#ifdef ASSERT		
		_cntTrackable--;
#endif		
		_flags.isTrackableOrDestroyed = AUTO_TRACKABLE_MARK_BY_ADDRESS;
	}

	bool isDestroyed() {
		return isTrackable_unsafe() && !isAlive() && 
			_flags.isTrackableOrDestroyed == AUTO_TRACKABLE_MARK_BY_ADDRESS;
	}

	bool isActiveFinalizer() {
		return _flags.rootRefCount & is_active_finalizer_value;
	}

	int unmarkSurvivorReachable() {
		rt_assert(isSurvivorReachable());
		rt_assert(!isGarbageMarked());
		_flags.rootRefCount &= ~survivor_reachable_value;
		rtgc_debug_log(this, "unmarkSurvivorReachable %p rc=%d", this, this->getRootRefCount());
		return _flags.rootRefCount;
	}

	int getAnchorCount();

	void markSurvivorReachable_unsafe() {
		rt_assert(!isSurvivorReachable());
		rtgc_debug_log(this, "markSurvivorReachable %p rc=%d ac=%d",    
			this, this->getRootRefCount(), this->getAnchorCount());
		_flags.rootRefCount |= survivor_reachable_value;
	}

	void markSurvivorReachable() {
		rt_assert(!isGarbageMarked());
		markSurvivorReachable_unsafe();
	}

	bool isSurvivorReachable() {
		return (_flags.rootRefCount & survivor_reachable_value) != 0;
	}


	void unmarkActiveFinalizer() {
		rt_assert(isActiveFinalizer());
		_flags.rootRefCount &= ~is_active_finalizer_value;
	}

	void markActiveFinalizer() {
		rt_assert(!isActiveFinalizer());
		_flags.rootRefCount |= is_active_finalizer_value;
	}

	bool isActiveFinalizerReachable() {
		return _flags.rootRefCount & 0x01;
	}

	void unmarkActiveFinalizerReachable() {
		rt_assert(isActiveFinalizerReachable());
		_flags.rootRefCount &= ~0x01;
	}

	void markActiveFinalizerReachable() {
		rt_assert(!isActiveFinalizerReachable());
		_flags.rootRefCount |= 0x01;
	}

	bool isUnstableMarked() {
		return _flags.isUnstable;
	}

	void markUnstable() {
		_flags.isUnstable = true;
	}

	void unmarkUnstable() {
		_flags.isUnstable = false;
	}

	void markGarbage(const char* reason = NULL);

	void unmarkGarbage(bool resurrected=true) {
		// rtgc_log(resurrected, "resurrected %p\n", this);
		#if RTGC_SHARE_GC_MARK
			_flags.isMarked = true;
		#else
			_flags.isGarbage = false;
		#endif
		// _flags.isUnstable = false;
		// _flags.dirtyReferrerPoints = false;
	}



	bool isStrongRootReachable() {
		return _flags.rootRefCount > 1;
	}

	bool isStrongReachable() {
		return isStrongRootReachable() || this->hasAnchor();
	}

	int getRootRefCount() {
		return _flags.rootRefCount;
	}

	int incrementRootRefCount();

	int decrementRootRefCount();

	bool isAlive() {
		#if RTGC_SHARE_GC_MARK
			return _flags.isMarked;
		#else
			return !_flags.isGarbage;
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
		return _flags.rootRefCount == ZERO_ROOT_REF && !this->hasAnchor();
	}

	bool isPublished() {
		return false;//_flags.isPublished;
	}

	void markPublished() {
		// _flags.isPublished = true;
	}

	bool getContextFlag() {
		return _flags.contextFlag;
	}

	void markContextFlag() {
		_flags.contextFlag = true;
	}

	void unmarkContextFlag() {
		_flags.contextFlag = false;
	}

	void markImmortal() {
		// _flags.immortal = false;
	}

	bool isImmortal() {
		return false;//_flags.immortal;
	}

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

}
#endif // SHARE_GC_RTGC_IMPL_RTGCNODE_HPP