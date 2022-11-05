#include "gc/rtgc/RTGC.hpp"
#include "gc/rtgc/rtSpace.hpp"
#include "gc/rtgc/rtgcDebug.hpp"
#include "gc/rtgc/impl/GCRuntime.hpp"

#include "rtCLDCleaner.hpp"

using namespace rtHeapUtil;
using namespace RTGC;
FreeMemStore g_freeMemStore;

static const int LOG_OPT(int function) {
  return RTGC::LOG_OPTION(RTGC::LOG_HEAP, function);
}

bool rtHeapUtil::is_dead_space(oopDesc* obj) {
  Klass* klass = obj->klass();
  return klass->is_typeArray_klass() || klass == vmClasses::Object_klass();
}

void rtHeapUtil::ensure_alive_or_deadsapce(oopDesc* old_p, oopDesc* anchor) {
  assert(!to_obj(old_p)->isGarbageMarked() || is_dead_space(old_p), 
        "invalid pointer %p(%s) isClass=%d isTr=%d anchor=%p(%s)\n", 
        old_p, RTGC::getClassName(to_obj(old_p)), old_p->klass() == vmClasses::Class_klass(),
        to_obj(old_p)->isTrackable(), anchor, anchor==NULL?"":RTGC::getClassName(to_obj(anchor)));
}


static size_t obj_size_in_word(oopDesc* obj) { 
  size_t size = obj->size_given_klass(obj->klass()); 
  return size;
}

void FreeMemStore::reclaimMemory(GCObject* garbage) {
  size_t word_size = obj_size_in_word(cast_to_oop(garbage));
  FreeMemQ* q = getFreeMemQ(word_size);
  FreeNode* free = reinterpret_cast<FreeNode*>(garbage);
  if (q->top != NULL) {
    q->top->prev = free;
  }
  free->next = q->top;
  q->top = free;
  if (word_size == 10) {
    rtgc_log(LOG_OPT(6), "add free node %ld %p\n", 
        word_size, free);
  }
}

int FreeMemStore::getFreeMemQIndex(size_t heap_size_in_word) {
  int low = 0;
  int high = freeMemQList.size() - 1;
  FreeMemQ* q;
  while (low <= high) {
    int mid = (low + high) / 2;
    q = freeMemQList.adr_at(mid);
    int diff = q->objSize - heap_size_in_word;
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
    q->top = NULL;
    q->objSize = heap_size_in_word;
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
    FreeNode* free = q->top;
    if (free != NULL) {
      FreeNode* next = free->next;
      if (next == NULL) {
        // freeMemQList.removeAndShift(idx);
      } else {
        next->prev = NULL;
      }
      q->top = next;
    }
    return free;
  }
  else {
    return NULL;
  }
};

void FreeMemStore::clearStore() {
  g_freeMemStore.freeMemQList.resize(0);
}

void RuntimeHeap::reclaimObject(GCObject* obj) {
  precond(!cast_to_oop(obj)->is_gc_marked());
  precond(obj->isTrackable());
  rtCLDCleaner::unlock_cld(cast_to_oop(obj));
  // ClassLoaderData* cld = rtHeapUtil::tenured_class_loader_data(cast_to_oop(obj));
  // if (cld != NULL) cld->decrease_holder_ref_count();
  
  if (false && !rtHeap::in_full_gc) {
    g_freeMemStore.reclaimMemory(obj);
  }
  obj->markDestroyed();
}

bool RuntimeHeap::is_broken_link(GCObject* anchor, GCObject* link) {
  return cast_to_oop(link) == 
      java_lang_ref_Reference::unknown_referent_no_keepalive(cast_to_oop(anchor));
}


void rtHeap__addResurrectedObject(GCObject* node);

HeapWord* RtSpace::allocate(size_t word_size) {
  HeapWord* heap = _SUPER::allocate(word_size);
  if (heap == NULL) {
    // int cntGarbage = g_garbage_list.size();
    // if (cntGarbage > 0) {
      heap = (HeapWord*)g_freeMemStore.recycle(word_size);
      if (heap != NULL) {
        rtgc_log(LOG_OPT(6), "recycle garbage %ld %p\n", 
            word_size, heap);
        rtHeap__addResurrectedObject(reinterpret_cast<GCObject*>(heap));
      }
  }
  return heap;
}

HeapWord* RtSpace::par_allocate(size_t word_size) {
  HeapWord* heap = _SUPER::par_allocate(word_size);
  return heap;
}

void rtSpace__initialize() {
  g_freeMemStore.initialize();
}