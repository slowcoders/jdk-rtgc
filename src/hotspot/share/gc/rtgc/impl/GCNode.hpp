#ifndef SHARE_GC_RTGC_IMPL_RTGCNODE_HPP
#define SHARE_GC_RTGC_IMPL_RTGCNODE_HPP

#include "../RTGC.hpp"
#include "../rtgcDebug.hpp"

#define ZERO_ROOT_REF 		0
static const int NO_SAFE_ANCHOR = 0;
static const int INVALID_SHORTCUT = 1;

extern int cnt_debug_hit;
namespace RTGC {

static const int 	TRACKABLE_BIT = 1;

struct GCFlags {
	uint32_t isTrackable: 1;
	uint32_t isYoungRoot: 1;
	uint32_t isGarbage: 1;
	uint32_t dirtyReferrerPoints: 1;
	uint32_t isUnstable: 1;

	uint32_t contextFlag: 1;
	uint32_t isPublished: 1;
	uint32_t hasMultiRef: 1;
#if ZERO_ROOT_REF < 0	
	int32_t rootRefCount: 24;
#else
	uint32_t rootRefCount: 24;
#endif
};

class GCNode {
	int64_t _klass[1];
public:
	union {
		int32_t _shortcutId;
		GCNode* _nextUntrackable;
	};
	uint32_t _refs;
	GCFlags _flags;
	static int _cntTrackable;

public:
	void clearFlags() { 
		_refs = 0; 
		*(int*)&_flags = 0;
		_nextUntrackable = NULL; 
	}

	void markDirtyReferrerPoints() {
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
		precond(!this->isYoungRoot());
		_flags.isYoungRoot = true;
	}

	void unmarkYoungRoot() {
		precond(this->isYoungRoot());
		_flags.isYoungRoot = false;
	}

	bool isTrackable() {
		return _flags.isTrackable;
	}

	void markTrackable() {
		precond(!this->isTrackable());
		_flags.isTrackable = true;
	}

	void markDestroyed() {
		precond(_flags.isGarbage);
#ifdef ASSERT		
		_cntTrackable--;
#endif		
		_flags.isTrackable = false;
	}

	bool isDestroyed() {
		return _flags.isGarbage && !_flags.isTrackable;
	}

	bool isActiveFinalizer() {
		return _flags.rootRefCount & (1 << 23);
	}

	int unmarkSurvivorReachable() {
		precond(isSurvivorReachable());
		_flags.rootRefCount &= ~(1 << 22);
		rtgc_debug_log(this, "unmarkSurvivorReachable %p rc=%d\n", this, this->getRootRefCount());
		return _flags.rootRefCount;
	}

	void markSurvivorReachable() {
		precond(!isGarbageMarked());
		precond(!isSurvivorReachable());
		rtgc_debug_log(this, "markSurvivorReachable[%d] %p rc=%d anchored=%d\n",    
			cnt_debug_hit, this, this->getRootRefCount(), this->isAnchored());
		_flags.rootRefCount |= (1 << 22);
	}

	bool isSurvivorReachable() {
		return _flags.rootRefCount & (1 << 22);
	}


	void unmarkActiveFinalizer() {
		precond(isActiveFinalizer());
		_flags.rootRefCount &= ~(1 << 23);
	}

	void markActiveFinalizer() {
		precond(!isActiveFinalizer());
		_flags.rootRefCount |= (1 << 23);
	}

	bool isActiveFinalizerReachable() {
		return _flags.rootRefCount & 0x01;
	}

	void unmarkActiveFinalizerReachable() {
		precond(isActiveFinalizerReachable());
		_flags.rootRefCount &= ~0x01;
	}

	void markActiveFinalizerReachable() {
		precond(!isActiveFinalizerReachable());
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
		_flags.isGarbage = false;
		// _flags.isUnstable = false;
		// _flags.dirtyReferrerPoints = false;
	}


	bool isAnchored() {
		return this->_refs != 0;
	}

	bool hasMultiRef() {
		return _flags.hasMultiRef != 0;
	}

	void setHasMultiRef(bool multiRef) {
		_flags.hasMultiRef = multiRef;
	}

	bool isStrongRootReachable() {
		return _flags.rootRefCount > 1;
	}

	bool isStrongReachable() {
		return isStrongRootReachable() || isAnchored();
	}

	void invalidateAnchorList_unsafe() {
		_flags.hasMultiRef = 0;
		this->_refs = 0;
	}

	int getRootRefCount() {
		return _flags.rootRefCount;
	}

	int incrementRootRefCount() {
		assert(!this->isGarbageMarked(), "wrong ref-count %p(%s) tr=%d rc=%d garbage=%d\n", 
			this, RTGC::getClassName(this), isTrackable(), _flags.rootRefCount, isGarbageMarked());
		return (_flags.rootRefCount += 2);
	}

	int decrementRootRefCount() {
		assert(!this->isGarbageMarked(), "wrong ref-count %p(%s) tr=%d rc=%d garbage=%d\n", 
			this, RTGC::getClassName(this), isTrackable(), _flags.rootRefCount, isGarbageMarked());
		assert(_flags.rootRefCount > 1, "wrong ref-count %p(%s) rc=%d garbage=%d\n", 
			this, RTGC::getClassName(this), _flags.rootRefCount, isGarbageMarked());
		return (_flags.rootRefCount -= 2);
	}

	bool isGarbageMarked() {
		return _flags.isGarbage;
	}

	bool isUnreachable() {
		return _flags.rootRefCount == ZERO_ROOT_REF && !this->isAnchored();
	}

	bool isPublished() {
		return _flags.isPublished;
	}

	void markPublished() {
		_flags.isPublished = true;
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
};

};
#endif // SHARE_GC_RTGC_IMPL_RTGCNODE_HPP