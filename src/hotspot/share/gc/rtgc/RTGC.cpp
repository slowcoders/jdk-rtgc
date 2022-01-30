#include "precompiled.hpp"

#include "oops/oop.inline.hpp"
#include "gc/rtgc/RTGC.hpp"
#include "gc/rtgc/rtgcDebug.hpp"
#include "gc/rtgc/impl/GCRuntime.hpp"

using namespace RTGC;

// static const int LOG_REF_CHAIN(int function) {
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
  return true;
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
    // obj->publishInstance();
  }
  lock_heap();
}

void RTGC::unlock_heap(bool locked) {
  if (locked) {
    Atomic::release_store(&g_mv_lock, 0);
  }
}

void RTGC::add_referrer(oopDesc* obj, oopDesc* referrer) {
  if (debugOptions->opt1) {
    rtgc_log(0, "add_referrer (%p)->%p\n", obj, referrer);
    GCRuntime::connectReferenceLink(to_obj(obj), to_obj(referrer));
  }
}

void RTGC::remove_referrer(oopDesc* obj, oopDesc* referrer) {
  if (debugOptions->opt1) {
    rtgc_log(0, "remove_referrer (%p)->%p\n", obj, referrer);
    GCRuntime::disconnectReferenceLink(to_obj(obj), to_obj(referrer));
  }
}

void RTGC::initialize() {
  RTGC::_rtgc.initialize();
}


const char* RTGC::baseFileName(const char* filePath) {
  const char* name = strrchr(filePath, '/');
  return name ? name + 1: filePath;
}

void RTGC::enableLog(int category, int functions) {
  logOptions[category] = functions;
}

bool RTGC::logEnabled(int logOption) {
  if (logOption == 0) return true;
  int category = LOG_CATEGORY(logOption);
  int function = LOG_FUNCTION(logOption);
  return logOptions[category] & function;
}

oop rtgc_break(const char* file, int line, const char* function) {
  printf("Error %s:%d %s", file, line, function);
  assert(false, "illegal barrier access");
  return NULL;
} 

