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
  volatile void* debug_obj2 = (void*)-1;
  bool REF_LINK_ENABLED = true;
  bool is_narrow_oop_mode;

}

static void check_valid_obj(void* p, void* base) {
  GCObject* obj = (GCObject*)p;
  assert(obj == NULL || !obj->isGarbageMarked(), 
      "incorrect garbage mark %p(%s) anchor=%p(%s)\n", 
      obj, RTGC::getClassName(obj), base, RTGC::getClassName((GCObject*)base));
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
    //RTGC::scanInstanceGraph(obj, GCRuntime::markPublished);
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

void RTGC::add_referrer_unsafe(oopDesc* p, oopDesc* base) {
  check_valid_obj(p, base);
  assert(RTGC::heap_locked_bySelf() ||
         (SafepointSynchronize::is_at_safepoint() && Thread::current()->is_VM_thread()),
         "not locked");
  ptrdiff_t offset = (address)p - (address)base;

  // rtgc_log(p != NULL && p->klass() == vmClasses::Module_klass(), 
  //     "Module anchored %p -> %p\n", (void*)base, (void*)p);

  if (!REF_LINK_ENABLED) return;
  rtgc_log(LOG_OPT(1), "add_referrer %p -> %p\n", base, p);
  GCRuntime::connectReferenceLink(to_obj(p), to_obj(base)); 
}

void RTGC::on_field_changed(oopDesc* base, oopDesc* oldValue, oopDesc* newValue, volatile void* addr, const char* fn) {
  check_valid_obj(newValue, base);
  check_valid_obj(oldValue, base);
  assert(!to_obj(base)->isGarbageMarked(), 
      "incorrect anchor %p(%s)\n", base, RTGC::getClassName((GCObject*)base));
  assert(RTGC::heap_locked_bySelf() ||
         (SafepointSynchronize::is_at_safepoint() && Thread::current()->is_VM_thread()),
         "not locked");
  assert(to_obj(base)->isTrackable(), "not a anchor %p\n", base);
  precond(base->klass()->id() != InstanceRefKlassID
    || ((address)addr - (address)base != java_lang_ref_Reference::referent_offset() &&
        (address)addr - (address)base != java_lang_ref_Reference::discovered_offset()));

  if (oldValue == newValue) return;
  // rtgc_log(oldValue != NULL && oldValue->klass() == vmClasses::Module_klass(), 
  //     "Module unlinked %p -> %p\n", (void*)base, (void*)oldValue);

  rtgc_log(LOG_OPT(1), "field_changed(%s) %p[%d] : %p -> %p\n", 
    fn, base, (int)((address)addr - (address)base), oldValue, newValue);
  if (newValue != NULL) {
#if RTGC_OPT_YOUNG_ROOTS 
    if (!to_obj(newValue)->isTrackable() && !to_obj(base)->isYoungRoot()) {
      rtHeap::add_young_root(base, base);
    }
#endif  
    add_referrer_unsafe(newValue, base);
  }
  if (!REF_LINK_ENABLED) return;
  if (oldValue != NULL) GCRuntime::disconnectReferenceLink(to_obj(oldValue), to_obj(base));
}

void RTGC::on_root_changed(oopDesc* oldValue, oopDesc* newValue, volatile void* addr, const char* fn) {
  check_valid_obj(newValue, newValue);
  check_valid_obj(oldValue, newValue);

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

GCObject* RTGC::getForwardee(GCObject* obj) {
  precond(cast_to_oop(obj)->is_gc_marked());
  oopDesc* p = cast_to_oop(obj)->forwardee();
  return p == NULL ? obj : to_obj(p);
}

void RTGC::print_anchor_list(void* obj) {
  AnchorIterator it((GCObject*)obj);
  while (it.hasNext()) {
    GCObject* R = it.next();
    rtgc_log(true, "anchor obj(%p) -> %p(%s)\n", obj, R, RTGC::getClassName(R));
  }
}

bool RTGC::collectGarbage(oopDesc* obj) {
  GCObject* erased = to_obj(obj); 
  ResourceMark rm;
  if (erased->isUnsafe() && !erased->isGarbageMarked()) {
      GCRuntime::detectGarbages(erased);
  }
  // rtgc_log(erased->isGarbageMarked(), "garbage deteted %p[%d](%s)\n", obj, 
  //     vmClasses::Class_klass() == obj->klass(),  RTGC::getClassName(erased));
  return erased->isGarbageMarked();
}

const char* RTGC::getClassName(GCObject* obj, bool showClassInfo) {
    Klass* klass = cast_to_oop(obj)->klass();
    if (vmClasses::Class_klass() == klass || vmClasses::Class_klass() == (void*)obj) {
      if (showClassInfo) {
        printf("Class of class\n");
        cast_to_oop(obj)->print_on(tty);
      }
      // else {
      //   return klass->internal_name();
      // }
    }
    return klass->internal_name();
}


oop rtgc_break(const char* file, int line, const char* function) {
  printf("Error %s:%d %s", file, line, function);
  assert(false, "illegal barrier access");
  return NULL;
} 

void RTGC::initialize() {
#ifdef _LP64
  is_narrow_oop_mode = UseCompressedOops;
#else
  is_narrow_oop_mode = false;
#endif

  RTGC::_rtgc.initialize();
  RTGC::debug_obj = (void*)0x7f00444a8;
  if (true) LogConfiguration::configure_stdout(LogLevel::Trace, true, LOG_TAGS(gc));

  REF_LINK_ENABLED |= UnlockExperimentalVMOptions;
  logOptions[0] = -1;
  debugOptions[0] = true || UnlockExperimentalVMOptions;

  if (UnlockExperimentalVMOptions) {
    logOptions[LOG_HEAP] = 1 << 8;
    logOptions[LOG_REF_LINK] = 0;
    logOptions[LOG_BARRIER] = 0;
  }
}

