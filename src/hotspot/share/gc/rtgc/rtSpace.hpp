#ifndef SHARE_GC_RTGC_SPACE_HPP
#define SHARE_GC_RTGC_SPACE_HPP

#include "gc/shared/space.inline.hpp"
#include "gc/rtgc/impl/GCObject.hpp"

namespace rtHeapUtil {
  void resurrect_young_root(RTGC::GCObject* node);

  void ensure_alive_or_deadsapce(oopDesc* old_p, oopDesc* anchor=NULL);
}

namespace RTGC {
  
  struct FreeNode {
    FreeNode* _prev;
    FreeNode* _next;
  };

  struct FreeMemQ {
    FreeNode* _top;
    int _objSize;
  };

  class FreeMemStore {
  public:
    void initialize() { freeMemQList.initialize(); }
    void reclaimMemory(GCObject* garbage); 
    void* recycle(size_t word_size);
    static void clearStore();
  private:
    HugeArray<FreeMemQ> freeMemQList;
    FreeMemQ* getFreeMemQ(size_t obj_size);
    int getFreeMemQIndex(size_t obj_size);
  };

  class RtSpace: public TenuredSpace {
  public:
    typedef TenuredSpace _SUPER;
    RtSpace(BlockOffsetSharedArray* sharedOffsetArray, MemRegion mr)
        : TenuredSpace(sharedOffsetArray, mr) {
      GCNode::g_trackable_heap_start = mr.start();
    }

    virtual HeapWord* allocate(size_t word_size);
    virtual HeapWord* par_allocate(size_t word_size);
  };
}

#endif