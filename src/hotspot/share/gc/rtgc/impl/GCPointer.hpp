
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

template <class T> 
class GCObjectArray;

template <class T> 
class GCPrimitiveArray;

uint32_t _pointer2offset(void* ref, void* origin);

void* _offset2Pointer(uint32_t offset, void* origin);

#define _offset2Object(offset, origin) (GCObject*)RTGC::_offset2Pointer(offset, origin)
#define USE_32BIT_POINTER 1

class GCObject;

class ShortOOP {
    uint32_t _ofs;
public:
    ShortOOP(GCObject* ptr) {
        _ofs = _pointer2offset(ptr, this);
    }

    operator GCObject* () {
        return (GCObject*)_offset2Pointer(_ofs, this);
    }
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
		return (_offset == 0) ? nullptr : (T*)_offset2Pointer(_offset, this);
#else
		return _ptr;
#endif
	}
};

}
