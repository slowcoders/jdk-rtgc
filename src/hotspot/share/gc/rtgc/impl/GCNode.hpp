#ifndef SHARE_GC_RTGC_IMPL_RTGCNODE_HPP
#define SHARE_GC_RTGC_IMPL_RTGCNODE_HPP

namespace RTGC {

static const int ZERO_ROOT_REF = -1;
static const int TRACKABLE_BIT = 1;
enum class TraceState : int {
	NOT_TRACED,
	IN_TRACING,
	TRACE_FINISHED,
};

struct GCFlags {
	uint32_t isTrackable: 1;
	uint32_t isGarbage: 1;
	uint32_t traceState: 2;
	uint32_t isPublished: 1;
	uint32_t hasMultiRef: 1;
	int32_t rootRefCount: 26;
};

class GCNode {
	int64_t _klass[1];
public:
	uint32_t _refs;
	GCFlags _flags;
	union {
		int32_t _shortcutId;
		GCNode* _nextUntrackable;
	};

public:
	void clear() { 
		_refs = 0; 
		*(int*)&_flags = 0;
		_nextUntrackable = NULL; 
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
		_flags.isTrackable = false;
	}

	bool isDestroyed() {
		return _flags.isGarbage && !_flags.isTrackable;
	}

	TraceState getTraceState() {
		return (TraceState)_flags.traceState;
	}

	void setTraceState(TraceState state) {
		_flags.traceState = (int)state;
	}

	void markGarbage() {
		_flags.isGarbage = true;
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
		return --_flags.rootRefCount;
	}

	bool isGarbageMarked() {
		return _flags.isGarbage;
	}

	bool isGarbage() {
		return _flags.rootRefCount <= ZERO_ROOT_REF && !this->hasReferrer();
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