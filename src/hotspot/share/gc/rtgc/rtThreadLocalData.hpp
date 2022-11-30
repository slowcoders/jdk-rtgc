#include "oops/oop.inline.hpp"
#include "impl/GCObject.hpp"

namespace RTGC {

class FieldUpdateLog {
public:
  address   _anchor;
  int32_t   _offset;
  narrowOop _erased;

  void init(oopDesc* anchor, volatile narrowOop* field, narrowOop erased);
  void updateAnchorList();
  static void add(oopDesc* anchor, volatile narrowOop* field, narrowOop erased);
};

class FieldUpdateReport {
  FieldUpdateReport* _next;
  FieldUpdateLog* _end;
  FieldUpdateLog* _sp;

  static FieldUpdateReport* g_report_q;

public:
  FieldUpdateReport() {
    _next = NULL; 
    _end = _sp = (FieldUpdateLog*)this;
  }

  FieldUpdateLog** stack_pointer() { return &_sp; }

  bool is_full() { return _sp <= (void*)&_sp; }

  FieldUpdateLog* first_log() { return is_full() ? (FieldUpdateLog*)&_sp + 1 : _sp; }

  FieldUpdateLog* end_of_log() { return _end; }

  FieldUpdateReport* next() { return _next; }

  static FieldUpdateReport* allocate();

  static void process_update_logs();

  static void reset_gc_context(bool promotion_finished);
};

class RtThreadLocalData {    
  FieldUpdateLog** _log_sp;
  void* _trackable_heap_start;
  static FieldUpdateReport  g_dummy_report;

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
    _log_sp = g_dummy_report.stack_pointer();
  }

  static void addUpdateLog(oopDesc* anchor, volatile narrowOop* field, narrowOop erased, RtThreadLocalData* rtData);

#ifdef ASSERT
  void checkLastLog(oopDesc* anchor, volatile narrowOop* field, narrowOop erased) {
    FieldUpdateLog* log = _log_sp[0];
    printf("log sp  %p start=%p\n", log, _log_sp);
    assert(log->_anchor == (void*)anchor, " %p %p\n", log->_anchor, anchor);
    assert(log->_offset == (int32_t)(intptr_t)field, " %d %d\n", log->_offset, (int32_t)(intptr_t)field);
    assert(log->_erased == erased, " %x %x\n", (int32_t)log->_erased, (int32_t)erased);
  }
#endif
};

};



