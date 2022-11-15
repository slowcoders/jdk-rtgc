
#include "oops/oop.inline.hpp"

namespace RTGC {
    struct FieldUpdateLog {
        oop _anchor;
        int _offset;
        narrowOop _erased;
    };

  class RtThreadLocalData {
  public:
    void* _update_log_stack;
    void* _update_log_top;

    RtThreadLocalData();

    static RtThreadLocalData* data(Thread* thread) {
      return thread->gc_data<RtThreadLocalData>();
    }

    static void create(Thread* thread) {
      new (data(thread)) RtThreadLocalData();
    }

    static void reset_gc_context();

    static void destroy(Thread* thread) {
      data(thread)->~RtThreadLocalData();
    }
  };
};



