#ifndef SHARE_GC_RTGC_SPACE_HPP
#define SHARE_GC_RTGC_SPACE_HPP

#include "gc/shared/space.inline.hpp"
#include "gc/rtgc/impl/GCObject.hpp"

namespace rtHeapUtil {

  bool is_dead_space(oopDesc* obj);

  void ensure_alive_or_deadsapce(oopDesc* old_p, oopDesc* anchor=NULL);
}

namespace RTGC {
  
  class FreeNode {
  friend class FreeMemStore;
    FreeNode* prev;
    FreeNode* next;
  };

  class FreeMemQ {
  friend class FreeMemStore;
    FreeNode* top;
    int objSize;
  };

  class FreeMemStore {
  public:
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
    RtSpace(BlockOffsetSharedArray* sharedOffsetArray,
                MemRegion mr) :
      TenuredSpace(sharedOffsetArray, mr) {}

    virtual HeapWord* allocate(size_t word_size);
    virtual HeapWord* par_allocate(size_t word_size);

  };
}

#endif