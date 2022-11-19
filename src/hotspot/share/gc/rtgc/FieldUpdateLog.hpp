#include "oops/oop.inline.hpp"

namespace RTGC {

struct FieldUpdateLog {
  oop       _anchor;
  int32_t   _offset;
  narrowOop _erased;

  void init(oopDesc* anchor, narrowOop* field, narrowOop erased) {
    this->_anchor = anchor;
    this->_offset = (address)field - (address)anchor;
    this->_erased = erased;
    postcond(_offset > 0);
  }
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

  FieldUpdateLog* first_log() { return _sp; }

  FieldUpdateLog* end_of_log() { return _end; }

  FieldUpdateReport* next() { return _next; }

  static FieldUpdateReport* allocate();

  static FieldUpdateLog** default_stack_pointer() {
    return g_dummy_report.stack_pointer();
  }

  static void reset_gc_context(bool promotion_finished);
};
};



