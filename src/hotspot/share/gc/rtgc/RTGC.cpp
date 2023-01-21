#include "oops/oop.inline.hpp"
#include "runtime/globals.hpp"

#include "gc/rtgc/rtgcGlobals.hpp"
#include "gc/rtgc/rtgcHeap.hpp"
#include "gc/rtgc/rtHeapEx.hpp"
#include "gc/rtgc/impl/GCRuntime.hpp"
#include "logging/logConfiguration.hpp"


using namespace RTGC;

static const int LOG_OPT(int function) {
  return LOG_OPTION(RTGC::LOG_REF_LINK, function);
}

static int _logOptions[256];
static int _debugOptions[256];

#ifdef ASSERT
#define LIGHT_LOCK        ((heap_lock_t)Thread::current())
#else
#define LIGHT_LOCK        ((heap_lock_t)1)
#endif
#define HEAVY_LOCK        -LIGHT_LOCK
#define UNLOCKED          ((heap_lock_t)0)
#define IS_HEAVY_LOCK(v)  (v < 0)

namespace RTGC {
  typedef intptr_t heap_lock_t;
  alignas(128) volatile heap_lock_t g_mv_lock = 0;
  int* logOptions = _logOptions;
  int* debugOptions = _debugOptions;
  void* debug_obj = NULL;
  void* debug_obj2 = NULL;
  bool REF_LINK_ENABLED = true;
  bool is_narrow_oop_mode;
}

static void check_valid_obj(void* p1, void* p2) {
  GCObject* obj1 = (GCObject*)p1;
  GCObject* obj2 = (GCObject*)p2;
  rt_assert_f(obj2 == NULL || !obj2->isGarbageMarked(),
      "incorrect garbage mark " PTR_DBG_SIG, PTR_DBG_INFO(obj2));
  rt_assert_f(obj1 == NULL || !obj1->isGarbageMarked(),
      "incorrect garbage mark " PTR_DBG_SIG, PTR_DBG_INFO(obj1));
}

int GCNode::_cntTrackable = 0;

bool RTGC::isPublished(GCObject* obj) {
  return obj->isPublished();
}

void RTGC::lock_heap(bool heavy) {
  heap_lock_t lock_v = heavy ? HEAVY_LOCK : LIGHT_LOCK;
  while (true) {
    heap_lock_t old_v = Atomic::cmpxchg(&g_mv_lock, UNLOCKED, lock_v);
    if (old_v == UNLOCKED) break;
    if (IS_HEAVY_LOCK(old_v)) {
      // do not waste spin lock
      // lock_semaphore
      // unlock_semaphore
    }
  }
  if (heavy) {
    // lock_semaphore
  }
}

void RTGC::promote_heavy_lock() {
  rt_assert(g_mv_lock == LIGHT_LOCK);
  g_mv_lock = HEAVY_LOCK;
  // lock_semaphore
}

void RTGC::unlock_heap() {
  rt_assert(heap_locked_bySelf());
  if (IS_HEAVY_LOCK(g_mv_lock)) {
    // unlock semaphore;
  }
  Atomic::release_store(&g_mv_lock, UNLOCKED);
}

bool RTGC::heap_locked_bySelf() {
  return g_mv_lock == LIGHT_LOCK || g_mv_lock == HEAVY_LOCK;
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


bool RTGC::needTrack(oopDesc* obj) {
  fatal("obsoleted!");
  return to_obj(obj)->isTrackable();
}

void RTGC::add_referrer_unsafe(oopDesc* p, oopDesc* base, oopDesc* debug_base) {
  rt_assert(p != NULL);
  check_valid_obj(p, debug_base);
  rt_assert_f(RTGC::heap_locked_bySelf() ||
         (SafepointSynchronize::is_at_safepoint() && Thread::current()->is_VM_thread()),
         "not locked");
  precond (p != debug_base);

  if (!REF_LINK_ENABLED) return;
#ifdef ASSERT    
  if (false && RTGC::is_debug_pointer(debug_base)) {
     rtgc_log(1, "referrer %p(rc=%d) added to %p(%s)\n", 
        base, to_obj(base)->getRootRefCount(), p, RTGC::getClassName(p));
  }
  if (RTGC::is_debug_pointer(p)) {
     rtgc_log(1, "referrer %p(%s) added to %p(rc=%d refs_=%x)\n", 
        debug_base, RTGC::getClassName(debug_base), p, 
        to_obj(p)->getRootRefCount(), to_obj(p)->node_()->getIdentityHashCode());
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
  rt_assert_f(!to_obj(base)->isGarbageMarked(), "incorrect anchor %p(%s) rc=%d\n", 
          base, RTGC::getClassName((GCObject*)base), to_obj(base)->getRootRefCount());
  rt_assert_f(RTGC::heap_locked_bySelf() ||
         (SafepointSynchronize::is_at_safepoint() && Thread::current()->is_VM_thread()),
         "not locked");
  rt_assert_f(to_obj(base)->isTrackable(), "not a anchor %p\n", base);

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

  rt_assert_f(RTGC::heap_locked_bySelf() ||
         (SafepointSynchronize::is_at_safepoint() && Thread::current()->is_VM_thread()),
         "not locked");

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

void RTGC::enableLog(int category, int function) {
  logOptions[category] |= (1 << function);
}


bool RTGC::logEnabled(int logOption) {
  int category = logOption / LOG_CATEGORY_BASE;
  int function = logOption & LOG_FUNCTION_MASK;
  return logOptions[category] & function;
}

GCObject* RTGC::getForwardee(GCObject* obj, const char* tag) {
  rt_assert_f(cast_to_oop(obj)->is_gc_marked(), "getForwardee(%s) on garbage %p(%s)\n", 
      tag, obj, RTGC::getClassName(obj));
  oopDesc* p = cast_to_oop(obj)->forwardee();
  return p == NULL ? obj : to_obj(p);
}


const char* RTGC::getClassName(const void* obj, bool showClassInfo) {
    if (obj == NULL || obj == (void*)-1) return NULL;
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
  rt_assert_f(false, "illegal barrier access");
  return NULL;
} 


const char* debugClassNames[] = {
  0, // reserved for -XX:AbortVMOnExceptionMessage=''
  // "sun/nio/fs/NativeBuffers$1", //  why this make java.nio.charset.MalformedInputException ???
  // "java/util/zip/ZipFile$ZipFileInflaterInputStream",
  // "invoke/MethodType$ConcurrentWeakInternSet$WeakEntry",
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
// void RTGC::clearDebugClasses() {
//   // for (int i = 0; i < CNT_DEBUG_CLASS; i ++) {
//   //   debugClassNames[i] = NULL;
//   //   debugKlass[i] = NULL;
//   // }
// }
int RTGC::is_debug_pointer(void* ptr) {
  oopDesc* obj = (oopDesc*)ptr;
  if (!RTGC_DEBUG || obj == NULL) return 0;

  if (ptr == debug_obj) return -1;

  if (ptr == debug_obj2) return -1;

  // if (((uintptr_t)ptr & ~0xFF00000) == 0x110029678) return true;

  Klass* klass = obj->klass();
  for (int i = 0; i < CNT_DEBUG_CLASS; i ++) {
    const char* className = debugClassNames[i];
    if (className == NULL) continue;
    if (className[0] == '&') {
      if (vmClasses::Class_klass() != klass) continue;
      className = className + 1;

      klass = java_lang_Class::as_Klass(cast_to_oop(obj));
      if (klass == NULL) continue;
    }

    if (debugKlass[i] == NULL) {
      if (strstr((char*)klass->name()->bytes(), className)
          && obj->klass()->name()->utf8_length() == (int)strlen(className)) {
        rtgc_log(1, "debug class resolved %s\n", klass->name()->bytes());
        debugKlass[i] = klass;
        return i+1;
      }
    } else if (klass == debugKlass[i]) {
      return i+1;
    }
  }
  return 0;
}


void RTGC::adjust_debug_pointer(void* old_p, void* new_p, bool destroy_old_node) {
  if (!RTGC_DEBUG) return;
  if (RTGC_FAT_OOP && destroy_old_node) {
    to_obj(old_p)->invalidateAnchorList_unsafe();
  }
  if (!REF_LINK_ENABLED) return;
  if (old_p == new_p) return;
  
  int type;
  if (RTGC::debug_obj == old_p || RTGC::debug_obj == new_p) {
    RTGC::debug_obj = new_p;
    rtgc_log(1, "debug_obj moved %p -> %p rc=%d\n", 
      old_p, new_p, to_obj(old_p)->getReferrerCount());
  }
  else if (RTGC::debug_obj2 == old_p || RTGC::debug_obj2 == new_p) {
    RTGC::debug_obj2 = new_p;
    rtgc_log(1, "debug_obj2 moved %p -> %p rc=%d\n", 
      old_p, new_p, to_obj(old_p)->getReferrerCount());
  } 
  else if ((type = is_debug_pointer(old_p)) != 0) {
    rtgc_log(1, "debug_obj(%d) moved %p -> %p rc=%d\n", 
      type, old_p, new_p, to_obj(old_p)->getReferrerCount());
  } 
  else if (false && cast_to_oop(old_p)->klass() == vmClasses::SoftReference_klass()) {
    rtgc_log(1, "debug_ref moved %p -> %p rc=%d\n", 
      old_p, new_p, to_obj(old_p)->getReferrerCount());
  }
}

#if ENABLE_RTGC_ASSERT
bool RTGC_DEBUG = false;
#endif
void rtHeap__initialize();
void rtSpace__initialize();


void RTGC::initialize() {
#ifdef _LP64
  is_narrow_oop_mode = UseCompressedOops;
#else
  is_narrow_oop_mode = false;
#endif

#if ENABLE_RTGC_ASSERT
  RTGC_DEBUG = AbortVMOnExceptionMessage != NULL && AbortVMOnExceptionMessage[0] == '#';
  // RTGC_DEBUG = 1;
  logOptions[0] = -1;
  // printf("init rtgc narrowOop=%d  %s\n", rtHeap::useModifyFlag(),  AbortVMOnExceptionMessage);
#endif

  ReferrerList::initialize();
  RTGC::_rtgc.initialize();
  rtHeap__initialize();
  rtSpace__initialize();
  rtHeapEx::initializeRefProcessor();

  REF_LINK_ENABLED |= UnlockExperimentalVMOptions;
  // LogConfiguration::configure_stdout(LogLevel::Trace, true, LOG_TAGS(gc));

  if (RTGC_DEBUG) {
    rtgc_log(1, "CDS base=%p end=%p\n", 
        MetaspaceObj::shared_metaspace_base(), MetaspaceObj::shared_metaspace_top());
    LogConfiguration::configure_stdout(LogLevel::Trace, true, LOG_TAGS(gc));

    // -XX:AbortVMOnExceptionMessage='#compiler/c2/Test7190310$1'
    ccstr s = AbortVMOnExceptionMessage;
    debugClassNames[0] = (s == NULL || s[1] == 0) ? NULL : s + 1;
    debugOptions[0] = 1;
    debug_obj = NULL;//0x3e0013510;

    rtgc_log(1, "debug_class '%s'\n", debugClassNames[0]);

    enableLog(LOG_REF_LINK, 0);
    enableLog(LOG_HEAP, 0);
    enableLog(LOG_REF, 0);
    enableLog(LOG_SCANNER, 0);
    enableLog(LOG_REF_LINK, 0);
    enableLog(LOG_BARRIER, 0);
    enableLog(LOG_SHORTCUT, 0);
    // enableLog(LOG_TLS, 1);
    // enableLog(LOG_TLS, 10);

    if (false) {
      rtgc_log(1, "lock_mask %p\n", (void*)markWord::lock_mask);
      rtgc_log(1, "lock_mask_in_place %p\n", (void*)markWord::lock_mask_in_place);
      rtgc_log(1, "biased_lock_mask %p\n", (void*)markWord::biased_lock_mask);
      rtgc_log(1, "biased_lock_mask_in_place %p\n", (void*)markWord::biased_lock_mask_in_place);
      rtgc_log(1, "biased_lock_bit_in_place %p\n", (void*)markWord::biased_lock_bit_in_place);
      rtgc_log(1, "age_mask %p\n", (void*)markWord::age_mask);
      rtgc_log(1, "age_mask_in_place %p\n", (void*)markWord::age_mask_in_place);
      rtgc_log(1, "epoch_mask %p\n", (void*)markWord::epoch_mask);
      rtgc_log(1, "epoch_mask_in_place %p\n", (void*)markWord::epoch_mask_in_place);

      rtgc_log(1, "hash_mask %p\n", (void*)markWord::hash_mask);
      rtgc_log(1, "hash_mask_in_place %p\n", (void*)markWord::hash_mask_in_place);

      rtgc_log(1, "unused_gap_bits %p\n", (void*)markWord::unused_gap_bits);
      rtgc_log(1, "unused_gap_bits_in_place %p\n", (void*)(markWord::unused_gap_bits << markWord::unused_gap_shift));
    }
    // unused_gap_bits
  }
}
