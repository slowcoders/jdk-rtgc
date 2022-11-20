#include "oops/oop.inline.hpp"
#include "FieldUpdateLog.hpp"

namespace RTGC {

class RtThreadLocalData {    
  FieldUpdateLog** _log_sp;

public:

  RtThreadLocalData() { reset_field_update_log_sp(); }

  static RtThreadLocalData* data(Thread* thread) {
    return thread->gc_data<RtThreadLocalData>();
  }

  static void create(Thread* thread) {
    new (data(thread)) RtThreadLocalData();
  }

  static void destroy(Thread* thread) {
    data(thread)->~RtThreadLocalData();
  }

  void reset_field_update_log_sp() {
    _log_sp = FieldUpdateReport::default_stack_pointer();
  }

  FieldUpdateLog* allocateLog() {
    FieldUpdateLog* log = --_log_sp[0];
    if (log <= (void*)_log_sp) {
      log = allocate_log_in_new_stack();
    } 
    return log;
  }

  FieldUpdateLog* allocate_log_in_new_stack() {
    _log_sp = FieldUpdateReport::allocate()->stack_pointer();
    return --_log_sp[0];
  }  
};

};



