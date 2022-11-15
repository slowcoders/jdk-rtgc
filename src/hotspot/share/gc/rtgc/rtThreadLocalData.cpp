#include "precompiled.hpp"

#include "oops/oop.inline.hpp"
#include "memory/iterator.inline.hpp"
#include "oops/instanceKlass.inline.hpp"
#include "oops/fieldStreams.inline.hpp"
#include "runtime/globals.hpp"
#include "runtime/fieldDescriptor.inline.hpp"
#include "gc/serial/defNewGeneration.inline.hpp"
#include "gc/rtgc/RTGC.hpp"
#include "rtThreadLocalData.hpp"

using namespace RTGC;

class ThreadLocalDataClosure : public ThreadClosure {
public:  
  void* _update_log_stack;
  void* _update_log_top;
  ThreadLocalDataClosure() {
    DefNewGeneration* newGen = (DefNewGeneration*)GenCollectedHeap::heap()->young_gen();
    if (newGen != NULL) {
      ContiguousSpace* to = newGen->to();
      if (to != NULL) {
        _update_log_stack = to->top();
        _update_log_top = to->bottom();
      }
    }
  }
  virtual void do_thread(Thread* thread) {
    RtThreadLocalData* data = RtThreadLocalData::data(thread);
    data->_update_log_stack = _update_log_stack;
    data->_update_log_top = _update_log_top;
  }
};

RtThreadLocalData::RtThreadLocalData() {
  DefNewGeneration* newGen = (DefNewGeneration*)GenCollectedHeap::heap()->young_gen();
  if (newGen != NULL) {
    ContiguousSpace* to = newGen->to();
    if (to != NULL) {
      _update_log_stack = to->top();
      _update_log_top = to->bottom();
    }
  }
}

void RtThreadLocalData::reset_gc_context() {
  ThreadLocalDataClosure tld_closure;
  Threads::java_threads_do(&tld_closure);
}

