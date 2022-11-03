#ifndef SHARE_GC_RTGC_CLD_CLEANER_HPP
#define SHARE_GC_RTGC_CLD_CLEANER_HPP

#include "utilities/macros.hpp"
#include "memory/allStatic.hpp"
#include "memory/referenceType.hpp"
#include "gc/shared/gc_globals.hpp"

#include "rtgcDebug.hpp"

class RtYoungRootClosure;

namespace rtCLDCleaner {
  void initialize();
  void lock_cld(oopDesc* obj);
  void unlock_cld(oopDesc* obj);
  void resurrect_cld(oopDesc* obj);
  void clear_cld_locks(RtYoungRootClosure* tenuredScanner);
  void collect_garbage_clds(RtYoungRootClosure* tenuredScanner);
};

#endif