/*
 * Copyright (c) 2001, 2020, Oracle and/or its affiliates. All rights reserved.
 * DO NOT ALTER OR REMOVE COPYRIGHT NOTICES OR THIS FILE HEADER.
 *
 * This code is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License version 2 only, as
 * published by the Free Software Foundation.
 *
 * This code is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License
 * version 2 for more details (a copy is included in the LICENSE file that
 * accompanied this code).
 *
 * You should have received a copy of the GNU General Public License version
 * 2 along with this work; if not, write to the Free Software Foundation,
 * Inc., 51 Franklin St, Fifth Floor, Boston, MA 02110-1301 USA.
 *
 * Please contact Oracle, 500 Oracle Parkway, Redwood Shores, CA 94065 USA
 * or visit www.oracle.com if you need additional information or have any
 * questions.
 *
 */

#ifndef SHARE_GC_SHARED_GENOOPCLOSURES_HPP
#define SHARE_GC_SHARED_GENOOPCLOSURES_HPP

#include "memory/iterator.hpp"
#include "oops/oop.hpp"
#include "gc/rtgc/rtgcHeap.hpp"

class Generation;
class CardTableRS;
class CardTableBarrierSet;
class DefNewGeneration;
class KlassRemSet;

#if INCLUDE_SERIALGC

// Super closure class for scanning DefNewGeneration.
//
// - Derived: The derived type provides necessary barrier
//            after an oop has been updated.
template <typename Derived>
class FastScanClosure : public BasicOopIterateClosure {
private:
  DefNewGeneration* _young_gen;
  HeapWord*         _young_gen_end;

  template <typename T>
  void do_oop_work(T* p);

protected:
  FastScanClosure(DefNewGeneration* g);

public:
#if RTGC_OPT_CLD_SCAN
  DefNewGeneration* young_gen() { return _young_gen; }
  void trackable_barrier(void* p, oop obj) {}
#endif 

  virtual void do_oop(oop* p);
  virtual void do_oop(narrowOop* p);
};

// Closure for scanning DefNewGeneration when iterating over the old generation.
//
// This closure performs barrier store calls on pointers into the DefNewGeneration.
class DefNewYoungerGenClosure : public FastScanClosure<DefNewYoungerGenClosure> {
private:
  Generation*  _old_gen;
  HeapWord*    _old_gen_start;
  CardTableRS* _rs;
#if RTGC_OPT_YOUNG_ROOTS
  oopDesc* _trackable_anchor;
#endif

public:
  DefNewYoungerGenClosure(DefNewGeneration* young_gen, Generation* old_gen);

  template <typename T>
  void barrier(T* p, oop forwardee);

#if RTGC_OPT_YOUNG_ROOTS
  template <typename T>
  void trackable_barrier(T* p, oop obj) { barrier(p, obj); }

  void do_iterate(oop obj) {
    _trackable_anchor = obj;
    rtHeap::mark_promoted_trackable(obj);
    obj->oop_iterate(this);
    debug_only(_trackable_anchor = NULL;)
  }
#endif

};

#if RTGC_OPT_YOUNG_ROOTS
class YoungRootClosure : public FastScanClosure<YoungRootClosure>, public BoolObjectClosure {
  int _cnt_young_ref;
public:
  YoungRootClosure(DefNewGeneration* young_gen) : FastScanClosure(young_gen) {}
  
  bool do_object_b(oop obj) {
    _cnt_young_ref = 0;
    obj->oop_iterate(this);
    return _cnt_young_ref > 0;
  }

  template <typename T>
  void barrier(T* p, oop new_obj) {
    if (!rtHeap::is_trackable(new_obj)) {
      _cnt_young_ref ++;
    }
  }
};
#endif

// Closure for scanning DefNewGeneration when *not* iterating over the old generation.
//
// This closures records changes to oops in CLDs.
class DefNewScanClosure : public FastScanClosure<DefNewScanClosure> {
  ClassLoaderData* _scanned_cld;

public:
  DefNewScanClosure(DefNewGeneration* g);

  void set_scanned_cld(ClassLoaderData* cld) {
    assert(cld == NULL || _scanned_cld == NULL, "Must be");
    _scanned_cld = cld;
  }

  template <typename T>
  void barrier(T* p, oop new_obj);

#if RTGC_OPT_YOUNG_ROOTS
  void do_iterate(oop obj) {
    obj->oop_iterate(this);
  }

  void clear_trackable_anchor() {}

#endif
};

class CLDScanClosure: public CLDClosure {
  DefNewScanClosure* _scavenge_closure;
 public:
  CLDScanClosure(DefNewScanClosure* scavenge_closure) :
       _scavenge_closure(scavenge_closure) {}
  void do_cld(ClassLoaderData* cld);
};

#endif // INCLUDE_SERIALGC

class FilteringClosure: public OopIterateClosure {
 private:
  HeapWord*   _boundary;
  OopIterateClosure* _cl;
 protected:
  template <class T> inline void do_oop_work(T* p);
 public:
  FilteringClosure(HeapWord* boundary, OopIterateClosure* cl) :
    OopIterateClosure(cl->ref_discoverer()), _boundary(boundary),
    _cl(cl) {}
  virtual void do_oop(oop* p);
  virtual void do_oop(narrowOop* p);
  virtual bool do_metadata()            { assert(!_cl->do_metadata(), "assumption broken, must change to 'return _cl->do_metadata()'"); return false; }
  virtual void do_klass(Klass*)         { ShouldNotReachHere(); }
  virtual void do_cld(ClassLoaderData*) { ShouldNotReachHere(); }
};

#if INCLUDE_SERIALGC

// Closure for scanning DefNewGeneration's weak references.
//  -- weak references are processed all at once,
//  with no notion of which generation they were in.
class ScanWeakRefClosure: public OopClosure {
 protected:
  DefNewGeneration* _g;
  HeapWord*         _boundary;
  template <class T> inline void do_oop_work(T* p);
 public:
  ScanWeakRefClosure(DefNewGeneration* g);
  virtual void do_oop(oop* p);
  virtual void do_oop(narrowOop* p);
};

#endif // INCLUDE_SERIALGC

#endif // SHARE_GC_SHARED_GENOOPCLOSURES_HPP
