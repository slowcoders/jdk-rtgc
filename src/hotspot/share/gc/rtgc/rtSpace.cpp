#include "oops/typeArrayKlass.hpp"
#include "gc/rtgc/rtgcGlobals.hpp"
#include "gc/rtgc/rtSpace.hpp"
#include "gc/rtgc/impl/GCRuntime.hpp"

#include "rtCLDCleaner.hpp"

using namespace rtHeapUtil;
using namespace RTGC;

FreeMemStore g_freeMemStore;
static Klass* g_deadspace_klass;

static const int LOG_OPT(int function) {
  return RTGC::LOG_OPTION(RTGC::LOG_SPACE, function);
}

namespace RTGC {
  extern bool REF_LINK_ENABLED;
  extern bool is_gc_started;
  FreeNode* g_destroyed = NULL;
};

bool rtHeapUtil::is_dead_space(oopDesc* obj) {
  Klass* klass = obj->klass();
  return klass->is_typeArray_klass() || klass == vmClasses::Object_klass();
}

void rtHeapUtil::ensure_alive_or_deadsapce(oopDesc* old_p, oopDesc* anchor, oopDesc* new_anchor) {
  rt_assert_f(to_obj(old_p)->isAlive() || is_dead_space(old_p), 
        "anchor=%p -> %p(%s) invalid pointer " PTR_DBG_SIG, 
        anchor, new_anchor, RTGC::getClassName(anchor), PTR_DBG_INFO(old_p));
}


static size_t obj_size_in_word(oopDesc* obj) { 
  size_t size = obj->size_given_klass(obj->klass()); 
  return size;
}

void FreeMemStore::reclaimMemory(GCObject* garbage) {
  size_t word_size = obj_size_in_word(cast_to_oop(garbage));
  if (false) {
    CollectedHeap::fill_with_object((HeapWord*)garbage, word_size, false);
  }
  FreeMemQ* q = getFreeMemQ(word_size);
  FreeNode* free = reinterpret_cast<FreeNode*>(garbage);
  if (q->_top != NULL) {
    q->_top->_prev = free;
  }
  free->_next = q->_top;
  q->_top = free;
}

int FreeMemStore::getFreeMemQIndex(size_t heap_size_in_word) {
  int low = 0;
  int high = freeMemQList.size() - 1;
  FreeMemQ* q;
  while (low <= high) {
    int mid = (low + high) / 2;
    q = freeMemQList.adr_at(mid);
    int diff = q->_objSize - heap_size_in_word;
    if (diff == 0) {
      return mid;
    }
    if (diff < 0) {
      low = mid + 1;
    } else {
      high = mid - 1;
    }
  }
  return ~low;
}


FreeMemQ* FreeMemStore::getFreeMemQ(size_t heap_size_in_word) {
  FreeMemQ* q;
  int idx = getFreeMemQIndex(heap_size_in_word);
  if (idx >= 0) {
    q = freeMemQList.adr_at(idx);
  }
  else {
    q = freeMemQList.push_empty_at(~idx);
    q->_top = NULL;
    q->_objSize = heap_size_in_word;
#if 0 //def ASSERT
    rtgc_log(LOG_OPT(6), "----- add freeMemQ %d : %d\n", ~idx, (int)heap_size_in_word);
    for (int i = 0; i < freeMemQList.size(); i ++) {
      rtgc_log(LOG_OPT(6), "freeMemQ %d : %d\n", i, freeMemQList.at(i).objSize);
    }
#endif    
  }
  return q;  
}


void* FreeMemStore::recycle(size_t heap_size_in_word) {
  FreeMemQ* q;
  int idx = getFreeMemQIndex(heap_size_in_word);
  if (idx >= 0) {
    q = freeMemQList.adr_at(idx);
    FreeNode* free = q->_top;
    if (free != NULL) {
      FreeNode* next = free->_next;
      if (next == NULL) {
        // freeMemQList.removeAndShift(idx);
      } else {
        next->_prev = NULL;
      }
      q->_top = next;
      // *(uintptr_t*)free = 0xFFFF;
    }
    return free;
  }
  else {
    return NULL;
  }
};

void FreeMemStore::clearStore() {
  g_destroyed = NULL;
  g_freeMemStore.freeMemQList.resize(0);
}


void RuntimeHeap::reclaimObject(GCObject* obj) {
  rt_assert(!cast_to_oop(obj)->is_gc_marked());
  rt_assert(obj->isTrackable());
  rtCLDCleaner::unlock_cld(cast_to_oop(obj));
  
  obj->markDestroyed();
  if (!rtHeap::in_full_gc) {
    ((FreeNode*)obj)->_next = g_destroyed;
    g_destroyed = (FreeNode*)obj;
  }
  rt_assert(obj->isDestroyed());
}

void RuntimeHeap::reclaimSpace() {
  FreeNode* next;
  for (FreeNode* obj = g_destroyed; obj != NULL; obj = next) {
    next = obj->_next;
    g_freeMemStore.reclaimMemory((GCObject*)obj);
  }
  g_destroyed = NULL;
}

bool RuntimeHeap::is_broken_link(GCObject* anchor, GCObject* link) {
  return cast_to_oop(link) == 
      java_lang_ref_Reference::unknown_referent_no_keepalive(cast_to_oop(anchor));
}


namespace RTGC {
  extern address g_buffer_area_start;
  extern address g_buffer_area_end;
}
void rtHeap__addUntrackedTenuredObject(GCObject* node, bool is_recycled);

HeapWord* RtSpace::allocate(size_t word_size) {
  rt_assert_f(Heap_lock->owned_by_self() ||
         (SafepointSynchronize::is_at_safepoint() && Thread::current()->is_VM_thread()),
          "not locked");
#if 1 // 2
/* recycle 을 먼저 하는 경우에만 아래 오류가 발생하는 것으로 보인다.
#  Internal Error (../../src/hotspot/share/classfile/javaClasses.inline.hpp:121), pid=80849, tid=18947
#  assert(is_instance(java_string)) failed: must be java_string
#
# JRE version: OpenJDK Runtime Environment (17.0) (fastdebug build 17-internal+0-adhoc.zeedh.jdk-rtgc)
# Java VM: OpenJDK 64-Bit Client VM (fastdebug 17-internal+0-adhoc.zeedh.jdk-rtgc, 
           mixed mode, emulated-client, compressed oops, compressed class ptrs, serial gc, bsd-amd64)
V  [libjvm.dylib+0x2cebbd]  report_vm_error(char const*, int, char const*, char const*, ...)+0xdd
V  [libjvm.dylib+0x49f01e]  java_lang_String::length(oop)+0x9e
V  [libjvm.dylib+0xa4134e]  VerifyStrings::operator()(WeakHandle*)+0x8e
V  [libjvm.dylib+0xa4117c]  void ConcurrentHashTable<StringTableConfig, (MEMFLAGS)10>::do_scan_locked<VerifyStrings>(Thread*, VerifyStrings&)+0x11c
V  [libjvm.dylib+0xa3d418]  bool ConcurrentHashTable<StringTableConfig, (MEMFLAGS)10>::try_scan<VerifyStrings>(Thread*, VerifyStrings&)+0x58
V  [libjvm.dylib+0xa3d39b]  StringTable::verify()+0x5b
V  [libjvm.dylib+0xb1c461]  Universe::verify(VerifyOption, char const*)+0x2b1
V  [libjvm.dylib+0xb6d731]  VM_Exit::doit()+0x51

-----------------------------------------
java/lang/System/LoggerFinder/internal/LoggerFinderLoaderTest/LoggerFinderLoaderTest
#  SIGBUS (0xa) at pc=0x000000014acac348, pid=87876, tid=19459
V  [libjvm.dylib+0xa1ac81]  ContiguousSpace::block_size(HeapWordImpl* const*) const+0x451
V  [libjvm.dylib+0xd7ea3]  BlockOffsetArrayContigSpace::block_start_unsafe(void const*) const+0x3e3
V  [libjvm.dylib+0xd79b9]  BlockOffsetArray::verify() const+0x139
V  [libjvm.dylib+0xa1b67d]  OffsetTableContigSpace::verify() const+0x2d
V  [libjvm.dylib+0x42ff6e]  GenCollectedHeap::verify(VerifyOption)+0x3e
V  [libjvm.dylib+0xb1c40d]  Universe::verify(VerifyOption, char const*)+0x25d
V  [libjvm.dylib+0xb87b9d]  VMThread::run()+0x19d

----------------------------------------------
runtime/modules/ClassLoaderNoUnnamedModuleTest
recycle 연관성은 모호함.
#  Internal Error (../../src/hotspot/share/classfile/moduleEntry.cpp:275), pid=78046, tid=6147
#  guarantee(java_lang_Module::is_instance(module)) failed: The unnamed module for ClassLoader ClassLoaderNoUnnamedModule$TestClass, is null or not an instance of java.lang.Module. The class loader has not been initialized correctly.
V  [libjvm.dylib+0x7d2963]  ModuleEntry::create_unnamed_module(ClassLoaderData*)+0x193
V  [libjvm.dylib+0x25a6cd]  ClassLoaderData::ClassLoaderData(Handle, bool)+0x29d
V  [libjvm.dylib+0x2627b2]  ClassLoaderDataGraph::add_to_graph(Handle, bool)+0xc2
V  [libjvm.dylib+0x2629af]  ClassLoaderDataGraph::add(Handle, bool)+0x5f
V  [libjvm.dylib+0xa90e04]  ClassLoaderDataGraph::find_or_create(Handle)+0xf4
V  [libjvm.dylib+0xa91e31]  SystemDictionary::resolve_instance_class_or_null(Symbol*, Handle, Handle, JavaThread*)+0x261
V  [libjvm.dylib+0xa914b4]  SystemDictionary::resolve_or_fail(Symbol*, Handle, Handle, bool, JavaThread*)+0x64
V  [libjvm.dylib+0x5e4781]  find_class_from_class_loader(JNIEnv_*, Symbol*, unsigned char, Handle, Handle, unsigned char, JavaThread*)+0x31
V  [libjvm.dylib+0x5e4696]  JVM_FindClassFromCaller+0x496
*/
  HeapWord* heap = (HeapWord*)g_freeMemStore.recycle(word_size);
  bool recycled = (heap != NULL);
  if (recycled) {
    // _offsets.alloc_block(heap, word_size);
  }
  else {
    heap = _SUPER::allocate(word_size);
  }

#else
  HeapWord* heap = _SUPER::allocate(word_size);
  bool recycled = false;
  if (heap == NULL) {
    heap = (HeapWord*)g_freeMemStore.recycle(word_size);
    recycled = true;
    rtgc_debug_log(heap, "recycle garbage %ld %p\n", 
        word_size, heap);
  }
#endif
  if (heap != NULL) {
    //rt_assert_f(is_gc_started, "old alloc at runtime %p %ld\n", heap, word_size*8);
    rtgc_log(!is_gc_started, "old alloc at runtime %p %ld\n", heap, word_size*8);
    rt_assert(heap < (void*)RTGC::g_buffer_area_start || heap >= (void*)RTGC::g_buffer_area_end);
    // rt_assert_f(Universe::heap()->is_oop(cast_to_oop(heap)) && cast_to_oop(heap)->mark().value() != 0, 
    //     "top=%p bottom=%p recycled=%d mv=%p\n" PTR_DBG_SIG, 
    //     top(), bottom(), recycled, (void*)cast_to_oop(heap)->mark().value(), PTR_DBG_INFO(heap));
    rtHeap__addUntrackedTenuredObject(reinterpret_cast<GCObject*>(heap), recycled);
  }
  return heap;
}

HeapWord* RtSpace::par_allocate(size_t word_size) {
  HeapWord* heap = _SUPER::par_allocate(word_size);
  return heap;
}

void rtSpace__initialize() {
  // g_deadspace_klass = TypeArrayKlass::create_klass((BasicType)T_INT, Thread::current());
  // rt_assert(g_deadspace_klass != NULL);
  g_freeMemStore.initialize();
}