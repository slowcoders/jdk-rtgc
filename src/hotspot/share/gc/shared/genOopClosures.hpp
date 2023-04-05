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
#include "gc/rtgc/rtHeapEx.hpp"

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
template <typename Derived, bool clear_modified_flag=false>
class FastScanClosure : public BasicOopIterateClosure {
private:
  DefNewGeneration* _young_gen;
  HeapWord*         _young_gen_end;

  template <typename T>
  void do_oop_work(T* p);

protected:
  FastScanClosure(DefNewGeneration* g);

public:
#if INCLUDE_RTGC // RTGC_OPT_CLD_SCAN
  DefNewGeneration* young_gen() { return _young_gen; }
#endif 

  virtual void do_oop(oop* p);
  virtual void do_oop(narrowOop* p);
};

// Closure for scanning DefNewGeneration when iterating over the old generation.
//
// This closure performs barrier store calls on pointers into the DefNewGeneration.
#if !INCLUDE_RTGC // RTGC_OPT_YOUNG_ROOTS
class DefNewYoungerGenClosure : public FastScanClosure<DefNewYoungerGenClosure> {
private:
  Generation*  _old_gen;
  HeapWord*    _old_gen_start;
  CardTableRS* _rs;

public:
  DefNewYoungerGenClosure(DefNewGeneration* young_gen, Generation* old_gen);

  template <typename T>
  void barrier(T* p, oop forwardee);
};

#else // RTGC_OPT_YOUNG_ROOTS

class YoungRootClosure : public RtYoungRootClosure, public FastScanClosure<YoungRootClosure, true> {
public:    
  Generation* _old_gen;
  VoidClosure* _complete_closure;
  VoidClosure* _unstable_complete_closure;
  bool _is_root_reachable;
  bool _has_young_ref;
  bool _is_strong_rechable;

  YoungRootClosure(DefNewGeneration* young_gen, Generation* old_gen, VoidClosure* complete_closure, VoidClosure* unstable_complete_closure)
      : FastScanClosure<YoungRootClosure, true>(young_gen), 
        _old_gen(old_gen), _complete_closure(complete_closure), _unstable_complete_closure(unstable_complete_closure) {}
  
  virtual bool iterate_tenured_young_root_oop(oopDesc* obj, bool is_root_reachble);

  virtual void do_complete(bool is_strong_rechable);

  virtual oop keep_alive_young_referent(oop p);

  void barrier(oop old_p, oop new_p) {
    if (!_is_root_reachable) {
      rtHeap::mark_young_root_reachable(_current_anchor, new_p);
    }
    _has_young_ref = true;
  }

  void trackable_barrier(oop old_p, oop new_p) {
    rtHeap::ensure_trackable_link(_current_anchor, new_p);
  }

  void promoted_trackable_barrier(oop old_p, oop new_p) {
    rtHeap::add_trackable_link(_current_anchor, new_p);
  }

};
#endif

// Closure for scanning DefNewGeneration when *not* iterating over the old generation.
//
// This closures records changes to oops in CLDs.
class DefNewScanClosure : public FastScanClosure<DefNewScanClosure, false> {
  ClassLoaderData* _scanned_cld;
public:
  DefNewScanClosure(DefNewGeneration* g);

  void set_scanned_cld(ClassLoaderData* cld) {
    assert(cld == NULL || _scanned_cld == NULL, "Must be");
    _scanned_cld = cld;
  }

  void barrier(oop old_p, oop new_p);

#if INCLUDE_RTGC // RtToungRootClosure
  void trackable_barrier(oop old_p, oop new_p) { 
    rtHeap::mark_survivor_reachable(new_p);
  }

  void promoted_trackable_barrier(oop old_p, oop new_p) { 
    // RTGC-TODO 생략할 수 있다(?)
    rtHeap::mark_survivor_reachable(new_p);
  }

  void do_object(oop obj) {
    obj->oop_iterate(this);
  }
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
