#include "precompiled.hpp"

#include "oops/oop.inline.hpp"
#include "runtime/globals.hpp"

#include "gc/rtgc/RTGC.hpp"
#include "gc/rtgc/rtgcDebug.hpp"
#include "gc/rtgc/rtgcHeap.hpp"
#include "gc/rtgc/impl/GCRuntime.hpp"
#include "logging/logConfiguration.hpp"

using namespace RTGC;

static const int LOG_OPT(int function) {
  return LOG_OPTION(RTGC::LOG_REF_LINK, function);
}

static int _logOptions[256];
static int _debugOptions[256];
namespace RTGC {

  Thread* g_mv_lock = 0;
  volatile int* logOptions = _logOptions;
  volatile int* debugOptions = _debugOptions;
  volatile void* debug_obj = (void*)-1;
  bool REF_LINK_ENABLED = false;
}
int GCNode::_cntTrackable = 0;

bool RTGC::isPublished(GCObject* obj) {
  return obj->isPublished();
}

void RTGC::lock_heap() {
#ifdef ASSERT
  Thread* self = Thread::current();
#else 
  Thread* self = (Thread*)1;
#endif
  while (Atomic::cmpxchg(&g_mv_lock, (Thread*)NULL, self) != 0) { /* do spin. */ }
}

bool RTGC::heap_locked_bySelf() {
#ifdef ASSERT
  Thread* self = Thread::current();
#else 
  Thread* self = (Thread*)1;
#endif
  return g_mv_lock == self;
}

bool RTGC::lock_if_published(GCObject* obj) {
  if (!isPublished(obj)) return false;
  lock_heap();
  return true;
}

void RTGC::publish_and_lock_heap(GCObject* obj, bool doPublish) {
  if (doPublish && obj != NULL && !isPublished(obj)) {
    //if (RTGC::debugOptions[0])
    //RTGC::scanInstance(obj, GCRuntime::markPublished);
  }
  lock_heap();
}

void RTGC::unlock_heap(bool locked) {
  if (locked) {
#ifdef ASSERT
    Thread* self = Thread::current();
    precond(g_mv_lock == self);
#endif

    Atomic::release_store(&g_mv_lock, (Thread*)NULL);
  }
}

bool RTGC::needTrack(oopDesc* obj) {
  return to_obj(obj)->isTrackable();
}

void RTGC::add_referrer_unsafe(oopDesc* p, oopDesc* referrer) {
  assert(RTGC::heap_locked_bySelf() ||
         (SafepointSynchronize::is_at_safepoint() && Thread::current()->is_VM_thread()),
         "not locked");
  GCObject* base = to_obj(referrer);
  precond(base->isTrackable());

  if (!REF_LINK_ENABLED) return;
  rtgc_log(LOG_OPT(1), "add_referrer %p -> %p\n", referrer, p);
  GCObject* obj = to_obj(p);
#if RTGC_OPT_YOUNG_ROOTS 
  if (!obj->isTrackable() && !base->isYoungRoot()) {
    RTGC::add_young_root(referrer);
  }
#endif  
  GCRuntime::connectReferenceLink(obj, base);
}

void RTGC::on_field_changed(oopDesc* base, oopDesc* oldValue, oopDesc* newValue, volatile void* addr, const char* fn) {
  assert(RTGC::heap_locked_bySelf() ||
         (SafepointSynchronize::is_at_safepoint() && Thread::current()->is_VM_thread()),
         "not locked");
  precond(to_obj(base)->isTrackable());

  if (oldValue == newValue) return;

  rtgc_log(LOG_OPT(1), "field_changed(%s) %p[%d] : %p -> %p\n", 
    fn, base, (int)((address)addr - (address)base), oldValue, newValue);
  if (newValue != NULL) add_referrer_unsafe(newValue, base);
  if (!REF_LINK_ENABLED) return;
  if (oldValue != NULL) GCRuntime::disconnectReferenceLink(to_obj(oldValue), to_obj(base));
}

void RTGC::on_root_changed(oopDesc* oldValue, oopDesc* newValue, volatile void* addr, const char* fn) {
  assert(RTGC::heap_locked_bySelf() ||
         (SafepointSynchronize::is_at_safepoint() && Thread::current()->is_VM_thread()),
         "not locked");
  rtgc_log(false && LOG_OPT(1), "root_changed(%s) *[%p] : %p -> %p\n", 
      fn, addr, oldValue, newValue);

  if (!REF_LINK_ENABLED) return;
  if (newValue != NULL) GCRuntime::onAssignRootVariable_internal(to_obj(newValue));
  if (oldValue != NULL) GCRuntime::onEraseRootVariable_internal(to_obj(oldValue));
}

const char* RTGC::baseFileName(const char* filePath) {
  const char* name = strrchr(filePath, '/');
  return name ? name + 1: filePath;
}

const void* RTGC::currentThreadId() {
  return Thread::current();
}

void RTGC::enableLog(int category, int functions) {
  logOptions[category] = functions;
}


bool RTGC::logEnabled(int logOption) {
  int category = logOption / LOG_CATEGORY_BASE;
  int function = logOption & LOG_FUNCTION_MASK;
  return logOptions[category] & function;
}

bool RTGC::collectGarbage(oopDesc* obj) {
  assert(0, "not impl");
  return false;
}

oop rtgc_break(const char* file, int line, const char* function) {
  printf("Error %s:%d %s", file, line, function);
  assert(false, "illegal barrier access");
  return NULL;
} 

void RTGC::initialize() {
  RTGC::_rtgc.initialize();
    // LogConfiguration::configure_stdout(LogLevel::Trace, true, LOG_TAGS(gc));

  // REF_LINK_ENABLED |= UnlockExperimentalVMOptions;
  logOptions[0] = -1;
  debugOptions[0] = UnlockExperimentalVMOptions;

//    logOptions[LOG_BARRIER] = 1 << 5;
  if (UnlockExperimentalVMOptions) {
    logOptions[LOG_HEAP] = 0;
    logOptions[LOG_REF_LINK] = 0;
    logOptions[LOG_BARRIER] = 1 << 5;
  }
}

