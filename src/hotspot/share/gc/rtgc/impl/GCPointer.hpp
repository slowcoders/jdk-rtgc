
#ifndef SHARE_GC_RTGC_IMPL_RTGC_POINTER_HPP
#define SHARE_GC_RTGC_IMPL_RTGC_POINTER_HPP

#include <memory.h>
#include "utilities/globalDefinitions_gcc.hpp"

#ifdef _MSC_VER
    #define NO_INLINE       __declspec(noinline)
#ifndef	THREAD_LOCAL
    #define THREAD_LOCAL    __declspec(thread)
#endif
#else
    #define NO_INLINE       __attribute__((noinline))
#ifndef	THREAD_LOCAL
    #define THREAD_LOCAL    thread_local
#endif
#endif

namespace RTGC {

class GCObject;

uint32_t _pointer2offset(void* ref);

void* _offset2Pointer(uint32_t offset);

#define _offset2Object(offset) (GCObject*)RTGC::_offset2Pointer(offset)
#define USE_32BIT_POINTER 1

class GCObject;

class ShortOOP {
public:
    typedef uint32_t OffsetType;
    ShortOOP(GCObject* ptr) {
        rt_assert(ptr != NULL);
        _ofs = _pointer2offset(ptr);
        rt_assert(_ofs != 0);
    }

    ShortOOP(struct UnsafeOffset* ptr) {
        _ofs = (uint32_t)(uintptr_t)ptr;
        rt_assert(_ofs != 0);
    }

    operator GCObject* () const {
        return (GCObject*)_offset2Pointer(_ofs);
    }

    GCObject* operator -> () const {
        return (GCObject*)_offset2Pointer(_ofs);
    }

    OffsetType getOffset() const {
        return _ofs;
    }
private:
    OffsetType _ofs;
};

template <class T>
class OffsetPointer {
#if USE_32BIT_POINTER
	uint32_t _offset;
#else
	T* _ptr;
#endif
public:
	OffsetPointer() {}

	OffsetPointer(std::nullptr_t) {
#if USE_32BIT_POINTER
		_offset = 0;
#else
		_ptr = nullptr;
#endif
	}

	T* getPointer() {
#if USE_32BIT_POINTER
		return (_offset == 0) ? nullptr : (T*)_offset2Pointer(_offset);
#else
		return _ptr;
#endif
	}
};

}
#endif // SHARE_GC_RTGC_IMPL_RTGC_POINTER_HPP