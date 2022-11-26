#include "oops/oop.inline.hpp"
#include "FieldUpdateLog.hpp"

namespace RTGC {

class RtThreadLocalData {    
  void* _trackable_heap_start;
  FieldUpdateLog** _log_sp;

public:

  RtThreadLocalData();

  static RtThreadLocalData* data(Thread* thread) {
    return thread->gc_data<RtThreadLocalData>();
  }

  static void create(Thread* thread) {
    new (data(thread)) RtThreadLocalData();
  }

  static void destroy(Thread* thread) {
    data(thread)->~RtThreadLocalData();
  }

  static ByteSize trackable_heap_start_offset() {
    return Thread::gc_data_offset() + byte_offset_of(RtThreadLocalData, _trackable_heap_start);
  }

  static ByteSize log_sp_offset() {
    return Thread::gc_data_offset() + byte_offset_of(RtThreadLocalData, _log_sp);
  }

  void reset_field_update_log_sp() {
    _log_sp = FieldUpdateReport::default_stack_pointer();
  }

  FieldUpdateLog* allocateLog() {
    FieldUpdateLog* log = --_log_sp[0];
    // rtgc_log(true, "add log(%p) sp= %p\n", log, _log_sp);
    if (log <= (void*)_log_sp) {
      log = allocate_log_in_new_stack();
    } 
    return log;
  }

  FieldUpdateLog* allocate_log_in_new_stack() {
    // rtgc_log(true, "allocate_log_in_new_stack\n");
    _log_sp = FieldUpdateReport::allocate()->stack_pointer();
    return --_log_sp[0];
  }  

};

};



