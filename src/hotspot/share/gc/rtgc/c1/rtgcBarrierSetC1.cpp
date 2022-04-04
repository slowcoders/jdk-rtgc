
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
#include "classfile/vmClasses.hpp"
#include "classfile/vmSymbols.hpp"
#include "runtime/arguments.hpp"
#include "runtime/sharedRuntime.hpp"
#include "runtime/stubRoutines.hpp"
#include "runtime/vm_version.hpp"
#include "utilities/bitMap.inline.hpp"
#include "utilities/macros.hpp"
#include "runtime/interfaceSupport.inline.hpp"
#include "asm/assembler.hpp"
#include "asm/macroAssembler.inline.hpp"
#ifdef COMPILER1
#include "c1/c1_LIRAssembler.hpp"
#include "c1/c1_MacroAssembler.hpp"
#include "gc/z/c1/zBarrierSetC1.hpp"
#endif // COMPILER1

#include "gc/rtgc/c1/rtgcBarrierSetC1.hpp"
#include "gc/rtgc/rtgcBarrier.hpp"
#include "gc/rtgc/RTGC.hpp"
#include "gc/rtgc/rtgcDebug.hpp"

static int rtgc_log_trigger = 0;
static const bool ENABLE_CPU_MEMBAR = false;

static const int LOG_OPT(int function) {
  return RTGC::LOG_OPTION(RTGC::LOG_BARRIER_C1, function);
}

static LIR_Opr get_resolved_addr(LIRAccess& access) {
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

static LIR_Opr call_barrier(address fn, LIRAccess& access, LIR_Opr new_value, ValueType* result_type,
               LIR_Opr cmp_value = LIR_OprFact::illegalOpr) {
  DecoratorSet decorators = access.decorators();
  LIRGenerator* gen = access.gen();

  LIRItem base = access.base().item();
  bool in_heap = decorators & IN_HEAP;
  bool compare = cmp_value->is_valid();
  LIR_Opr addr = get_resolved_addr(access);

  BasicTypeList signature;
  signature.append(T_ADDRESS); // addr
  if (compare) signature.append(T_OBJECT); // cmp_value
  signature.append(T_OBJECT); // new_value
  if (in_heap) {
    signature.append(T_OBJECT); // object
    base.load_item();
  }
  
  LIR_OprList* args = new LIR_OprList();
  args->append(addr); 
  if (compare) args->append(cmp_value); // cmp_value
  args->append(new_value);
  if (in_heap) {
    args->append(base.result());
  }

  return gen->call_runtime(&signature, args, fn, result_type, NULL);
}

RtgcBarrierSetC1::RtgcBarrierSetC1() {
  RtgcBarrier::init_barrier_runtime();
}

bool RtgcBarrierSetC1::needBarrier_onResolvedAddress(LIRAccess& access, bool op_store) {
  return access.is_oop() && !RtgcBarrier::is_raw_access(access.decorators(), op_store)
      && !access.resolved_addr()->is_stack();
}

LIR_Opr RtgcBarrierSetC1::resolve_address(LIRAccess& access, bool resolve_in_register) {
  resolve_in_register |= access.is_oop()
      && !RtgcBarrier::is_raw_access(access.decorators(), false)
      && !access.base().opr()->is_stack();
  return BarrierSetC1::resolve_address(access, resolve_in_register);
}


oopDesc* __rtgc_load(narrowOop* addr) {
  RTGC::lock_heap();
  narrowOop res = *addr;
  oopDesc* result = CompressedOops::decode(res);
  rtgc_log(LOG_OPT(1), "load (%p) => (%p:%s) th=%p\n",
      addr, result, 
      result ? result->klass()->name()->bytes() : NULL, JavaThread::current());
  RTGC::unlock_heap(true);
  return result;
}

void RtgcBarrierSetC1::load_at_resolved(LIRAccess& access, LIR_Opr result) {
  DecoratorSet decorators = access.decorators();
  if (true || !needBarrier_onResolvedAddress(access, false)) {
    BarrierSetC1::load_at_resolved(access, result);
    return;
  }

  BasicTypeList signature;
  signature.append(T_ADDRESS); // addr
  
  LIR_OprList* args = new LIR_OprList();
  args->append(result);

  fatal("not_implemented");
  // address fn = RtgcBarrier::getPostLoadFunction(docorators);

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
  rtgc_log(true || LOG_OPT(2), "store-raw %p(%p) = %p\n", 
      base, addr, new_value);
  RawAccess<>::oop_store(addr, new_value);
}

void RtgcBarrierSetC1::store_at_resolved(LIRAccess& access, LIR_Opr value) {
  bool need_barrier = needBarrier_onResolvedAddress(access, true);
  if (!need_barrier) {
    LIR_Opr offset = access.offset().opr();
    bool setKlass = (access.decorators() & IN_HEAP) && 
                !(access.decorators() & IS_ARRAY) && 
                offset->is_constant() &&
                offset->as_jint() >= 0 &&
                offset->as_jint() <= oopDesc::klass_offset_in_bytes();
    assert(!setKlass, "just check");
    BarrierSetC1::store_at_resolved(access, value);
    return;
  }

  rtgc_log(LOG_OPT(11), "store_at_resolved\n");
  address fn = RtgcBarrier::getStoreFunction(access.decorators() | AS_RAW);
  call_barrier(fn, access, value, voidType);
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
  if (!needBarrier_onResolvedAddress(access, true)) {
    return BarrierSetC1::atomic_xchg_at_resolved(access, value);
  }

  value.load_item();
  LIRGenerator* gen = access.gen();
  address fn = RtgcBarrier::getXchgFunction(access.decorators() | AS_RAW);
  LIR_Opr res = call_barrier(fn, access, value.result(), objectType);
  return res;   
}

bool __rtgc_cmpxchg(volatile narrowOop* addr, oopDesc* cmp_value, oopDesc* new_value, oopDesc* base) {
  oopDesc* old_value = RtgcBarrier::oop_cmpxchg(addr, cmp_value, new_value, base);
  rtgc_log(cmp_value != NULL, "cmpxchg %p.%p = %p->%p %d\n", 
    base, addr, cmp_value, new_value, old_value == cmp_value);
  return old_value == cmp_value;
}

bool __rtgc_cmpxchg_nih(volatile narrowOop* addr, oopDesc* cmp_value, oopDesc* new_value) {
  oopDesc* old_value = RtgcBarrier::oop_cmpxchg_not_in_heap(addr, cmp_value, new_value);
  rtgc_log(cmp_value != NULL, "cmpxchg @.%p = %p->%p %d\n", 
    addr, cmp_value, new_value, old_value == cmp_value);
  return old_value == cmp_value;
}

LIR_Opr RtgcBarrierSetC1::atomic_cmpxchg_at_resolved(LIRAccess& access, LIRItem& cmp_value, LIRItem& new_value) {
  if (!needBarrier_onResolvedAddress(access, true)) {
    return BarrierSetC1::atomic_cmpxchg_at_resolved(access, cmp_value, new_value);
  }

  // access.base().item().load_item();
  new_value.load_item();
  cmp_value.load_item();
  LIRGenerator* gen = access.gen();
  address fn = RtgcBarrier::getCmpSetFunction(access.decorators() | AS_RAW);
  LIR_Opr res = call_barrier(fn, access, new_value.result(), intType, cmp_value.result());
  return res;   
}

const char* RtgcBarrierSetC1::rtcall_name_for_address(address entry) {
  #define FUNCTION_CASE(a, f) \
    if ((intptr_t)a == CAST_FROMfn_PTR(intptr_t, f))  return #f

  // FUNCTION_CASE(entry, rtgc_oop_xchg_0);
  // FUNCTION_CASE(entry, rtgc_oop_xchg_3);
  // FUNCTION_CASE(entry, rtgc_oop_xchg_8);
  // FUNCTION_CASE(entry, rtgc_oop_cmpxchg_0);
  // FUNCTION_CASE(entry, rtgc_oop_cmpxchg_3);
  // FUNCTION_CASE(entry, rtgc_oop_cmpxchg_8);
  #undef FUNCTION_CASE

  return "RtgcRuntime::method";
}