#include "precompiled.hpp"

#include "oops/oop.inline.hpp"
#include "runtime/globals.hpp"

#include "gc/rtgc/RTGC.hpp"
#include "gc/rtgc/rtgcDebug.hpp"
#include "gc/rtgc/impl/GCRuntime.hpp"

using namespace RTGC;

const static bool ENABLE_REF_LINK = false;
// static const int LOG_OPT(int function) {
//   return LOG_OPTION(1, function);
// }

static int g_mv_lock = 0;

namespace RTGC {
  static int _logOptions[256];
  volatile int* logOptions = _logOptions;

  static DebugOptions _debugOptions;
  volatile DebugOptions* debugOptions = &_debugOptions;
}

bool RTGC::isPublished(GCObject* obj) {
  return obj->isPublished();
}

void RTGC::lock_heap() {
  while (Atomic::cmpxchg(&g_mv_lock, 0, 1) != 0) { /* do spin. */ }
}

bool RTGC::lock_if_published(GCObject* obj) {
  if (!isPublished(obj)) return false;
  lock_heap();
  return true;
}

void RTGC::publish_and_lock_heap(GCObject* obj, bool doPublish) {
  if (doPublish && obj != NULL && !isPublished(obj)) {
    if (RTGC::debugOptions->opt1)
    RTGC::scanInstance(obj, GCRuntime::markPublished);
  }
  lock_heap();
}

void RTGC::unlock_heap(bool locked) {
  if (locked) {
    Atomic::release_store(&g_mv_lock, 0);
  }
}

void RTGC::add_referrer_unsafe(oopDesc* obj, oopDesc* referrer) {
  if (ENABLE_REF_LINK && debugOptions->opt1) {
    //rtgc_log(true, "add_referrer (%p)->%p\n", obj, referrer);
    GCRuntime::connectReferenceLink(to_obj(obj), to_obj(referrer));
  }
}

void RTGC::add_referrer(oopDesc* obj, oopDesc* referrer) {
  precond(to_obj(referrer)->isReverseTrackable());
  add_referrer_unsafe(obj, referrer);
}

void RTGC::remove_referrer(oopDesc* obj, oopDesc* referrer) {
  precond(to_obj(referrer)->isReverseTrackable());
  if (ENABLE_REF_LINK && debugOptions->opt1) {
    //rtgc_log(true, "remove_referrer (%p)->%p\n", obj, referrer);
    GCRuntime::disconnectReferenceLink(to_obj(obj), to_obj(referrer));
  }
}

void* last_log = 0;
void RTGC::add_global_reference(oopDesc* obj) {
  if (ENABLE_REF_LINK && debugOptions->opt1) {
    // if (obj < (void*)0x7f00d8dFF && obj >=   (void*)0x7f00d8d00) {
    //   precond(last_log != obj);
    //   last_log = obj;
    // } 
    //rtgc_log(true, "add_global_ref %p\n", obj);
    precond(obj != NULL);  
    GCRuntime::onAssignRootVariable_internal(to_obj(obj));
  }
}

void RTGC::remove_global_reference(oopDesc* obj) {
  if (ENABLE_REF_LINK && debugOptions->opt1) {
    //rtgc_log(true, "remove_global_ref %p\n", obj);
    precond(obj != NULL);  
    GCRuntime::onEraseRootVariable_internal(to_obj(obj));
  }
}

void RTGC::initialize() {
  RTGC::_rtgc.initialize();
  debugOptions->opt1 = UnlockExperimentalVMOptions;
  logOptions[0] = -1;
  if (UnlockExperimentalVMOptions) {
    //logOptions[1] = -1;
  }
  rtgc_log(true, "UseTLAB=%d, ScavengeBeforeFullGC=%d\n", UseTLAB, ScavengeBeforeFullGC);
}


const char* RTGC::baseFileName(const char* filePath) {
  const char* name = strrchr(filePath, '/');
  return name ? name + 1: filePath;
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

