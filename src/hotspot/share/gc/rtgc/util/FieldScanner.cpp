#include "precompiled.hpp"

#include "oops/oop.inline.hpp"
#include "memory/iterator.inline.hpp"
#include "oops/instanceKlass.inline.hpp"
#include "oops/fieldStreams.inline.hpp"
#include "runtime/globals.hpp"
#include "runtime/fieldDescriptor.inline.hpp"

#include "gc/rtgc/RTGC.hpp"
#include "gc/rtgc/rtgcDebug.hpp"
// #include "gc/rtgc/impl/GCRuntime.hpp"

using namespace RTGC;

static const int LOG_OPT(int function) {
  return RTGC::LOG_OPTION(RTGC::LOG_HEAP, function);
}

template <class T>
class FieldIterator {
  oopDesc* _base;
  OopMapBlock* _map;
  T* _field;
  int _cntMap;
  int _cntOop;

public:
  FieldIterator(oopDesc* p) : _base(p) {
    Klass* klass = p->klass();
    if (klass->is_objArray_klass()) {
      arrayOopDesc* array = (arrayOopDesc*)p;
      _cntOop = array->length();
      _field = (T*)(((address)p) + array->base_offset_in_bytes(T_OBJECT));
      _cntMap = 0;
    }
    else if (klass->is_instance_klass()) {
      InstanceKlass* objKlass = (InstanceKlass*)klass;
      _cntMap = objKlass->nonstatic_oop_map_count();
      if (--_cntMap < 0) {
        _cntOop = 0;
      } else {
        setOopMap(objKlass->start_of_nonstatic_oop_maps());
      }
    }
    else {
      _cntMap = _cntOop = 0;
    }
  }

  void setOopMap(OopMapBlock* map) {
    _map = map;
    _field = (T*)_base->obj_field_addr<T>(map->offset());
    _cntOop = map->count();
  }

  void iterate_static_pointers(OopClosure* closure) {
    InstanceKlass* klass = (InstanceKlass*)java_lang_Class::as_Klass(_base);
    if (klass == NULL || !klass->is_loaded()) return;

    rtgc_log(LOG_OPT(11), "tracing klass %p(%s)\n", _base, klass->name()->bytes());
    for (JavaFieldStream fs(klass); !fs.done(); fs.next()) {
      if (fs.access_flags().is_static()) {
        fieldDescriptor& fd = fs.field_descriptor();
        if (fd.field_type() == T_OBJECT || fd.field_type() == T_ARRAY) {
          T* field = _base->obj_field_addr<T>(fd.offset());
          debug_only(oop old = CompressedOops::decode(*field);)
          closure->do_oop(field);
          rtgc_log(LOG_OPT(11), "tracing klass offset [%d] %p -> %p \n", 
              fd.offset(), (void*)old, (void*)CompressedOops::decode(*field));
        }
      }
    }
  }

  oopDesc* next() {
    assert(_base->klass() != vmClasses::Class_klass(), "is static field processed as root?");
    while (true) {
      while (--_cntOop >= 0) {
        T heap_oop = RawAccess<>::oop_load(_field++);
        if (!CompressedOops::is_null(heap_oop)) {
          oop obj = CompressedOops::decode_not_null(heap_oop);
          return obj;
        }
      }
      if (--_cntMap < 0) break;
      setOopMap(_map + 1);
    }
    return NULL;
  }

public:
  FieldIterator() {}
  void iterate_pointers(OopClosure* closure) {
    if (_base->klass() == vmClasses::Class_klass()) {
      _base->oop_iterate_size((BasicOopIterateClosure*)closure);
      return;
    }
    while (true) {
      while (--_cntOop >= 0) {
        closure->do_oop(_field++);
      }
      if (--_cntMap < 0) break;
      setOopMap(_map + 1);
    }
    // if (_base->klass() == vmClasses::Class_klass()) {
    //   iterate_static_pointers(closure);
    // }
  }

  template <int args>
  static void scanInstanceGraph(oopDesc* p, void* trace_fn, void* param) {
    GrowableArrayCHeap<FieldIterator, mtGC> stack;
    stack.append(FieldIterator(p));
    FieldIterator* it = &stack.at(0);
    while (true) {
      oopDesc* link = it->next();
      if (link == nullptr) {
        stack.pop();
        int len = stack.length();
        if (--len < 0) break;
        it = &stack.at(len);
      }
      else if ((args == 1 && ((RefTracer1)trace_fn)(to_obj(link))) ||
               (args == 2 && ((RefTracer2)trace_fn)(to_obj(link), param)) ||
               (args == 3 && ((RefTracer3)trace_fn)(to_obj(link), to_obj(it->_base), param))) {
          stack.append(FieldIterator(link));
          it = &stack.at(stack.length() - 1);
      }
    }    
  }  
};

void RTGC::scanInstanceGraph(GCObject* root, RTGC::RefTracer1 trace) {
  oopDesc* p = cast_to_oop(root);
  if (RTGC::is_narrow_oop_mode) {
    FieldIterator<narrowOop>::scanInstanceGraph<1>(p, (void*)trace, p);
  } else {
    FieldIterator<oop>::scanInstanceGraph<1>(p, (void*)trace, p);
  }
}

void RTGC::scanInstanceGraph(GCObject* root, RTGC::RefTracer2 trace, void* param) {
  oopDesc* p = cast_to_oop(root);
  if (RTGC::is_narrow_oop_mode) {
    FieldIterator<narrowOop>::scanInstanceGraph<2>(p, (void*)trace, param);
  } else {
    FieldIterator<oop>::scanInstanceGraph<2>(p, (void*)trace, param);
  }
}

void RTGC::scanInstanceGraph(GCObject* root, RTGC::RefTracer3 trace, void* param) {
  oopDesc* p = cast_to_oop(root);
  if (RTGC::is_narrow_oop_mode) {
    FieldIterator<narrowOop>::scanInstanceGraph<3>(p, (void*)trace, param);
  } else {
    FieldIterator<oop>::scanInstanceGraph<3>(p, (void*)trace, param);
  }
}



class Ref2Tracer : public OopClosure {
  RTGC::RefTracer2 _trace;
  void* _param;
public:  
  Ref2Tracer(RTGC::RefTracer2 trace, void* param) {
    _trace = trace;
    _param = param;
  }

  template <class T>
  void do_oop_work(T* p) {
    T heap_oop = RawAccess<>::oop_load(p);
    if (!CompressedOops::is_null(heap_oop)) {
      oop obj = CompressedOops::decode_not_null(heap_oop);
      _trace(to_obj(obj), _param);
    }    
  }

  virtual void do_oop(oop* p) { do_oop_work(p); }
  virtual void do_oop(narrowOop* p) { do_oop_work(p); }

};

void RTGC::iterateReferents(GCObject* root, RTGC::RefTracer2 trace, void* param) {
  oopDesc* p = cast_to_oop(root);
  Ref2Tracer tracer(trace, param);
  if (RTGC::is_narrow_oop_mode) {
    FieldIterator<narrowOop> it(p);
    it.iterate_pointers(&tracer);
  }
  else {
    FieldIterator<oop> it(p);
    it.iterate_pointers(&tracer);
  }
}
