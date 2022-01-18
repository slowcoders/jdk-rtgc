
#include "precompiled.hpp"
#include "c1/c1_Compilation.hpp"
#include "c1/c1_Defs.hpp"
#include "c1/c1_FrameMap.hpp"
#include "c1/c1_Instruction.hpp"
#include "c1/c1_LIRAssembler.hpp"
#include "c1/c1_LIRGenerator.hpp"
#include "c1/c1_ValueStack.hpp"
#include "ci/ciArrayKlass.hpp"
#include "ci/ciInstance.hpp"
#include "ci/ciObjArray.hpp"
#include "ci/ciUtilities.hpp"
#include "gc/shared/barrierSet.hpp"
#include "gc/shared/c1/barrierSetC1.hpp"
#include "oops/klass.inline.hpp"
#include "runtime/arguments.hpp"
#include "runtime/sharedRuntime.hpp"
#include "runtime/stubRoutines.hpp"
#include "runtime/vm_version.hpp"
#include "utilities/bitMap.inline.hpp"
#include "utilities/macros.hpp"
#include "gc/rtgc/c1/rtgcBarrierSetC1.hpp"
#include "gc/rtgc/rtgcBarrier.hpp"


volatile int enable_rtgc_c1_barrier_hook = 1;

RtgcBarrierSetC1::RtgcBarrierSetC1() {
  RtgcBarrier::init_barrier_runtime();
}

void RtgcBarrierSetC1::load_at_resolved(LIRAccess& access, LIR_Opr result) {
  // LIRGenerator *gen = access.gen();
  // DecoratorSet decorators = access.decorators();
  // bool is_volatile = (((decorators & MO_SEQ_CST) != 0) || AlwaysAtomicAccesses);
  // bool needs_patching = (decorators & C1_NEEDS_PATCHING) != 0;
  // bool mask_boolean = (decorators & C1_MASK_BOOLEAN) != 0;
  // bool in_native = (decorators & IN_NATIVE) != 0;

  // if (support_IRIW_for_not_multiple_copy_atomic_cpu && is_volatile) {
  //   //__ membar();
  //   RTGC::lock_heap(NULL);
  // }
    BarrierSetC1::load_at_resolved(access, result);
    return;
  // if (is_volatile) {
  //   __ membar_acquire();
  // }

  // /* Normalize boolean value returned by unsafe operation, i.e., value  != 0 ? value = true : value false. */
  // if (mask_boolean) {
  //   LabelObj* equalZeroLabel = new LabelObj();
  //   __ cmp(lir_cond_equal, result, 0);
  //   __ branch(lir_cond_equal, T_BOOLEAN, equalZeroLabel->label());
  //   __ move(LIR_OprFact::intConst(1), result);
  //   __ branch_destination(equalZeroLabel->label());
  // }
}

LIR_Opr get_resolved_addr(LIRAccess& access, Register reg) {
  DecoratorSet decorators = access.decorators();
  LIRGenerator* gen = access.gen();
  bool is_array = (decorators & IS_ARRAY) != 0;
  bool on_anonymous = (decorators & ON_UNKNOWN_OOP_REF) != 0;
  bool needs_patching = (decorators & C1_NEEDS_PATCHING) != 0;

  bool precise = is_array || on_anonymous;
  LIR_Opr addr = access.resolved_addr();

  if (addr->is_address()) {
    LIR_Address* address = addr->as_address_ptr();
    // ptr cannot be an object because we use this barrier for array card marks
    // and addr can point in the middle of an array.
    LIR_Opr resolved_addr;// = // FrameMap::as_oop_opr(reg);
    if (!address->index()->is_valid() && address->disp() == 0) {
      resolved_addr = address->base();//   gen->lir()->move(address->base(), resolved_addr);
    } else {
      assert(false && address->disp() != max_jint, "lea doesn't support patched addresses!");
      resolved_addr = gen->new_pointer_register();
      if (needs_patching) {
        gen->lir()->leal(addr, resolved_addr, lir_patch_normal, access.patch_emit_info());
        access.clear_decorators(C1_NEEDS_PATCHING);
      } else {
        gen->lir()->leal(addr, resolved_addr);
      }
      access.set_resolved_addr(LIR_OprFact::address(new LIR_Address(resolved_addr, access.type())));
    }
    addr = resolved_addr;
  }
  assert(addr->is_register(), "must be a register at this point");
  return addr;
}

/* 참고) LIR_Op1::emit_code()
   -> LIR_Assembler::emit_op1() 
     -> LIR_Assembler::move_op()
        -> LIR_Assembler::mem2reg()
*/
void __rtgc_store(narrowOop* addr, oopDesc* new_value, oopDesc* base) {
  printf("store %p(%p) = %p\n", base, addr, new_value);
   RtgcBarrier::oop_store(addr, new_value, base);
}

void __rtgc_store_nih(narrowOop* addr, oopDesc* new_value) {
  printf("store __(%p) = %p\n", addr, new_value);
   RtgcBarrier::oop_store_not_in_heap(addr, new_value);
}

void RtgcBarrierSetC1::store_at_resolved(LIRAccess& access, LIR_Opr value) {
  DecoratorSet decorators = access.decorators();
  if (!access.is_oop() || (AS_NO_REF & decorators)) {
    BarrierSetC1::store_at_resolved(access, value);
    return;
  }

  LIRGenerator* gen = access.gen();
  bool mask_boolean = (decorators & C1_MASK_BOOLEAN) != 0;
  bool is_volatile = (((decorators & MO_SEQ_CST) != 0) || AlwaysAtomicAccesses);

  if (mask_boolean) {
    value = gen->mask_boolean(access.base().opr(), value, access.access_emit_info());
  }

  if (is_volatile) {
    // gen->lir()->membar_release();
  }

  bool in_heap = decorators & IN_HEAP;
  LIRItem base = access.base().item();
  LIR_Opr addr = get_resolved_addr(access, c_rarg0);
  
  BasicTypeList signature;
  signature.append(T_ADDRESS); // addr
  signature.append(T_OBJECT); // new_value
  if (in_heap) signature.append(T_OBJECT); // object
  
  LIR_OprList* args = new LIR_OprList();
  args->append(addr); 
  args->append(value);
  if (in_heap) args->append(base.result());

  address fn = RtgcBarrier::getStoreFunction(in_heap);
  // if (in_heap)
  //   fn = reinterpret_cast<address>(__rtgc_store);
  // else 
  //   fn = reinterpret_cast<address>(__rtgc_store_nih);

  gen->call_runtime(&signature, args,
              fn,
              voidType, NULL);

  if (is_volatile && !support_IRIW_for_not_multiple_copy_atomic_cpu) {
    // gen->lir()->membar();
  }
  return;    
}


oopDesc* __rtgc_xchg(volatile narrowOop* addr, oopDesc* new_value, oopDesc* base) {
  printf("xchg %p(%p) = %p\n", base, addr, new_value);
  return RtgcBarrier::oop_xchg(addr, new_value, base);
}

oopDesc* __rtgc_xchg_nih(volatile narrowOop* addr, oopDesc* new_value) {
  printf("xchg __(%p) = %p\n", addr, new_value);
  return RtgcBarrier::oop_xchg_not_in_heap(addr, new_value);
}

LIR_Opr RtgcBarrierSetC1::atomic_xchg_at_resolved(LIRAccess& access, LIRItem& value) {
  DecoratorSet decorators = access.decorators();
  if (!access.is_oop() || (AS_NO_REF & decorators)) {
    return BarrierSetC1::atomic_xchg_at_resolved(access, value);
  }
  printf("c1 atomic_xchg_at_resolved");
  LIRGenerator* gen = access.gen();
  LIRItem base = access.base().item();
  bool in_heap = decorators & IN_HEAP;
  if (in_heap) base.load_item();//_force(FrameMap::as_oop_opr(c_rarg2));
  LIR_Opr addr = get_resolved_addr(access, c_rarg0);// .offset().item();
  if (addr->is_register()) {
    //assert_different_registers(addr->as_register(), c_rarg1, c_rarg2);  //addr.load_item_force(FrameMap::as_opr(c_rarg0));
  }
  value.load_item();//_force(FrameMap::as_oop_opr(c_rarg1));

  assert(access.decorators() & IN_HEAP, "store_at_resolved not in heap!!");

  BasicTypeList signature;
  signature.append(T_INT); // addr
  signature.append(T_OBJECT); // new_value
  if (in_heap) signature.append(T_OBJECT);    // object
  
  LIR_OprList* args = new LIR_OprList();
  args->append(addr);//.result());
  args->append(value.result());
  if (in_heap) args->append(base.result());

  address fn = RtgcBarrier::getXchgFunction(in_heap);
  // if (in_heap)
  //   fn = reinterpret_cast<address>(__rtgc_xchg);
  // else 
  //   fn = reinterpret_cast<address>(__rtgc_xchg_nih);

  LIR_Opr res = gen->call_runtime(&signature, args,
                fn,
                objectType, NULL);
  return res;    
}

oopDesc* __rtgc_cmpxchg(oopDesc* base, volatile narrowOop* addr, oopDesc* cmp_value, oopDesc* new_value) {
  printf("cmpxchg %p(%p) = %p/%p\n", base, addr, cmp_value, new_value);
//  return RtgcBarrier::oop_cmpxchg(addr, cmp_value, new_value, base);
  narrowOop cmp_v = CompressedOops::encode(cmp_value);
  narrowOop new_v = CompressedOops::encode(new_value);
  narrowOop res;
  res = Atomic::cmpxchg(addr, cmp_v, new_v);
  return CompressedOops::decode(res);
  // return BarrierSet::AccessBarrier<0, BarrierSet>::atomic_cmpxchg_in_heap<narrowOop>(addr, 
  //   );
}

oopDesc* __rtgc_cmpxchg_nih(volatile narrowOop* addr, oopDesc* cmp_value, oopDesc* new_value) {
  printf("cmpxchg __(%p) = %p/%p\n", addr, cmp_value, new_value);
  return RtgcBarrier::oop_cmpxchg_not_in_heap(addr, cmp_value, new_value);
}

LIR_Opr RtgcBarrierSetC1::atomic_cmpxchg_at_resolved(LIRAccess& access, LIRItem& cmp_value, LIRItem& new_value) {
  DecoratorSet decorators = access.decorators();
  if (!access.is_oop() || (AS_NO_REF & decorators)) {
    return BarrierSetC1::atomic_cmpxchg_at_resolved(access, cmp_value, new_value);
  }

  printf("c1 atomic_cmpxchg_at_resolved\n");
  LIRGenerator* gen = access.gen();
  LIRItem base = access.base().item();
  //LIRItem addr = access.offset().item();
  bool in_heap = decorators & IN_HEAP;
  LIR_Opr addr = get_resolved_addr(access, c_rarg0);;
  if (addr->is_register()) {
    //assert_different_registers(addr->as_register(), c_rarg1, c_rarg2, c_rarg3);// addr.load_item_force(FrameMap::as_opr(c_rarg0));
  }
  cmp_value.load_item();//_force(FrameMap::rax_oop_opr);//FrameMap::as_oop_opr(c_rarg1));
  new_value.load_item();//_force(FrameMap::as_oop_opr(c_rarg2));
  if (in_heap) base.load_item();//_force(FrameMap::as_oop_opr(c_rarg3));
  assert(access.decorators() & IN_HEAP, "store_at_resolved not in heap!!");


  BasicTypeList signature;
  if (in_heap) signature.append(T_OBJECT); // object
  signature.append(T_ADDRESS);    // addr
  signature.append(T_OBJECT); // cmp_value
  signature.append(T_OBJECT); // new_value
  
  LIR_OprList* args = new LIR_OprList();
  if (in_heap) args->append(base.result());
  args->append(addr);//.result());
  args->append(cmp_value.result());
  args->append(new_value.result());

  address fn = RtgcBarrier::getCmpXchgFunction(in_heap);
  if (in_heap)
    fn = reinterpret_cast<address>(__rtgc_cmpxchg);
  else 
    fn = reinterpret_cast<address>(__rtgc_cmpxchg_nih);

  LIR_Opr res = gen->call_runtime(&signature, args,
              fn,
              objectType, NULL);
  return res;    
}

const char* RtgcBarrierSetC1::rtcall_name_for_address(address entry) {
  #define FUNCTION_CASE(a, f) \
    if ((intptr_t)a == CAST_FROM_FN_PTR(intptr_t, f))  return #f

  // FUNCTION_CASE(entry, rtgc_oop_xchg_0);
  // FUNCTION_CASE(entry, rtgc_oop_xchg_3);
  // FUNCTION_CASE(entry, rtgc_oop_xchg_8);
  // FUNCTION_CASE(entry, rtgc_oop_cmpxchg_0);
  // FUNCTION_CASE(entry, rtgc_oop_cmpxchg_3);
  // FUNCTION_CASE(entry, rtgc_oop_cmpxchg_8);
  #undef FUNCTION_CASE

  return "RtgcRuntime::method";
}