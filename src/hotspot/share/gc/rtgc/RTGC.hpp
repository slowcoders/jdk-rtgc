#ifndef SHARE_GC_RTGC_RTGC_HPP
#define SHARE_GC_RTGC_RTGC_HPP


namespace RTGC {
  class GCObject;
  typedef bool (*RefTracer)(GCObject* obj);
  typedef bool (*RefTracer2)(GCObject* obj, void* param);
  
  void scanInstance(GCObject* obj, RefTracer trace);
  void iterateReferents(GCObject* obj, RefTracer2 trace, void* param);

  inline static GCObject* to_obj(oopDesc* obj) {
    return reinterpret_cast<GCObject*>(obj);
  }

  void initialize();

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

  bool lock_if_published(GCObject* obj);

  void lock_heap();

  void unlock_heap(bool locked);

  void add_referrer(oopDesc* obj, oopDesc* referrer);

  void add_referrer_unsafe(oopDesc* obj, oopDesc* referrer);

  void remove_referrer(oopDesc* obj, oopDesc* referrer);

  void add_global_reference(oopDesc* obj);

  void remove_global_reference(oopDesc* obj);

  bool collectGarbage(oopDesc* obj);
};


#endif // SHARE_GC_RTGC_RTGC_HPP
