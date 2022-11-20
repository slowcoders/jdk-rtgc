#include "oops/oop.inline.hpp"
#include "impl/GCObject.hpp"

namespace RTGC {

struct FieldUpdateLog {
  address   _anchor;
  int32_t   _offset;
  narrowOop _erased;

  void init(oopDesc* anchor, volatile narrowOop* field, narrowOop erased);

  void updateAnchorList();
};

class FieldUpdateReport {
  FieldUpdateReport* _next;
  FieldUpdateLog* _end;
  FieldUpdateLog* _sp;

  static FieldUpdateReport* g_report_q;
  static FieldUpdateReport  g_dummy_report;

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

  static FieldUpdateLog** default_stack_pointer() {
    return g_dummy_report.stack_pointer();
  }

  static void process_update_logs();

  static void reset_gc_context(bool promotion_finished);
};
};



