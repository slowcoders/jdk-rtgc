#include "precompiled.hpp"

#include "oops/oop.inline.hpp"
#include "memory/iterator.inline.hpp"
#include "oops/instanceKlass.inline.hpp"
#include "oops/fieldStreams.inline.hpp"
#include "runtime/globals.hpp"
#include "runtime/fieldDescriptor.inline.hpp"
#include "gc/serial/defNewGeneration.inline.hpp"
#include "gc/shared/memAllocator.hpp"
#include "gc/rtgc/RTGC.hpp"
#include "rtThreadLocalData.hpp"
#include "gc/rtgc/impl/GCObject.hpp"

using namespace RTGC;

namespace RTGC {
  class ThreadLocalDataClosure : public ThreadClosure {
  public:  
    virtual void do_thread(Thread* thread) {
      RtThreadLocalData::data(thread)->reset_field_update_log_sp();
    }
  };

  extern bool g_in_gc_termination;
  address g_report_area = 0;
  address g_last_report = 0;
  static const int STACK_CHUNK_SIZE = sizeof(FieldUpdateLog)*512;
}

FieldUpdateReport* FieldUpdateReport::g_report_q = NULL;
FieldUpdateReport  FieldUpdateReport::g_dummy_report;

void FieldUpdateReport::reset_gc_context(bool init_shared_chunk_area) {
  g_report_q = NULL;
  if (init_shared_chunk_area) {
    DefNewGeneration* newGen = (DefNewGeneration*)GenCollectedHeap::heap()->young_gen();
    ContiguousSpace* to = newGen->to();
    g_report_area = (address)to->bottom();
    g_last_report = (address)to->end();
    precond(g_report_area < g_last_report);
  } else {
    g_report_area = 0;
    g_last_report = 0;
  }
  rtgc_log(true, "reset log chunk area %p size=%x", g_report_area, (int)(g_last_report - g_report_area));

  ThreadLocalDataClosure tld_closure;
  Threads::java_threads_do(&tld_closure);
}

static FieldUpdateReport* __allocate_log_stack_chunk() {
  address report;
  if (g_last_report > g_report_area) {
    DefNewGeneration* newGen = (DefNewGeneration*)GenCollectedHeap::heap()->young_gen();
    ContiguousSpace* to = newGen->to();
    assert(g_report_area == (address)to->bottom(), "wrong chunk area %p : %p\n", g_report_area, to->bottom());


    report = Atomic::sub(&g_last_report, STACK_CHUNK_SIZE);
    if (report >= g_report_area) {
      return (FieldUpdateReport*)report;
    } else {
      // all chunk allocated;
    }
  }

  fatal("allocation fail!");
  int length = (STACK_CHUNK_SIZE - sizeof(arrayOopDesc)) / sizeof(jint);
  ObjArrayAllocator allocator(Universe::intArrayKlassObj(), 
      STACK_CHUNK_SIZE >> LogHeapWordSize, length, false);
  return (FieldUpdateReport*)(void*)allocator.allocate();
}

FieldUpdateReport* FieldUpdateReport::allocate() {
  precond(!g_in_gc_termination);

  FieldUpdateReport* report = __allocate_log_stack_chunk();
  report->_end = (FieldUpdateLog*)((uintptr_t)report + STACK_CHUNK_SIZE);
  report->_sp = report->_end;
  report->_next = Atomic::xchg(&g_report_q, report);
  rtgc_log(true, "report allocated %p to %p\n", report, report->_end);

  return report;
}

void FieldUpdateLog::init(oopDesc* anchor, volatile narrowOop* field, narrowOop erased) {
  // rtgc_log(true, "add log(%p) %p[%p] v=%p\n", this, anchor, field, (void*)erased);
  precond(to_obj(anchor)->isTrackable());
  precond(!rtHeap::is_modified(erased));
  this->_anchor = (address)anchor;
  this->_offset = (address)field - (address)anchor;
  this->_erased = erased;
  assert(_offset > 0, PTR_DBG_SIG, PTR_DBG_INFO(anchor));
}

void FieldUpdateLog::updateAnchorList() {
  narrowOop* pField = (narrowOop*)(_anchor + _offset);
  narrowOop new_p = *pField;
  rtgc_debug_log(_anchor, "set_unmodified %p(%s)[%p] v=%p\n", 
      _anchor, RTGC::getClassName(_anchor), _anchor + _offset, (void*)new_p);
  precond(rtHeap::is_modified(new_p));
  precond(!rtHeap::is_modified(_erased));
  new_p = rtHeap::to_unmodified(new_p);
  if (new_p != _erased) {
    if (false) to_obj(_anchor)->replaceAnchor(*(ShortOOP*)&_erased, *(ShortOOP*)&new_p);
  } else {
    // 여러번 변경되어 _erased 값이 동일해진 경우.
  }
  *pField = new_p;
}


void FieldUpdateReport::process_update_logs() {
  Klass* intArrayKlass = Universe::intArrayKlassObj();
  FieldUpdateReport* next_report;
  for (FieldUpdateReport* report = g_report_q; report != NULL; report = next_report) {
    next_report = report->_next;
    FieldUpdateLog* end = report->end_of_log();
    for (FieldUpdateLog* log = report->first_log(); log < end; log ++) {
      log->updateAnchorList();
    }
    int length = (STACK_CHUNK_SIZE - sizeof(arrayOopDesc)) / sizeof(jint);
    to_obj(report)->markGarbage();
    to_obj(report)->markDestroyed();
    CollectedHeap::fill_with_object((HeapWord*)report, STACK_CHUNK_SIZE >> LogHeapWordSize, false);
  }
}