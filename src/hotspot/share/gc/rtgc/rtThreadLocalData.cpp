#include "precompiled.hpp"

#include "oops/oop.inline.hpp"
#include "memory/iterator.hpp"
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

bool rtHeapEx__useModifyFlag = true;

#ifdef ASSERT
bool rtHeap::useModifyFlag() {
  return rtHeapEx__useModifyFlag && EnableRTGC && UseCompressedOops;
}
#endif


namespace RTGC {
#if TRACE_UPDATE_LOG
  int g_field_update_cnt = 0;
  int g_inverse_graph_update_cnt = 0;
#endif
  int g_cnt_update = 0;
  int g_cnt_update_log = 0;
  address g_buffer_area_start = 0;
  address g_buffer_area_end = 0;
  narrowOop* g_erase_q;

  static const int STACK_CHUNK_SIZE = sizeof(FieldUpdateLog)*512;
  static const int MAX_LOG_IN_CHUNK = STACK_CHUNK_SIZE / sizeof(FieldUpdateLog) - 2;
  static UpdateLogBufferHeader g_dummy_buffer_header;
}

UpdateLogBuffer* volatile UpdateLogBuffer::g_free_buffer_q = NULL;
UpdateLogBuffer* volatile UpdateLogBuffer::g_active_buffer_q = NULL;
UpdateLogBuffer* volatile UpdateLogBuffer::g_inactive_buffer_q = NULL;
UpdateLogBuffer* const RtThreadLocalData::g_dummy_buffer = (UpdateLogBuffer*)&g_dummy_buffer_header;

RtThreadLocalData* RtThreadLocalData::g_active_thread_q = NULL;

template <bool _atomic>
void FieldUpdateLog::updateAnchorList() {
  if (_anchor == NULL) {
    rt_assert(RTGC::LAZY_REF_COUNT);
    narrowOop erased = _erased._obj;
    narrowOop assigned = *(narrowOop*)&_erased._offset;
    if (_atomic) {
      RTGC::on_root_changed(CompressedOops::decode(erased), CompressedOops::decode(assigned), NULL, NULL);
    } else {
      --g_erase_q;
      *g_erase_q = erased;
      RTGC::on_root_changed(NULL, CompressedOops::decode(assigned), NULL, NULL);
    }
    return;
  }

  rt_assert(to_obj(_anchor)->isTrackable());
  rt_assert_f(!rtHeap::is_modified(erased()), "%p(%s) [%d] v=%x/n", 
      _anchor, RTGC::getClassName(_anchor), offset(), (int32_t)erased());

  narrowOop* pField = field(); 
  narrowOop new_p;
  while (true) {
    narrowOop cmp_v = *pField;
    rt_assert_f(rtHeap::is_modified(cmp_v), "%p(%s) [%d] v=%x/n", 
        _anchor, RTGC::getClassName(_anchor), offset(), (int32_t)cmp_v);

    rt_assert(rtHeap::is_modified(cmp_v));
    new_p = rtHeap::to_unmodified(cmp_v);
    if (!_atomic) {
      *pField = new_p;
      break;
    } else {
      if (Atomic::cmpxchg(pField, cmp_v, new_p) == cmp_v) break;
    }
  }

#if TRACE_UPDATE_LOG
    Atomic::inc(&g_field_update_cnt);
#endif

  rtgc_debug_log(to_obj(CompressedOops::decode(erased())), 
      "updateAnchorList %p[%d yr=%d] = %p -> %p", 
      _anchor, offset(), to_obj(_anchor)->isYoungRoot(), (void*)CompressedOops::decode(erased()), (void*)CompressedOops::decode(new_p));

  if (rtHeap::useModifyFlag() && new_p != erased()) {
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
  if (anchor != NULL) {
    rt_assert_f(anchor, "add log(%p) (%p yr=%d) [%d] %p", 
        this, anchor, to_obj(anchor)->isYoungRoot(), erasedField._offset, (void*)CompressedOops::decode(erasedField._obj));
    rt_assert_f(anchor->size() * sizeof(HeapWord) > (uint64_t)erasedField._offset, "size %d offset %d", 
        anchor->size(), erasedField._offset);
    rt_assert(to_obj(anchor)->isTrackable());
    rt_assert(!rtHeap::is_modified(erasedField._obj));
  } else {
    // printf("RTGC add log(%p) (%p yr=%d) [%x] %p\n", 
    //     this, anchor, 0, erasedField._offset, (void*)CompressedOops::decode(erasedField._obj));
    narrowOop assigned = *(narrowOop*)&erasedField._offset;
    // printf("RTGC p %p\n", (void*)(intptr_t)((uint32_t)assigned << 2));
    rt_assert(CompressedOops::is_null(assigned) ||
        !to_obj((void*)CompressedOops::decode(assigned))->isGarbageTrackable());
  }

  debug_only(Atomic::add(&g_cnt_update_log, 1);)
  this->_anchor = (address)anchor;
  this->_erased = erasedField;

  // printf("RTGC er %p\n", (void*)this->erased());
  rt_assert(CompressedOops::is_null(this->erased()) ||
      !to_obj(CompressedOops::decode(this->erased()))->isGarbageTrackable());

  if (anchor != NULL) {
    rt_assert_f(rtHeap::is_modified(*field()), "%p(%s) [%d] v=%x/n", 
        _anchor, RTGC::getClassName(_anchor), erasedField._offset, 
        (int32_t)*field());
    rt_assert_f(erasedField._offset > 0, PTR_DBG_SIG, PTR_DBG_INFO(anchor));
  }
}


void RtThreadLocalData::reset_gc_context() {
  int cntThread = 0;
  for (RtThreadLocalData* rtData = g_active_thread_q; rtData != NULL; rtData = rtData->_next) {
    rtData->reset_field_update_log_buffer();
  }
}


void UpdateLogBuffer::reset_gc_context() {
  DefNewGeneration* newGen = (DefNewGeneration*)GenCollectedHeap::heap()->young_gen();
  ContiguousSpace* to = newGen->to();
  g_buffer_area_start = (address)to->bottom();
  g_buffer_area_end = (address)to->end();

#ifdef ASSERT    
  rt_assert(g_buffer_area_start < g_buffer_area_end);
  rtgc_log(LOG_OPT(1), "heap old %p eden=%p(%x) young-from=%p young-to=%p cnt_update=%d cnt_update_log=%d", 
    GenCollectedHeap::heap()->old_gen()->reserved().start(),
    newGen->eden()->bottom(), (int)newGen->eden()->capacity(),
    newGen->from()->bottom(), newGen->to()->bottom(), g_cnt_update, g_cnt_update_log);
  rtgc_log(LOG_OPT(1), "reset log chunk area %p size=%x", g_buffer_area_start, (int)(g_buffer_area_end - g_buffer_area_start));
#endif    

  g_active_buffer_q = NULL;
  g_inactive_buffer_q = NULL;
  g_free_buffer_q = (UpdateLogBuffer*)g_buffer_area_start;
  UpdateLogBuffer* buffer = g_free_buffer_q;
  for (; buffer + 1 <= (void*)g_buffer_area_end; buffer++) {
    buffer->_next = buffer + 1;
    buffer->_sp = buffer->end_of_log();
  }
  buffer[-1]._next = NULL;

  RtThreadLocalData::reset_gc_context();
}


UpdateLogBuffer* UpdateLogBuffer::allocate() {
  rt_assert(rtHeap::useModifyFlag());

  UpdateLogBuffer* buffer;
  
  rt_assert(! is_gc_started);
  RTGC::lock_heap();
  rt_assert(! is_gc_started);
  if ((buffer = g_inactive_buffer_q) != NULL) {
    rtgc_log(LOG_OPT(1), "recycle inactive LogBuffer %p s=%p e=%p", buffer, g_buffer_area_start, g_buffer_area_end);
    g_inactive_buffer_q = buffer->_next;
  } else if ((buffer = g_free_buffer_q) != NULL) {
    rtgc_log(LOG_OPT(10), "Allocate LogBuffer %p s=%p e=%p", g_free_buffer_q, g_buffer_area_start, g_buffer_area_end);
    g_free_buffer_q = buffer->_next;
  } 
  if (buffer != NULL) {
    buffer->_next = g_active_buffer_q;
    g_active_buffer_q = buffer;
  }
  RTGC::unlock_heap();
  return buffer;

  if (false) {
    int length = (STACK_CHUNK_SIZE - arrayOopDesc::header_size(T_INT) * HeapWordSize) / sizeof(jint);
    ObjArrayAllocator allocator(Universe::intArrayKlassObj(), 
        STACK_CHUNK_SIZE >> LogHeapWordSize, length, false);
    buffer = (UpdateLogBuffer*)(void*)allocator.allocate();
  }
  return buffer;
}


void UpdateLogBuffer::recycle(UpdateLogBuffer* buffer) {
  rtgc_log(LOG_OPT(1), "add inactive buffer %p", buffer);
  rt_assert(buffer > (void*)0x100);
// java/lang/invoke/MethodHandlesGeneralTest/hs_err_pid45346.log
// #  Internal Error (../../src/hotspot/share/gc/rtgc/rtThreadLocalData.cpp:195), pid=45346, tid=41731
// #  assert(! is_gc_started) failed: precond

  RTGC::lock_heap();
  UpdateLogBuffer* prev = g_active_buffer_q; 
  if (prev == NULL) {
    rt_assert(g_inactive_buffer_q == NULL);
    rt_assert(g_free_buffer_q == NULL);
  } else {
    if (prev == buffer) {
      g_active_buffer_q = buffer->_next;
    } else {
      while (prev->_next != buffer) {
        prev = prev->_next;
#ifdef ASSERT
        if (prev <= (void*)0x100) {
          int idx = 0;
          printf("fail to find %p (%p)\n", buffer, prev);
          printf("g_active_buffer_q\n");
          prev = g_active_buffer_q; 
          while (prev > (void*)0x100) {
            printf("q(%d) %p\n", idx++, prev);
            prev = prev->_next;
          }
          prev = g_inactive_buffer_q; 
          idx = 0;
          while (prev > (void*)0x100) {
            printf("q(%d) %p\n", idx++, prev);
            prev = prev->_next;
          }
          prev = g_free_buffer_q; 
          idx = 0;
          while (prev > (void*)0x100) {
            printf("q(%d) %p\n", idx++, prev);
            prev = prev->_next;
          }
        }
#endif
        rt_assert(prev > (void*)0x100);
      }
      prev->_next = buffer->_next;
    }
    buffer->_next = g_inactive_buffer_q;
    g_inactive_buffer_q = buffer;
  }
  RTGC::unlock_heap();
}


template <bool _atomic>
void UpdateLogBuffer::flush_pending_logs() {
  FieldUpdateLog* top = first_log();
  FieldUpdateLog* log = end_of_log();
  rtgc_log(LOG_OPT(10), "flush_pending_logs atomic=%d buffer:%p cnt:%ld", _atomic, this, log-top);
  for (; --log >= top; ) {
    log->updateAnchorList<_atomic>();
  }
  _sp = this->end_of_log();
}


void RtThreadLocalData::addUpdateLog(oopDesc* anchor, ErasedSlot erasedField, RtThreadLocalData* rtData) {
  rt_assert(rtData == RtThreadLocalData::data(Thread::current()));
  // rt_assert(!Thread::current()->is_VM_thread());

  UpdateLogBuffer* curr_buffer = rtData->_log_buffer;
  rt_assert_f(curr_buffer == g_dummy_buffer || 
        (curr_buffer >= (void*)g_buffer_area_start && curr_buffer < (void*)g_buffer_area_end),
      " curr_buffer = %p, buffer_start = %p, buffer_end = %p", curr_buffer, g_buffer_area_start, g_buffer_area_end);

  UpdateLogBuffer* volatile prev_inactive_q = UpdateLogBuffer::g_inactive_buffer_q;
  UpdateLogBuffer* volatile prev_free_q = UpdateLogBuffer::g_free_buffer_q;
  bool reuse_curr_buffer = false;
  bool buffer_full = false;

  if (curr_buffer->is_full()) {
    UpdateLogBuffer* new_buffer = UpdateLogBuffer::allocate();
    if (new_buffer != NULL) {
      curr_buffer = rtData->_log_buffer = new_buffer;
    } else if (curr_buffer != g_dummy_buffer) {
      reuse_curr_buffer = true;
      rtgc_log(true, "Reuse CurrBuffer %p[%d] v=%x", anchor, erasedField._offset, (int32_t)erasedField._obj);
      RTGC::lock_heap(true);
      curr_buffer->flush_pending_logs<true>();
      RTGC::unlock_heap();
    } else {
      buffer_full = true;
      rtgc_log(true, "LogBuffer full!! %p[%d] v=%x", anchor, erasedField._offset, (int32_t)erasedField._obj);
      FieldUpdateLog tmp;
      tmp.init(anchor, erasedField);
      RTGC::lock_heap();
      tmp.updateAnchorList<true>();
      RTGC::unlock_heap();
      return;
    }
  } 
  FieldUpdateLog* log = curr_buffer->pop();

#ifdef ASSERT
  {
    DefNewGeneration* newGen = (DefNewGeneration*)GenCollectedHeap::heap()->young_gen();
    ContiguousSpace* to = newGen->to();
    rt_assert(g_buffer_area_start == (address)to->bottom());
    rt_assert(g_buffer_area_end == (address)to->end());

    rt_assert_f((curr_buffer >= (void*)g_buffer_area_start && curr_buffer < (void*)g_buffer_area_end) && (log >= (void*)g_buffer_area_start && log < (void*)g_buffer_area_end),
        "curr_buffer = %p log = %p, buffer_start = %p, buffer_end = %p free_q = %p inactive_q = %p, reuse_curr_buffer=%d, buffer_full=%d", 
        curr_buffer, log, g_buffer_area_start, g_buffer_area_end, prev_free_q, prev_inactive_q, reuse_curr_buffer, buffer_full);
  }
#endif
  log->init(anchor, erasedField);
}


RtThreadLocalData::RtThreadLocalData() { 
  _trackable_heap_start = GCNode::g_trackable_heap_start;
  rt_assert(_trackable_heap_start != NULL);
  reset_field_update_log_buffer(); 
  rt_assert(_log_buffer->is_full());
  rt_assert(_log_buffer->next() == NULL);
  while (true) {
    this->_next = g_active_thread_q;
    if (this->_next == Atomic::cmpxchg(&g_active_thread_q, this->_next, this)) break;
  }
}

RtThreadLocalData::~RtThreadLocalData() {
  rt_assert(_log_buffer > (void*)0x100);
  rt_assert(!Thread::current()->is_VM_thread());
  {
    RTGC::lock_heap();
    RtThreadLocalData* rtData = Atomic::cmpxchg(&g_active_thread_q, this, this->_next);
    if (rtData != this) {
      for (; rtData->_next != this; ) {
        rtData = rtData->_next;
      }
      rtData->_next = this->_next;
    }
    RTGC::unlock_heap();
  }
  if (!_log_buffer->is_full()) {
    rt_assert(_log_buffer != g_dummy_buffer);
    rt_assert(!Thread::current()->is_VM_thread());
    UpdateLogBuffer::recycle(_log_buffer);
    _log_buffer = g_dummy_buffer;
  }
}


void UpdateLogBuffer::process_update_logs() {
  rtgc_log(LOG_OPT(1), "process_update_logs %p, %p, %p", 
      g_free_buffer_q, g_active_buffer_q, g_inactive_buffer_q);
  Klass* intArrayKlass = Universe::intArrayKlassObj();

  RTGC::lock_heap();
  UpdateLogBuffer* top_active_buffer = g_active_buffer_q;
  UpdateLogBuffer* top_inactive_buffer = g_inactive_buffer_q;
  UpdateLogBuffer* end_buffer = g_free_buffer_q; 
  g_free_buffer_q = NULL;
  g_active_buffer_q = NULL;
  g_inactive_buffer_q = NULL;
  RTGC::unlock_heap();
 
  g_erase_q = (narrowOop*)end_buffer;
  for (UpdateLogBuffer* buffer = end_buffer; --buffer >= (void*)g_buffer_area_start;) {
    buffer->flush_pending_logs<false>();
  }

  // for (UpdateLogBuffer* buffer = top_active_buffer; buffer != NULL; buffer = buffer->_next) {
  //   buffer->flush_pending_logs<false>();
  // }
  // for (UpdateLogBuffer* buffer = top_inactive_buffer; buffer != NULL; buffer = buffer->_next) {
  //   buffer->flush_pending_logs<false>();
  // }  
  if (RTGC::LAZY_REF_COUNT) {
    for (narrowOop* pErased = (narrowOop*)end_buffer; --pErased >= g_erase_q; ) {
      RTGC::on_root_changed(CompressedOops::decode(*pErased), NULL, NULL, NULL);   
    }
  }
}