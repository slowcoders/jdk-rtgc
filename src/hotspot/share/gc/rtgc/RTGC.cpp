#include "precompiled.hpp"


#include "gc/rtgc/RTGC.hpp"
#include "gc/rtgc/rtgcDebug.hpp"

using namespace RTGC;

static const int LOG_REF_CHAIN(int function) {
  return LOG_OPTION(1, function);
}

static int g_mv_lock = 0;

namespace RTGC {
  static int debugOptions[256];
  volatile int* logOptions = debugOptions;
}

bool RTGC::isPublished(oopDesc* obj) {
  return true;
}

bool RTGC::lock_heap(oopDesc* obj) {
  if (!isPublished(obj)) return false;
  while (Atomic::cmpxchg(&g_mv_lock, 0, 1) != 0) { /* do spin. */ }
  return true;
}

void RTGC::unlock_heap(bool locked) {
  if (locked) {
    Atomic::release_store(&g_mv_lock, 0);
  }
}

void RTGC::add_referrer(oopDesc* obj, oopDesc* referrer) {
    rtgc_log(LOG_REF_CHAIN(1), "add_ref: obj=%p(%s), referrer=%p\n", 
      obj, obj->klass()->name()->bytes(), referrer); 
}

void RTGC::remove_referrer(oopDesc* obj, oopDesc* referrer) {
    rtgc_log(LOG_REF_CHAIN(1), "remove_ref: obj=%p(%s), referrer=%p\n",
      obj, obj->klass()->name()->bytes(), referrer); 
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

