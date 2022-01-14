#include "precompiled.hpp"
#include "jvm.h"
#include "aot/aotLoader.hpp"
#include "classfile/stringTable.hpp"
#include "classfile/systemDictionary.hpp"
#include "classfile/vmSymbols.hpp"
#include "code/codeCache.hpp"
#include "code/compiledIC.hpp"
#include "code/icBuffer.hpp"
#include "code/compiledMethod.inline.hpp"
#include "code/scopeDesc.hpp"
#include "code/vtableStubs.hpp"
#include "compiler/abstractCompiler.hpp"
#include "compiler/compileBroker.hpp"
#include "compiler/disassembler.hpp"
#include "gc/shared/barrierSet.hpp"
#include "gc/shared/gcLocker.inline.hpp"
#include "interpreter/interpreter.hpp"
#include "interpreter/interpreterRuntime.hpp"
#include "jfr/jfrEvents.hpp"
#include "logging/log.hpp"
#include "memory/metaspaceShared.hpp"
#include "memory/resourceArea.hpp"
#include "memory/universe.hpp"
#include "oops/klass.hpp"
#include "oops/method.inline.hpp"
#include "oops/objArrayKlass.hpp"
#include "oops/oop.inline.hpp"
#include "prims/forte.hpp"
#include "prims/jvmtiExport.hpp"
#include "prims/methodHandles.hpp"
#include "prims/nativeLookup.hpp"
#include "runtime/arguments.hpp"
#include "runtime/atomic.hpp"
#include "runtime/biasedLocking.hpp"
#include "runtime/frame.inline.hpp"
#include "runtime/handles.inline.hpp"
#include "runtime/init.hpp"
#include "runtime/interfaceSupport.inline.hpp"
#include "runtime/java.hpp"
#include "runtime/javaCalls.hpp"
#include "runtime/sharedRuntime.hpp"
#include "runtime/stubRoutines.hpp"
#include "runtime/vframe.inline.hpp"
#include "runtime/vframeArray.hpp"
#include "utilities/copy.hpp"
#include "utilities/dtrace.hpp"
#include "utilities/events.hpp"
#include "utilities/hashtable.inline.hpp"
#include "utilities/macros.hpp"
#include "utilities/xmlstream.hpp"
#ifdef COMPILER1
#include "c1/c1_Runtime1.hpp"
#endif

#include "gc/rtgc/RTGC.hpp"

extern volatile int enable_rtgc_c1_barrier_hook;

using namespace RTGC;

static int g_mv_lock = (0);

volatile int RTGC::ENABLE_LOG = false;
volatile int RTGC::ENABLE_TRACE = false;

bool RTGC::isPublished(oopDesc* obj) {
  return true;
}

bool RTGC::lock_heap(oopDesc* obj) {
  if (!isPublished(obj)) return false;
  while (Atomic::xchg(&g_mv_lock, 1) != 0) { /* do spin. */ }
  return true;
}

void RTGC::unlock_heap(bool locked) {
  if (locked) {
    Atomic::release_store(&g_mv_lock, 0);
  }
}

void RTGC::add_referrer(oopDesc* obj, oopDesc* referrer) {
    rtgc_log(1, "add_ref: obj=%p(%s), referrer=%p\n", 
      obj, obj->klass()->name()->bytes(), referrer); 
}

void RTGC::remove_referrer(oopDesc* obj, oopDesc* referrer) {
    rtgc_log(1, "remove_ref: obj=%p(%s), referrer=%p\n",
      obj, obj->klass()->name()->bytes(), referrer); 
}




