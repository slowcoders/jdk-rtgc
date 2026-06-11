
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

template <class T, bool nullable>
class CompressedPointer {
public:
    typedef uint32_t OffsetType;
    CompressedPointer(T* ptr) {
        if (nullable && ptr == NULL) {
            _ofs = 0;
        } else {
            rt_assert(ptr != NULL);
            _ofs = _pointer2offset(ptr);
            rt_assert(_ofs != 0);
        }
    }

    operator T* () const {
        if (nullable && _ofs == 0) {
            return NULL;
        }
        return (T*)_offset2Pointer(_ofs);
    }

    T* operator -> () const {
        if (nullable && _ofs == 0) {
            return NULL;
        }
        return (T*)_offset2Pointer(_ofs);
    }

    OffsetType getOffset() const {
        return _ofs;
    }
private:
    OffsetType _ofs;
};

typedef CompressedPointer<GCObject, false> ShortOOP;

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