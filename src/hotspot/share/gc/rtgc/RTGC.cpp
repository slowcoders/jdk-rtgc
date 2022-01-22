#include "precompiled.hpp"

#include "gc/rtgc/RTGC.hpp"


using namespace RTGC;

static int g_mv_lock = 0;
static bool LOG_REF_CHAIN = false;

DebugOption debugOption;
volatile DebugOption* RTGC::debug = &debugOption;

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
    rtgc_log(LOG_REF_CHAIN, "add_ref: obj=%p(%s), referrer=%p\n", 
      obj, obj->klass()->name()->bytes(), referrer); 
}

void RTGC::remove_referrer(oopDesc* obj, oopDesc* referrer) {
    rtgc_log(LOG_REF_CHAIN, "remove_ref: obj=%p(%s), referrer=%p\n",
      obj, obj->klass()->name()->bytes(), referrer); 
}



oop rtgc_break(const char* file, int line, const char* function) {
  printf("Error %s:%d %s", file, line, function);
  assert(false, "illegal barrier access");
  return NULL;
} 

