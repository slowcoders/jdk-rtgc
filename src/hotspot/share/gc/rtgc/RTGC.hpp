#ifndef SHARE_GC_RTGC_RTGC_HPP
#define SHARE_GC_RTGC_RTGC_HPP


namespace RTGC {
  class GCNode;
  class GCObject;
  typedef bool (*RefTracer1)(GCObject* obj);
  typedef bool (*RefTracer2)(GCObject* obj, void* param);
  typedef bool (*RefTracer3)(GCObject* obj, GCObject* anchor, void* param);
  
  void scanInstanceGraph(GCObject* obj, RefTracer1 tracer);
  void scanInstanceGraph(GCObject* obj, RefTracer2 tracer, void* param);
  void scanInstanceGraph(GCObject* obj, RefTracer3 tracer, void* param);
  void iterateReferents(GCObject* obj, RefTracer2 trace, void* param);

  extern bool is_narrow_oop_mode;

  inline static GCNode* to_node(oopDesc* obj) {
    return reinterpret_cast<GCNode*>(obj);
  }
  inline static GCObject* to_obj(oopDesc* obj) {
    return reinterpret_cast<GCObject*>(obj);
  }

  void initialize();

  bool needTrack(oopDesc* obj);

  bool isPublished(GCObject* obj);

  inline bool isPublished(oopDesc* obj) {
    return isPublished(to_obj(obj));
  }

  void publish_and_lock_heap(GCObject* obj, bool doPublish);

  inline void publish_and_lock_heap(oopDesc* obj, oopDesc* base) {
    publish_and_lock_heap(to_obj(obj), isPublished(to_obj(base)));
  }

  inline void publish_and_lock_heap(oopDesc* obj) {
    publish_and_lock_heap(to_obj(obj), true);
  }

  GCObject* getForwardee(GCObject* obj);

  bool lock_if_published(GCObject* obj);

  void lock_heap();

  void unlock_heap(bool locked);

  bool heap_locked_bySelf();

  void add_referrer_unsafe(oopDesc* obj, oopDesc* referrer);

  void add_young_root(oopDesc* obj);

  void on_field_changed(oopDesc* base, oopDesc* oldValue, oopDesc* newValue, volatile void* addr, const char* fn);

  void on_root_changed(oopDesc* oldValue, oopDesc* newValue, volatile void* addr, const char* fn);

  bool collectGarbage(oopDesc* obj);
};


#endif // SHARE_GC_RTGC_RTGC_HPP
