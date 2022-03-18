#include "precompiled.hpp"

#include "oops/oop.inline.hpp"
#include "memory/iterator.inline.hpp"
#include "oops/instanceKlass.inline.hpp"
#include "oops/fieldStreams.inline.hpp"
#include "runtime/globals.hpp"
#include "runtime/fieldDescriptor.inline.hpp"

#include "gc/rtgc/RTGC.hpp"
#include "gc/rtgc/rtgcDebug.hpp"

static const int LOG_OPT(int function) {
  return RTGC::LOG_OPTION(RTGC::LOG_HEAP, function);
}

class RTGC_MarkClosure : public BasicOopIterateClosure {
public:
  GrowableArrayCHeap<oopDesc*, mtGC> ref_stak;
  int _cnt;
  int _cntLocal;

  RTGC_MarkClosure() : _cnt(0), _cntLocal(0) {}

  void do_work(oopDesc* p) {
    if (p != NULL) {
      ref_stak.append(p);
      _cnt ++;
      if (!RTGC::isPublished(p)) {
        _cntLocal ++;
      }
      // rtgc_log(LOG_OPT(0x100), "mark stack %d %p\n", _cnt++, p);
    }
    //if (ref != NULL) RTGC::add_referrer(ref, _rookie);
  }

  virtual void do_oop(narrowOop* p) { do_work(CompressedOops::decode(*p)); }
  virtual void do_oop(oop*       p) { do_work(*p); }
};


class RtgcThreadLocalData {
  GrowableArrayCHeap<oopDesc*, mtGC> localObjects;
  // freeSpaces;

public:
  RtgcThreadLocalData() {}

  static RtgcThreadLocalData* data(Thread* thread) {
    return thread->gc_data<RtgcThreadLocalData>();
  }

  void registerLocalRookies(ThreadLocalAllocBuffer& tlab) {
    HeapWord* t_p = tlab.start();
    HeapWord* end = tlab.top();
    int cnt = 0;
    while (t_p < end) {
      oopDesc* p = cast_to_oop(t_p);
      if (!RTGC::collectGarbage(p)
      &&  !RTGC::isPublished(p)) {
        localObjects.append(p);
      }
      rtgc_log(LOG_OPT(12), "rookie %d, %s\n", ++cnt, p->klass()->name()->bytes());
      t_p += p->size();  // size() == sizeInBytes / sizeof(HeapWord);
    }
  }

  void markStack(Thread* thread) {
    ResourceMark rm;
    RTGC_MarkClosure c;
    thread->oops_do(&c, NULL);
    rtgc_log(LOG_OPT(12), "local root marked %d(%d)\n", c._cnt, c._cntLocal);
  }

  void collectTlab(Thread* thread) {
    int end = localObjects.length();
    int dst = 0;
    for (int idx = 0; idx < end; idx ++) {
      oopDesc* p = localObjects.at(idx);
      if (!RTGC::collectGarbage(p) && dst != idx
      &&  !RTGC::isPublished(p)) {
        localObjects.at(dst++) = p;
      }
    }
    if (dst != end) {
      localObjects.trunc_to(dst);
    }
    registerLocalRookies(thread->tlab());
  }

  HeapWord* try_alloc_tlab(const size_t word_size) {
    return nullptr;
  }

};


HeapWord* rtHeap::allocate_tlab(Thread* thread, const size_t word_size) {
  RtgcThreadLocalData* tld = RtgcThreadLocalData::data(thread);
  HeapWord* mem = tld->try_alloc_tlab(word_size);
  if (mem == nullptr) {
    tld->markStack(thread);
    //tld->collectTlab(thread);
  }
  return NULL;
}

