#include "precompiled.hpp"

#include "oops/oop.inline.hpp"
#include "memory/iterator.inline.hpp"
#include "oops/instanceKlass.inline.hpp"
#include "oops/fieldStreams.inline.hpp"
#include "runtime/globals.hpp"
#include "runtime/fieldDescriptor.inline.hpp"
#include "gc/serial/defNewGeneration.inline.hpp"
#include "gc/shared/memAllocator.hpp"

#include "gc/rtgc/rtgcGlobals.hpp"
#include "rtThreadLocalData.hpp"
#include "gc/rtgc/impl/GCObject.hpp"
#include "gc/rtgc/impl/GCRuntime.hpp"

using namespace RTGC;

static const int LOG_OPT(int function) {
  return RTGC::LOG_OPTION(RTGC::LOG_TLS, function);
}

namespace RTGC {
  class ThreadLocalDataClosure : public ThreadClosure {
  public:  
    virtual void do_thread(Thread* thread) {
      RtThreadLocalData::data(thread)->reset_field_update_log_sp();
    }
  };

  int g_cnt_update = 0;
  int g_cnt_update_log = 0;
  extern bool g_in_gc_termination;
  address g_report_area = 0;
  address g_last_report = 0;
  static const int STACK_CHUNK_SIZE = sizeof(FieldUpdateLog)*512;
  static const int MAX_LOG_IN_CHUNK = STACK_CHUNK_SIZE / sizeof(FieldUpdateLog) - 2;
}

FieldUpdateReport* FieldUpdateReport::g_report_q = NULL;
FieldUpdateReport  RtThreadLocalData::g_dummy_report;

RtThreadLocalData::RtThreadLocalData() { 
  _trackable_heap_start = GCNode::g_trackable_heap_start;
  precond(_trackable_heap_start != NULL);
  reset_field_update_log_sp(); 
  precond(_log_sp[0] <= (void*)_log_sp);
}

void FieldUpdateReport::reset_gc_context() {
  g_report_q = NULL;
  DefNewGeneration* newGen = (DefNewGeneration*)GenCollectedHeap::heap()->young_gen();
  ContiguousSpace* to = newGen->to();
  g_report_area = (address)to->bottom();
  g_last_report = (address)to->end();
  precond(g_report_area < g_last_report);
#ifdef ASSERT    
  rtgc_log(LOG_OPT(1), "heap old %p young=%p update=%d log=%d\n", 
    GenCollectedHeap::heap()->old_gen()->reserved().start(),
    newGen->from()->bottom(), g_cnt_update, g_cnt_update_log);
#endif    
  rtgc_log(LOG_OPT(1), "reset log chunk area %p size=%x", g_report_area, (int)(g_last_report - g_report_area));

  ThreadLocalDataClosure tld_closure;
  Threads::java_threads_do(&tld_closure);
}

FieldUpdateReport* FieldUpdateReport::allocate() {
  precond(!g_in_gc_termination);
  precond(rtHeapEx::OptStoreOop);

  FieldUpdateReport* report;
  if (g_last_report > g_report_area) {
#ifdef ASSERT    
    DefNewGeneration* newGen = (DefNewGeneration*)GenCollectedHeap::heap()->young_gen();
    ContiguousSpace* to = newGen->to();
    assert(g_report_area == (address)to->bottom(), "wrong chunk area %p : %p\n", g_report_area, to->bottom());
#endif

    report = (FieldUpdateReport*)Atomic::sub(&g_last_report, STACK_CHUNK_SIZE);
    if (report >= (void*)g_report_area) {
      report->_end = (FieldUpdateLog*)((uintptr_t)report + STACK_CHUNK_SIZE);
      report->_sp = report->_end;
      report->_next = Atomic::xchg(&g_report_q, report);
      rtgc_log(LOG_OPT(1), "report allocated %p to %p\n", report, report->_end);
      return (FieldUpdateReport*)report;
    } else {
      // all chunk allocated;
    }
  }

  fatal("allocation fail %p, %p\n", g_report_area, g_last_report);
  if (false) {
    int length = (STACK_CHUNK_SIZE - sizeof(arrayOopDesc)) / sizeof(jint);
    ObjArrayAllocator allocator(Universe::intArrayKlassObj(), 
        STACK_CHUNK_SIZE >> LogHeapWordSize, length, false);
    return (FieldUpdateReport*)(void*)allocator.allocate();
  }
  return NULL;
}

void RtThreadLocalData::addUpdateLog(oopDesc* anchor, volatile narrowOop* field, narrowOop erased, RtThreadLocalData* rtData) {

  FieldUpdateLog** log_sp = rtData->_log_sp;
  FieldUpdateLog* log = --log_sp[0];
  if (log <= (void*)log_sp) {
    precond(g_dummy_report.is_full());
    precond(g_dummy_report.next() == NULL);
    FieldUpdateReport* report = FieldUpdateReport::allocate();
    if (report != NULL) {
      rtData->_log_sp = log_sp = report->stack_pointer();
      log = --log_sp[0];
    } else if (log_sp != g_dummy_report.stack_pointer()) {
      FieldUpdateLog* last = log + MAX_LOG_IN_CHUNK;
      precond(((uintptr_t)(last+1) % STACK_CHUNK_SIZE) == 0);
      log_sp[0] = last;
      RTGC::lock_heap();
      while (true) {
        ++log;
        log->updateAnchorList();
        if (log == last) break;
      }
      RTGC::unlock_heap(true);
    } else {
      RTGC::lock_heap();
      oop assigned = CompressedOops::decode(*field);
      RTGC::on_field_changed(anchor, CompressedOops::decode(erased), assigned, NULL, NULL);
      RTGC::unlock_heap(true);
      return;
    }
  } 
  log->init(anchor, field, erased);
}

void FieldUpdateLog::add(oopDesc* anchor, volatile narrowOop* field, narrowOop erased) {
  RtThreadLocalData::addUpdateLog(anchor, field, erased, RtThreadLocalData::data(Thread::current()));
}


void FieldUpdateLog::init(oopDesc* anchor, volatile narrowOop* field, narrowOop erased) {
  rtgc_log(LOG_OPT(10), "add log(%p) [%p] %p\n", 
      anchor, field, (void*)CompressedOops::decode(erased));
  narrowOop new_p = *field;
  debug_only(Atomic::add(&g_cnt_update_log, 1);)

  precond(to_obj(anchor)->isTrackable());
  precond(!rtHeap::is_modified(erased));
  this->_anchor = (address)anchor;
  this->_offset = (address)field - (address)anchor;
  this->_erased = erased;
  assert(rtHeap::is_modified(new_p), "%p(%s) [%d] v=%x/n", 
      _anchor, RTGC::getClassName(_anchor), _offset, (int32_t)new_p);
  assert(_offset > 0, PTR_DBG_SIG, PTR_DBG_INFO(anchor));
}

void FieldUpdateLog::updateAnchorList() {
  narrowOop* pField = (narrowOop*)(_anchor + _offset);
  narrowOop new_p = *pField;
  rtgc_log(LOG_OPT(10), "updateAnchorList %p [%p] %p -> %p\n", 
      _anchor, pField, (void*)CompressedOops::decode(_erased), (void*)CompressedOops::decode(new_p));
  assert(rtHeap::is_modified(new_p), "%p(%s) [%d] v=%x/n", 
      _anchor, RTGC::getClassName(_anchor), _offset, (int32_t)new_p);
  precond(!rtHeap::is_modified(_erased));

  new_p = rtHeap::to_unmodified(new_p);
  *pField = new_p;
  if (rtHeapEx::OptStoreOop && new_p != _erased) {
    RTGC::on_field_changed((oopDesc*)_anchor, CompressedOops::decode(_erased), CompressedOops::decode(new_p), NULL, NULL);
  } else {
    // 여러번 변경되어 _erased 값이 동일해진 경우.
  }
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
    if (false) {
      int length = (STACK_CHUNK_SIZE - sizeof(arrayOopDesc)) / sizeof(jint);
      to_obj(report)->markGarbage(NULL);
      to_obj(report)->markDestroyed();
      CollectedHeap::fill_with_object((HeapWord*)report, STACK_CHUNK_SIZE >> LogHeapWordSize, false);
    }
  }
  g_report_q = NULL;
}