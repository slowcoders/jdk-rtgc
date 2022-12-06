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
      RtThreadLocalData::data(thread)->reset_field_update_log_buffer();
    }
  };

  int g_cnt_update = 0;
  int g_cnt_update_log = 0;
  extern bool g_in_gc_termination;
  address g_buffer_area_start = 0;
  address g_buffer_area_end = 0;

  static const int STACK_CHUNK_SIZE = sizeof(FieldUpdateLog)*512;
  static const int MAX_LOG_IN_CHUNK = STACK_CHUNK_SIZE / sizeof(FieldUpdateLog) - 2;
  static UpdateLogBufferHeader g_dummy_buffer_header;
}

UpdateLogBuffer* UpdateLogBuffer::g_free_buffer_q = NULL;
UpdateLogBuffer* UpdateLogBuffer::g_active_buffer_q = NULL;
UpdateLogBuffer* RtThreadLocalData::g_dummy_buffer = (UpdateLogBuffer*)&g_dummy_buffer_header;

template <bool _atomic>
void FieldUpdateLog::updateAnchorList() {
  // rtgc_log(_atomic, "updateAnchorList %p[%d] = %x\n", _anchor, offset(), (int32_t)erased());

  precond(to_obj(_anchor)->isTrackable());
  assert(!rtHeap::is_modified(erased()), "%p(%s) [%d] v=%x/n", 
      _anchor, RTGC::getClassName(_anchor), offset(), (int32_t)erased());

  narrowOop* pField = field(); 
  narrowOop new_p;
  while (true) {
    narrowOop cmp_v = *pField;
    assert(rtHeap::is_modified(cmp_v), "%p(%s) [%d] v=%x/n", 
        _anchor, RTGC::getClassName(_anchor), offset(), (int32_t)cmp_v);

    precond(rtHeap::is_modified(cmp_v));
    new_p = rtHeap::to_unmodified(cmp_v);
    if (!_atomic) {
      *pField = new_p;
      break;
    } else {
      if (Atomic::cmpxchg(pField, cmp_v, new_p) == cmp_v) break;
    }
  }

  rtgc_log(LOG_OPT(10), "updateAnchorList %p[%d] = %p -> %p\n", 
      _anchor, offset(), (void*)CompressedOops::decode(erased()), (void*)CompressedOops::decode(new_p));

  if (rtHeapEx::OptStoreOop && new_p != erased()) {
    RTGC::on_field_changed((oopDesc*)_anchor, CompressedOops::decode(erased()), CompressedOops::decode(new_p), NULL, NULL);
  }
}


void FieldUpdateLog::add(oopDesc* anchor, volatile narrowOop* field, narrowOop erased) {
  ErasedSlot erasedField;
  erasedField._offset = (address)field - (address)anchor;
  erasedField._obj = erased;
  RtThreadLocalData::addUpdateLog(anchor, erasedField, RtThreadLocalData::data(Thread::current()));
}


void FieldUpdateLog::init(oopDesc* anchor, ErasedSlot erasedField) {
  rtgc_debug_log(anchor, "add log(%p) [%d] %p\n", 
      anchor, erasedField._offset, (void*)CompressedOops::decode(erasedField._obj));
  assert(anchor->size() * sizeof(HeapWord) > (uint64_t)erasedField._offset, "size %d offset %d\n", 
      anchor->size(), erasedField._offset);

  debug_only(Atomic::add(&g_cnt_update_log, 1);)

  precond(to_obj(anchor)->isTrackable());
  precond(!rtHeap::is_modified(erasedField._obj));
  this->_anchor = (address)anchor;
  this->_erased = erasedField;
  assert(rtHeap::is_modified(*field()), "%p(%s) [%d] v=%x/n", 
      _anchor, RTGC::getClassName(_anchor), erasedField._offset, 
      (int32_t)*field());
  assert(erasedField._offset > 0, PTR_DBG_SIG, PTR_DBG_INFO(anchor));
}



void UpdateLogBuffer::reset_gc_context() {

  DefNewGeneration* newGen = (DefNewGeneration*)GenCollectedHeap::heap()->young_gen();
  ContiguousSpace* to = newGen->to();
  g_buffer_area_start = (address)to->bottom();
  g_buffer_area_end = (address)to->end();

#ifdef ASSERT    
  precond(g_buffer_area_start < g_buffer_area_end);
  rtgc_log(LOG_OPT(1), "heap old %p young=%p update=%d log=%d\n", 
    GenCollectedHeap::heap()->old_gen()->reserved().start(),
    newGen->from()->bottom(), g_cnt_update, g_cnt_update_log);
  rtgc_log(LOG_OPT(1), "reset log chunk area %p size=%x", g_buffer_area_start, (int)(g_buffer_area_end - g_buffer_area_start));
#endif    

  g_active_buffer_q = NULL;
  g_free_buffer_q = (UpdateLogBuffer*)g_buffer_area_start;
  UpdateLogBuffer* buffer = g_free_buffer_q;
  for (; buffer + 1 <= (void*)g_buffer_area_end; buffer++) {
    buffer->_next = buffer + 1;
    buffer->_sp = buffer->end_of_log();
  }
  buffer[-1]._next = NULL;

  ThreadLocalDataClosure tld_closure;
  Threads::java_threads_do(&tld_closure);
}

UpdateLogBuffer* UpdateLogBuffer::allocate() {
  precond(!g_in_gc_termination);
  precond(rtHeapEx::OptStoreOop);

  RTGC::lock_heap();
  UpdateLogBuffer* buffer = g_free_buffer_q;
  if (buffer != NULL) {
    g_free_buffer_q = buffer->_next;
    buffer->_next = g_active_buffer_q;
    g_active_buffer_q = buffer;
  }
  RTGC::unlock_heap(true);

  if (false) {
    int length = (STACK_CHUNK_SIZE - sizeof(arrayOopDesc)) / sizeof(jint);
    ObjArrayAllocator allocator(Universe::intArrayKlassObj(), 
        STACK_CHUNK_SIZE >> LogHeapWordSize, length, false);
    buffer = (UpdateLogBuffer*)(void*)allocator.allocate();
  }
  return buffer;
}

void UpdateLogBuffer::recycle(UpdateLogBuffer* buffer) {
  precond(RTGC::heap_locked_bySelf());
  rtgc_log(true, "UpdateLogBuffer::recycle %p\n", buffer);
  UpdateLogBuffer* prev = g_active_buffer_q;
  if (prev != buffer) {
    while (prev->_next != buffer) {
      prev = prev->_next;
      postcond(prev != NULL);
    }
    prev->_next = buffer->_next;
  } else {
    g_active_buffer_q = buffer->_next;
  }

  buffer->_next = g_free_buffer_q;
  g_free_buffer_q = buffer;
}

template <bool _atomic>
void UpdateLogBuffer::flushPendingLogs() {
  FieldUpdateLog* log = first_log();
  FieldUpdateLog* end = end_of_log();
  rtgc_log(_atomic, "flushPendingLogs %p -> %p\n", log, end);
  for (; log < end; log++) {
    log->updateAnchorList<_atomic>();
  }
  _sp = this->end_of_log();
}

void RtThreadLocalData::addUpdateLog(oopDesc* anchor, ErasedSlot erasedField, RtThreadLocalData* rtData) {

  UpdateLogBuffer* curr_buffer = rtData->_log_buffer;
  FieldUpdateLog* log = curr_buffer->pop();

  if (log <= (void*)curr_buffer) {
    UpdateLogBuffer* new_buffer = UpdateLogBuffer::allocate();
    if (new_buffer != NULL) {
      rtData->_log_buffer = curr_buffer = new_buffer;
    } else if (curr_buffer != g_dummy_buffer) {
      rtgc_log(true, "Reusing LogBuffer %p[%d] v=%x\n", anchor, erasedField._offset, erasedField._obj);
      RTGC::lock_heap();
      curr_buffer->flushPendingLogs<true>();
      RTGC::unlock_heap(true);
    } else {
      rtgc_log(true, "LogBuffer full!! %p[%d] v=%x\n", anchor, erasedField._offset, erasedField._obj);
      FieldUpdateLog tmp;
      tmp.init(anchor, erasedField);
      RTGC::lock_heap();
      tmp.updateAnchorList<true>();
      RTGC::unlock_heap(true);
      return;
    }
    log = curr_buffer->pop();
  } 
  log->init(anchor, erasedField);
}


RtThreadLocalData::RtThreadLocalData() { 
  _trackable_heap_start = GCNode::g_trackable_heap_start;
  precond(_trackable_heap_start != NULL);
  reset_field_update_log_buffer(); 
  precond(_log_buffer->is_full());
  precond(_log_buffer->next() == NULL);
}

RtThreadLocalData::~RtThreadLocalData() {
  if (_log_buffer != g_dummy_buffer) {
    RTGC::lock_heap();
    _log_buffer->flushPendingLogs<true>();
    UpdateLogBuffer::recycle(_log_buffer);
    RTGC::unlock_heap(true);
  }
}


void UpdateLogBuffer::process_update_logs() {
  Klass* intArrayKlass = Universe::intArrayKlassObj();
  for (UpdateLogBuffer* buffer = g_active_buffer_q; buffer != NULL; buffer = buffer->_next) {
    buffer->flushPendingLogs<false>();
    if (false) {
      int length = (STACK_CHUNK_SIZE - sizeof(arrayOopDesc)) / sizeof(jint);
      to_obj(buffer)->markGarbage(NULL);
      to_obj(buffer)->markDestroyed();
      CollectedHeap::fill_with_object((HeapWord*)buffer, STACK_CHUNK_SIZE >> LogHeapWordSize, false);
    }
  }
  g_free_buffer_q = NULL;
  g_active_buffer_q = NULL;
}