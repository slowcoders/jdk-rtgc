#ifndef SHARE_GC_RTGC_IMPL_RTGCNODE_HPP
#define SHARE_GC_RTGC_IMPL_RTGCNODE_HPP

namespace RTGC {

class GCNode {
	int64_t _klass[1];
protected:
	uint32_t _refs;
	int32_t _shortcutId;
	struct {
		int32_t _rootRefCount: 26;
		uint32_t _traceState: 2;
		uint32_t _isPublished: 1;
		uint32_t _hasMultiRef: 1;
		uint32_t _nodeType: 2;
	};
public:
	void clear() { 
		((int64_t*)this)[0] = 0; 
		((int32_t*)this)[2] = 0; 
	}
};

};
#endif // SHARE_GC_RTGC_IMPL_RTGCNODE_HPP