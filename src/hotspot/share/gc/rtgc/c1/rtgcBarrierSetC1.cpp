
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
#include "gc/rtgc/RTGC.hpp"

static int rtgc_log_trigger = 0;
static const bool ENABLE_CPU_MEMBAR = false;

RtgcBarrierSetC1::RtgcBarrierSetC1() {
  RtgcBarrier::init_barrier_runtime();
}

LIR_Opr RtgcBarrierSetC1::resolve_address(LIRAccess& access, bool resolve_in_register) {
  return BarrierSetC1::resolve_address(access, resolve_in_register | needBarrier(access));
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
    LIR_Opr resolved_addr = gen->new_pointer_register();
    if (!address->index()->is_valid() && address->disp() == 0) {
      gen->lir()->move(address->base(), resolved_addr);
    } else {
      assert(false && address->disp() != max_jint, "lea doesn't support patched addresses!");
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

oopDesc* __rtgc_load(narrowOop* addr) {
  RTGC::lock_heap(NULL);
  narrowOop res = *addr;
  oopDesc* result = CompressedOops::decode(res);
  if (RTGC::logLevel() > 3) {
    JavaThread* __the_thread__ = JavaThread::current();
    printf("load (%p) => (%p:%s) th=%p\n",
      addr, result, 
      result ? result->klass()->name()->bytes() : NULL, __the_thread__);
  }
  RTGC::unlock_heap(true);
  return result;
}

void RtgcBarrierSetC1::load_at_resolved(LIRAccess& access, LIR_Opr result) {
  DecoratorSet decorators = access.decorators();
  if (true || !needBarrier(access)) {
    BarrierSetC1::load_at_resolved(access, result);
    return;
  }

  BasicTypeList signature;
  signature.append(T_ADDRESS); // addr
  
  LIR_OprList* args = new LIR_OprList();
  args->append(result);

  // address fn = RtgcBarrier::getPostLoadFunction(in_heap);

  // LIR_Opr res = gen->call_runtime(&signature, args,
  //             fn,
  //             objectType, NULL);
}


/* 참고) LIR_Op1::emit_code()
   -> LIR_Assembler::emit_op1() 
     -> LIR_Assembler::move_op()
        -> LIR_Assembler::mem2reg()
*/
void __rtgc_store(narrowOop* addr, oopDesc* new_value, oopDesc* base) {
  if (RTGC::logLevel() > 3) {
    JavaThread* __the_thread__ = JavaThread::current();
    printf("store %p(%p) = %p th=%p\n", base, addr, new_value, __the_thread__);
  }
  RtgcBarrier::oop_store(addr, new_value, base);
}

void __rtgc_store_nih(narrowOop* addr, oopDesc* new_value) {
  printf("store __(%p) = %p\n", addr, new_value);
  RtgcBarrier::oop_store_not_in_heap(addr, new_value);
}

void RtgcBarrierSetC1::store_at_resolved(LIRAccess& access, LIR_Opr value) {
  if (!needBarrier(access)) {
    BarrierSetC1::store_at_resolved(access, value);
    return;
  }

  LIRGenerator* gen = access.gen();
  DecoratorSet decorators = access.decorators();
  bool in_heap = decorators & IN_HEAP;
  bool mask_boolean = (decorators & C1_MASK_BOOLEAN) != 0;
  bool is_volatile = (((decorators & MO_SEQ_CST) != 0) || AlwaysAtomicAccesses);

  assert(!mask_boolean, "oop can not cast to boolean");

  if (ENABLE_CPU_MEMBAR && is_volatile) {
    gen->lir()->membar_release();
  }

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

  if (ENABLE_CPU_MEMBAR && is_volatile && !support_IRIW_for_not_multiple_copy_atomic_cpu) {
    gen->lir()->membar();
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
  if (!needBarrier(access)) {
    return BarrierSetC1::atomic_xchg_at_resolved(access, value);
  }

  LIRGenerator* gen = access.gen();
  LIRItem base = access.base().item();
  bool in_heap = decorators & IN_HEAP;
  if (in_heap) base.load_item();
  LIR_Opr addr = get_resolved_addr(access, c_rarg0);
  value.load_item();

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

bool __rtgc_cmpxchg(volatile narrowOop* addr, oopDesc* cmp_value, oopDesc* new_value, oopDesc* base) {
  oopDesc* old_value = RtgcBarrier::oop_cmpxchg(addr, cmp_value, new_value, base);
  return old_value == cmp_value;
  if (false) {
    narrowOop cmp_v = CompressedOops::encode(cmp_value);
    narrowOop new_v = CompressedOops::encode(new_value);
    narrowOop res;
    res = Atomic::cmpxchg(addr, cmp_v, new_v);
    oopDesc* result = CompressedOops::decode(res);
    JavaThread* __the_thread__ = JavaThread::current();
    if (++rtgc_log_trigger > RTGC::opt1()) {
      printf("cmpxchg %d) %p(%p) = %p(%p)/%p th=%p \n", rtgc_log_trigger, base, 
        addr, cmp_value, result, new_value, __the_thread__);
    }
    return result;
  }
}

bool __rtgc_cmpxchg_nih(volatile narrowOop* addr, oopDesc* cmp_value, oopDesc* new_value) {
  // printf("cmpxchg __(%p) = %p/%p\n", addr, cmp_value, new_value);
  // narrowOop cmp_v = CompressedOops::encode(cmp_value);
  // narrowOop new_v = CompressedOops::encode(new_value);
  // narrowOop res;
  // res = Atomic::cmpxchg(addr, cmp_v, new_v);
  // oopDesc* old_value = CompressedOops::decode(res);
  oopDesc* old_value = RtgcBarrier::oop_cmpxchg_not_in_heap(addr, cmp_value, new_value);
  return old_value == cmp_value;
}

LIR_Opr RtgcBarrierSetC1::atomic_cmpxchg_at_resolved(LIRAccess& access, LIRItem& cmp_value, LIRItem& new_value) {
  DecoratorSet decorators = access.decorators();
  if (!needBarrier(access)) {
    return BarrierSetC1::atomic_cmpxchg_at_resolved(access, cmp_value, new_value);
  }

  LIRGenerator* gen = access.gen();
  bool in_heap = decorators & IN_HEAP;
  LIRItem base = access.base().item();
  LIR_Opr addr = get_resolved_addr(access, c_rarg0);;
  cmp_value.load_item();
  new_value.load_item();
  if (in_heap) base.load_item();

  BasicTypeList signature;
  signature.append(T_ADDRESS); // addr
  signature.append(T_OBJECT); // cmp_value
  signature.append(T_OBJECT); // new_value
  if (in_heap) signature.append(T_OBJECT); // object
  
  LIR_OprList* args = new LIR_OprList();
  args->append(addr);
  args->append(cmp_value.result());
  args->append(new_value.result());
  if (in_heap) args->append(base.result());

  address fn = RtgcBarrier::getCmpXchgFunction(in_heap);
  if (in_heap)
    fn = reinterpret_cast<address>(__rtgc_cmpxchg);
  else 
    fn = reinterpret_cast<address>(__rtgc_cmpxchg_nih);

  LIR_Opr res = gen->call_runtime(&signature, args,
              fn,
              intType, NULL);
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