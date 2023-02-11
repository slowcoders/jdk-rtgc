#include "oops/oop.inline.hpp"
#include "impl/GCObject.hpp"

namespace RTGC {

struct ErasedSlot {
  int32_t   _offset;
  narrowOop _obj;
};

class FieldUpdateLog {
public:
  address   _anchor;
  ErasedSlot _erased;

  void init(oopDesc* anchor, ErasedSlot erased);
  
  template <bool _atomic>
  void updateAnchorList();
  
  int32_t offset()    { return _erased._offset; }
  narrowOop erased()  { return _erased._obj; }
  narrowOop* field()  { return (narrowOop*)(_anchor + offset()); }
  static void add(oopDesc* anchor, volatile narrowOop* field, narrowOop erased);
};

class UpdateLogBuffer;

class UpdateLogBufferHeader {
protected:  
  FieldUpdateLog* _sp;
  UpdateLogBuffer* _next;

public:  
  UpdateLogBufferHeader() {
    _next = NULL; 
    _sp = (FieldUpdateLog*)this;
  }
};

class UpdateLogBuffer : public UpdateLogBufferHeader {
  static const int MAX_LOGS = (4*1024 - sizeof(UpdateLogBufferHeader)) / sizeof(FieldUpdateLog);
  FieldUpdateLog  _logs[MAX_LOGS];

public:

  static UpdateLogBuffer* volatile g_free_buffer_q;
  static UpdateLogBuffer* volatile g_active_buffer_q;
  static UpdateLogBuffer* volatile g_inactive_buffer_q;

  FieldUpdateLog* pop() { return --_sp; }

  FieldUpdateLog* peek() { return _sp; }

  bool is_full() { return _sp <= _logs; }

  FieldUpdateLog* first_log() { return is_full() ? _logs : _sp; }

  FieldUpdateLog* end_of_log() { return _logs + MAX_LOGS; }

  UpdateLogBuffer* next() { return _next; }

  template <bool _atomic>
  void flush_pending_logs();

  static UpdateLogBuffer* allocate();

  static void recycle(UpdateLogBuffer* retiree);

  static void process_update_logs();

  static void reset_gc_context();
};

class RtThreadLocalData {    
  UpdateLogBuffer* _log_buffer;
  RtThreadLocalData* _next;
  void* _trackable_heap_start;

  static UpdateLogBuffer* const g_dummy_buffer;
  static RtThreadLocalData* g_active_thread_q;

public:

  RtThreadLocalData();
  ~RtThreadLocalData();

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
    return Thread::gc_data_offset() + byte_offset_of(RtThreadLocalData, _log_buffer);
  }

  void reset_field_update_log_buffer() {
    _log_buffer = g_dummy_buffer;
  }

  static void addUpdateLog(oopDesc* anchor, ErasedSlot erasedField, RtThreadLocalData* rtData);

  static void reset_gc_context();

#ifdef ASSERT
  void checkLastLog(oopDesc* anchor, volatile narrowOop* field, narrowOop erased) {
    FieldUpdateLog* log = _log_buffer->peek();
    printf("log sp  %p start=%p\n", log, _log_buffer);
    rt_assert_f(log->_anchor == (void*)anchor, " %p %p\n", log->_anchor, anchor);
    rt_assert_f(log->offset() == (int32_t)(intptr_t)field, " %d %d\n", 
        log->offset(), (int32_t)(intptr_t)field);
    rt_assert_f(log->erased() == erased, " %x %x\n", (int32_t)log->erased(), (int32_t)erased);
  }
#endif
};

};



