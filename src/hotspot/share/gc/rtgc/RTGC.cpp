#include "precompiled.hpp"

#include "oops/oop.inline.hpp"
#include "runtime/globals.hpp"

#include "gc/rtgc/RTGC.hpp"
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

void RTGC::add_referrer_unsafe(oopDesc* p, oopDesc* base, bool checkYoungRoot) {
  check_valid_obj(p, base);
  precond(p != NULL);
  assert(RTGC::heap_locked_bySelf() ||
         (SafepointSynchronize::is_at_safepoint() && Thread::current()->is_VM_thread()),
         "not locked");
  precond (p != base);// return;

  if (checkYoungRoot && !to_obj(p)->isTrackable() && !to_obj(base)->isYoungRoot()) {
    rtHeap::add_young_root(base, base);
  }

  if (!REF_LINK_ENABLED) return;
  rtgc_log(LOG_OPT(1), "add_referrer %p -> %p\n", base, p);
  GCRuntime::connectReferenceLink(to_obj(p), to_obj(base)); 
}

void RTGC::on_field_changed(oopDesc* base, oopDesc* oldValue, oopDesc* newValue, volatile void* addr, const char* fn) {
  check_valid_obj(newValue, base);
  check_valid_obj(oldValue, base);
  precond (oldValue != newValue);
  assert(!to_obj(base)->isGarbageMarked(), "incorrect anchor %p(%s) rc=%d\n", 
          base, RTGC::getClassName((GCObject*)base), to_obj(base)->getRootRefCount());
  assert(RTGC::heap_locked_bySelf() ||
         (SafepointSynchronize::is_at_safepoint() && Thread::current()->is_VM_thread()),
         "not locked");
  assert(to_obj(base)->isTrackable(), "not a anchor %p\n", base);
  rtgc_log(false && base->klass()->id() == InstanceRefKlassID &&
      (address)addr - (address)base == java_lang_ref_Reference::discovered_offset(),
      "discover changed %p.%p -> %p\n", base, oldValue, newValue);

  // rtgc_log(oldValue != NULL && oldValue->klass() == vmClasses::Module_klass(), 
  //     "Module unlinked %p -> %p\n", (void*)base, (void*)oldValue);

  rtgc_log(LOG_OPT(1), "field_changed(%s) %p[%d] : %p -> %p\n", 
    fn, base, (int)((address)addr - (address)base), oldValue, newValue);
  if (newValue != NULL && newValue != base) {
    add_referrer_unsafe(newValue, base, true);
  }
  if (!REF_LINK_ENABLED) return;
  if (oldValue != NULL && oldValue != base) {
    GCRuntime::disconnectReferenceLink(to_obj(oldValue), to_obj(base));
  }
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
  //rtgc_debug_log(newValue, "debug obj assigned! %p(%d)\n", newValue, RTGC::debugOptions[1]);
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

GCObject* RTGC::getForwardee(GCObject* obj, const char* tag) {
  assert(cast_to_oop(obj)->is_gc_marked(), "getForwardee(%s) on garbage %p(%s)\n", 
      tag, obj, RTGC::getClassName(obj));
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

void RTGC::collectGarbage(GCObject** nodes, int count) {
  GarbageProcessor::collectGarbage(nodes, count);
}

const char* RTGC::getClassName(GCNode* obj, bool showClassInfo) {
    Klass* klass = cast_to_oop(obj)->klass();
    if (vmClasses::Class_klass() == klass) {//} || vmClasses::Class_klass() == (void*)obj) {
      Klass* k2 = java_lang_Class::as_Klass(cast_to_oop(obj));
      if (k2 != NULL) {
        klass = k2;
      }
      if (showClassInfo) {
        printf("Class of class\n");
        cast_to_oop(obj)->print_on(tty);
      }
      // else {
      //   return klass->internal_name();
      // }
    }
    //return (const char*)klass->name()->bytes();
    return (const char*)klass->name()->bytes();
}


oop rtgc_break(const char* file, int line, const char* function) {
  printf("Error %s:%d %s", file, line, function);
  assert(false, "illegal barrier access");
  return NULL;
} 

void RTGC::adjust_debug_pointer(void* old_p, void* new_p) {
  if (is_debug_pointer(old_p)) {
    RTGC::debug_obj = new_p;
    rtgc_log(true, "debug_obj moved %p -> %p\n", old_p, new_p);
  } else if (RTGC::debug_obj == new_p) {
    // assert(!RTGC::debugOptions[0], "gotcha");
    rtgc_log(true, "object %p moved into debug_obj %p\n", old_p, new_p);
  }
}

static void* debugKlass = NULL;
bool RTGC::is_debug_pointer(void* ptr) {
  oopDesc* obj = (oopDesc*)ptr;
  if (obj == NULL) return false;

  // return obj->klass() == vmClasses::Object_klass();

  return ptr == debug_obj;

  if (debugKlass == NULL) {
    const char* className = "java/lang/invoke/ConstantCallSite";
    if (strstr((char*)obj->klass()->name()->bytes(), className) && obj->is_typeArray()) {
      debugKlass = obj->klass();
      return true;
    }
    return false;
  } else {
    return obj->klass() == debugKlass;
  }

  if (debugOptions[0]) return obj->klass() == Universe::byteArrayKlassObj();

  return obj->klass()->id() == InstanceRefKlassID && 
        ((InstanceKlass*)obj->klass())->reference_type() == REF_PHANTOM;


  // return cast_to_oop(obj)->klass() == vmClasses::String_klass() 
  //     && to_obj(obj)->getRootRefCount() == 1;

}


void RTGC::initialize() {
#ifdef _LP64
  is_narrow_oop_mode = UseCompressedOops;
#else
  is_narrow_oop_mode = false;
#endif

  RTGC::_rtgc.initialize();
  RTGC::debug_obj = (void*)0x7f04e0000;
  if (true) LogConfiguration::configure_stdout(LogLevel::Trace, true, LOG_TAGS(gc));

  REF_LINK_ENABLED |= UnlockExperimentalVMOptions;
  logOptions[0] = -1;
  debugOptions[0] = UnlockExperimentalVMOptions;
  //logOptions[LOG_HEAP] = 1 << 3;

  if (UnlockExperimentalVMOptions) {
    logOptions[LOG_HEAP] = 0;
    logOptions[LOG_REF_LINK] = 0;
    logOptions[LOG_BARRIER] = 0;
  }
}
