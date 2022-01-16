
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
#include "gc/rtgc/rtgc_jrt.hpp"


volatile int enable_rtgc_c1_barrier_hook = 1;

void RtgcBarrierSetC1::load_at_resolved(LIRAccess& access, LIR_Opr value) {
    BarrierSetC1::load_at_resolved(access, value);
    return;
}

LIR_Opr get_resolved_addr(LIRAccess& access, Register reg) {
  DecoratorSet decorators = access.decorators();
  LIRGenerator* gen = access.gen();
  bool is_array = (decorators & IS_ARRAY) != 0;
  bool on_anonymous = (decorators & ON_UNKNOWN_OOP_REF) != 0;

  bool precise = is_array || on_anonymous;
  LIR_Opr addr = access.resolved_addr();

  if (addr->is_address()) {
    LIR_Address* address = addr->as_address_ptr();
    // ptr cannot be an object because we use this barrier for array card marks
    // and addr can point in the middle of an array.
    LIR_Opr ptr = // FrameMap::as_oop_opr(reg);
      gen->new_pointer_register();
    if (!address->index()->is_valid() && address->disp() == 0) {
      gen->lir()->move(address->base(), ptr);
    } else {
      assert(address->disp() != max_jint, "lea doesn't support patched addresses!");
      gen->lir()->leal(addr, ptr);
    }
    addr = ptr;
  }
  assert(addr->is_register(), "must be a register at this point");
  return addr;
}

/* 참고) LIR_Op1::emit_code()
   -> LIR_Assembler::emit_op1() 
     -> LIR_Assembler::move_op()
        -> LIR_Assembler::mem2reg()
*/
void RtgcBarrierSetC1::store_at_resolved(LIRAccess& access, LIR_Opr value) {
  if (!enable_rtgc_c1_barrier_hook || !access.is_oop()) {
    BarrierSetC1::store_at_resolved(access, value);
    return;
  }

  LIRGenerator* gen = access.gen();
  LIRItem base = access.base().item();
  LIR_Opr addr = get_resolved_addr(access, c_rarg0);
  bool in_heap = access.decorators() & IN_HEAP;
  
  BasicTypeList signature;
  signature.append(T_ADDRESS); // addr
  signature.append(T_OBJECT); // new_value
  if (in_heap) signature.append(T_OBJECT); // object
  
  LIR_OprList* args = new LIR_OprList();
  args->append(addr);
  args->append(value);
  if (in_heap) args->append(base.result());

  address fn = RtgcBarrier::getXchgFunction(in_heap);
  gen->call_runtime(&signature, args,
              fn,
              voidType, NULL);
  return;    
}


LIR_Opr RtgcBarrierSetC1::atomic_xchg_at_resolved(LIRAccess& access, LIRItem& value) {
  if (!enable_rtgc_c1_barrier_hook || !access.is_oop()) {
    return BarrierSetC1::atomic_xchg_at_resolved(access, value);
  }

  LIRGenerator* gen = access.gen();
  LIRItem base = access.base().item();
  LIRItem addr = access.offset().item();

  bool in_heap = access.decorators() & IN_HEAP;
  addr.load_item_force(FrameMap::as_opr(c_rarg0));
  value.load_item_force(FrameMap::as_oop_opr(c_rarg1));
  if (in_heap) base.load_item_force(FrameMap::as_oop_opr(c_rarg2));

  assert(access.decorators() & IN_HEAP, "store_at_resolved not in heap!!");

  BasicTypeList signature;
  signature.append(T_INT); // addr
  signature.append(T_OBJECT); // new_value
  if (in_heap) signature.append(T_OBJECT);    // object
  
  LIR_OprList* args = new LIR_OprList();
  args->append(addr.result());
  args->append(value.result());
  if (in_heap) args->append(base.result());

  address fn = RtgcBarrier::getXchgFunction(access.decorators() & IN_HEAP);
  LIR_Opr res = gen->call_runtime(&signature, args,
                fn,
                objectType, NULL);
  return res;    
}


LIR_Opr RtgcBarrierSetC1::atomic_cmpxchg_at_resolved(LIRAccess& access, LIRItem& cmp_value, LIRItem& new_value) {
  if (!enable_rtgc_c1_barrier_hook || !access.is_oop()) {
    return BarrierSetC1::atomic_cmpxchg_at_resolved(access, cmp_value, new_value);
  }

  LIRGenerator* gen = access.gen();
  LIRItem base = access.base().item();
  LIRItem addr = access.offset().item();
  bool in_heap = access.decorators() & IN_HEAP;

  addr.load_item_force(FrameMap::as_opr(c_rarg0));
  cmp_value.load_item_force(FrameMap::as_oop_opr(c_rarg1));
  new_value.load_item_force(FrameMap::as_oop_opr(c_rarg2));
  if (in_heap) base.load_item_force(FrameMap::as_oop_opr(c_rarg3));
  assert(access.decorators() & IN_HEAP, "store_at_resolved not in heap!!");


  BasicTypeList signature;
  signature.append(T_ADDRESS);    // addr
  signature.append(T_OBJECT); // cmp_value
  signature.append(T_OBJECT); // new_value
  if (in_heap) signature.append(T_OBJECT); // object
  
  LIR_OprList* args = new LIR_OprList();
  args->append(addr.result());
  args->append(cmp_value.result());
  args->append(new_value.result());
  if (in_heap) args->append(base.result());

  address fn = RtgcBarrier::getCmpXchgFunction(access.decorators() & IN_HEAP);
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