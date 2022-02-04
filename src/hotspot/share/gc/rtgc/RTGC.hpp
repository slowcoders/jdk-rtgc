#ifndef SHARE_GC_RTGC_RTGC_HPP
#define SHARE_GC_RTGC_RTGC_HPP


namespace RTGC {
  class GCObject;
  typedef bool (*RefTracer)(GCObject* obj);
  
  void scanInstance(GCObject* obj, RefTracer trace);

  inline static GCObject* to_obj(oopDesc* obj) {
    return reinterpret_cast<GCObject*>(obj);
  }

  void initialize();

  bool isPublished(GCObject* obj);

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

  void remove_referrer(oopDesc* obj, oopDesc* referrer);

  void add_global_reference(oopDesc* obj);

  void remove_global_reference(oopDesc* obj);

};


#endif // SHARE_GC_RTGC_RTGC_HPP
