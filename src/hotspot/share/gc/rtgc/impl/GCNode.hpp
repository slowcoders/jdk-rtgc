#ifndef SHARE_GC_RTGC_IMPL_RTGCNODE_HPP
#define SHARE_GC_RTGC_IMPL_RTGCNODE_HPP

#include "../RTGC.hpp"
#include "../rtgcDebug.hpp"

#define ZERO_ROOT_REF 		0

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
	GCFlags _flags;
	uint32_t _refs;
	union {
		int32_t _shortcutId;
		GCNode* _nextUntrackable;
	};
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

	bool isActiveRef() {
		return _flags.contextFlag;
	}

	void unmarkActiveRef() {
		_flags.contextFlag = 0;
	}

	void markActiveRef() {
		_flags.contextFlag = 1;
	}

	bool isUnstable() {
		return _flags.isUnstable;
	}

	void markUnstable() {
		_flags.isUnstable = true;
	}

	void unmarkUnstable() {
		_flags.isUnstable = false;
	}

	void markGarbage(const char* reason = NULL);

	void unmarkGarbage() {
		_flags.isGarbage = false;
	}


	bool hasReferrer() {
		return this->_refs != 0;
	}

	bool hasMultiRef() {
		return _flags.hasMultiRef != 0;
	}

	void setHasMultiRef(bool multiRef) {
		_flags.hasMultiRef = multiRef;
	}

	int getRootRefCount() {
		return _flags.rootRefCount;
	}

	int incrementRootRefCount() {
		return ++_flags.rootRefCount;
	}

	int decrementRootRefCount() {
		assert(_flags.rootRefCount > ZERO_ROOT_REF, "wrong ref-count %p(%d) garbage=%d\n", 
			this, _flags.rootRefCount, isGarbageMarked());
		return --_flags.rootRefCount;
	}

	bool isGarbageMarked() {
		return _flags.isGarbage;
	}

	bool isAnchored() {
		return _flags.rootRefCount > ZERO_ROOT_REF || this->hasReferrer();
	}

	bool isUnsafe() {
		return _flags.rootRefCount <= ZERO_ROOT_REF && this->_shortcutId == 0;
	}

	bool isPublished() {
		return _flags.isPublished;
	}
};

};
#endif // SHARE_GC_RTGC_IMPL_RTGCNODE_HPP