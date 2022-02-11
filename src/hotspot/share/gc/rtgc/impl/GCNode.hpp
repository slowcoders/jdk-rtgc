#ifndef SHARE_GC_RTGC_IMPL_RTGCNODE_HPP
#define SHARE_GC_RTGC_IMPL_RTGCNODE_HPP

namespace RTGC {

static const int ZERO_ROOT_REF = -1;

enum class TraceState : int {
	NOT_TRACED,
	IN_TRACING,
	TRACE_FINISHED,
};

enum class NodeType : int {
	Reachable,
	Garbage,
	Destroyed,
};

struct GCFlags {
	int32_t rootRefCount: 26;
	uint32_t traceState: 2;
	uint32_t isPublished: 1;
	uint32_t hasMultiRef: 1;
	uint32_t nodeType: 2;
};

class GCNode {
	int64_t _klass[1];
public:
	uint32_t _refs;
	int32_t _shortcutId;
	GCFlags _flags;

public:
	void clear() { 
		((int64_t*)this)[0] = 0; 
		((int32_t*)this)[2] = 0; 
	}

	bool isOld() {
		return _flags.isPublished;
	}

	bool isReverseTrackable() {
		return this->isOld();
	}

	void markOld() {
		precond(!this->isOld());
		_flags.isPublished = true;
	}

	void markDestroyed() {
		_flags.nodeType = (int)NodeType::Destroyed;
	}

	bool isDestroyed() {
		return _flags.nodeType == (int)NodeType::Destroyed;;
	}

	TraceState getTraceState() {
		return (TraceState)_flags.traceState;
	}

	void setTraceState(TraceState state) {
		_flags.traceState = (int)state;
	}

	void markGarbage() {
		_flags.nodeType = (int)NodeType::Garbage;
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

	NodeType getNodeType() {
		return (NodeType)_flags.nodeType;
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