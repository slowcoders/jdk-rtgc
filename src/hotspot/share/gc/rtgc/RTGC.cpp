#include "precompiled.hpp"

#include "oops/oop.inline.hpp"
#include "runtime/globals.hpp"

#include "gc/rtgc/RTGC.hpp"
#include "gc/rtgc/rtgcHeap.hpp"
#include "gc/rtgc/rtRefProcessor.hpp"
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

static void check_valid_obj(void* p1, void* p2) {
  GCObject* obj1 = (GCObject*)p1;
  GCObject* obj2 = (GCObject*)p2;
  assert((obj2 == NULL || !obj2->isGarbageMarked()) && (obj1 == NULL || !obj1->isGarbageMarked()), 
      "incorrect garbage mark %p(g=%d:%d) anchor=%p(g=%d:%d)\n", 
      obj1, obj1 == NULL ? 0 : obj1->isGarbageMarked(), obj1 == NULL ? 0 : obj1->getRootRefCount(),   
      obj2, obj2 == NULL ? 0 : obj2->isGarbageMarked(), obj2 == NULL ? 0 : obj2->getRootRefCount());
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

void RTGC::add_referrer_unsafe(oopDesc* p, oopDesc* base, oopDesc* debug_base) {
  precond(p != NULL);
  check_valid_obj(p, debug_base);
  assert(RTGC::heap_locked_bySelf() ||
         (SafepointSynchronize::is_at_safepoint() && Thread::current()->is_VM_thread()),
         "not locked");
  precond (p != base && p != debug_base);// return;

  if (!REF_LINK_ENABLED) return;
#ifdef ASSERT    
  if (RTGC::is_debug_pointer(p) || RTGC::is_debug_pointer(debug_base)) {
     rtgc_log(1, "referrer %p added to %p\n", base, p);
  }
#endif
  GCRuntime::connectReferenceLink(to_obj(p), to_obj(base)); 
}


void RTGC::add_referrer_ex(oopDesc* p, oopDesc* base, bool checkYoungRoot) {
  add_referrer_unsafe(p, base, base);
  if (checkYoungRoot && !to_obj(p)->isTrackable() && !to_obj(base)->isYoungRoot()) {
    rtHeap::add_young_root(base, base);
  }
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
    add_referrer_ex(newValue, base, true);
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

void RTGC::enableLog(int category, int function) {
  logOptions[category] |= (1 << function);
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
    rtgc_log(1, "anchor obj(%p) -> %p(%s)\n", obj, R, RTGC::getClassName(R));
  }
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


const char* debugClassNames[] = {
  0, // reserved for -XX:AbortVMOnExceptionMessage=''
  //  "compiler/c2/Test7190310$1",
  // "jdk/internal/ref/CleanerImpl$PhantomCleanableRef",
    // "java/lang/ref/Finalizer",
    // "jdk/nio/zipfs/ZipFileSystem",
  //  "java/lang/invoke/LambdaFormEditor$Transform",
    // "java/lang/invoke/MethodTypeForm",
    // "[Ljava/util/concurrent/ConcurrentHashMap$Node;",
    // "java/lang/invoke/MethodType",
    // "java/lang/invoke/MethodType$ConcurrentWeakInternSet$WeakEntry",
    // "java/lang/ref/SoftReference",
    // "java/util/HashMap$Node", 
    // "jdk/internal/ref/CleanerImpl$PhantomCleanableRef",
    // "jdk/nio/zipfs/ZipFileSystem",
    // "java/lang/ref/Finalizer"
};

const int CNT_DEBUG_CLASS = sizeof(debugClassNames) / sizeof(debugClassNames[0]);
void* debugKlass[CNT_DEBUG_CLASS];
void* dbgObjs[16];
int cntDbgObj = 0;
bool RTGC::is_debug_pointer(void* ptr) {
  oopDesc* obj = (oopDesc*)ptr;
  if (obj == NULL) return false;

  if (ptr == debug_obj) return true;

  // if (ptr < (void*)0x203990310) return true;
  // if (!UnlockExperimentalVMOptions || !to_obj(ptr)->isActiveFinalizerReachable()) return false;

  for (int i = 0; i < CNT_DEBUG_CLASS; i ++) {
    Klass* klass = obj->klass();
    if (false) {
      if (vmClasses::Class_klass() != klass) return false;
      klass = java_lang_Class::as_Klass(cast_to_oop(obj));
      if (klass == NULL) return false;
    }
    if (debugKlass[i] == NULL) {
      const char* className = debugClassNames[i];
      if (className != NULL && strstr((char*)klass->name()->bytes(), className)
          && obj->klass()->name()->utf8_length() == (int)strlen(className)) {
        rtgc_log(1, "debug class resolved %s\n", klass->name()->bytes());
        debugKlass[i] = klass;
        return true;
      }
    } else if (klass == debugKlass[i]) {
      return true;
    }
  }
  return false;
}

void RTGC::adjust_debug_pointer(void* old_p, void* new_p, bool destroy_old_node) {
  if (destroy_old_node) {
    to_node(old_p)->invalidateAnchorList_unsafe();
  }
  if (!REF_LINK_ENABLED) return;
  if (old_p == new_p) return;
  
  if (RTGC::debug_obj == old_p) {
    RTGC::debug_obj = new_p;
    rtgc_log(1, "debug_obj moved %p -> %p\n", old_p, new_p);
  }
  else if (is_debug_pointer(old_p)) {
    rtgc_log(1, "debug_obj moved %p -> %p\n", old_p, new_p);
  } 
}


void RTGC::initialize() {
#ifdef _LP64
  is_narrow_oop_mode = UseCompressedOops;
#else
  is_narrow_oop_mode = false;
#endif

  RTGC::_rtgc.initialize();
  RTGC::debug_obj = (void*)-1;
  RTGC::debug_obj2 = NULL;
  rtHeapEx::initializeRefProcessor();
  if (UnlockExperimentalVMOptions) LogConfiguration::configure_stdout(LogLevel::Trace, true, LOG_TAGS(gc));
  ScavengeBeforeFullGC = true;

  REF_LINK_ENABLED |= UnlockExperimentalVMOptions;
  logOptions[0] = -1;
  debugOptions[0] = UnlockExperimentalVMOptions;
  debugClassNames[0] = AbortVMOnExceptionMessage;

  if (UnlockExperimentalVMOptions) {
    // debugClassNames[0] = "java/util/HashMap$Node";
    enableLog(LOG_SCANNER, 0);
    enableLog(LOG_REF_LINK, 0);
    enableLog(LOG_BARRIER, 0);
  }
}
