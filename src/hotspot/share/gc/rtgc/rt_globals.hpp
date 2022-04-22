#ifndef SHARE_GC_RT_RT_GLOBALS_HPP
#define SHARE_GC_RT_RT_GLOBALS_HPP

#define GC_RT_FLAGS(develop,                                                \
                   develop_pd,                                              \
                   product,                                                 \
                   product_pd,                                              \
                   notproduct,                                              \
                   range,                                                   \
                   constraint)                                              \
                                                                            \
  product(bool, EnableRTGC, false,                                          \
          "Enable Reverse Tracking garbage collection(RTGC) method")        \
                                                                            \
  product(bool, RtNoDiscoverPhantom, false,                                 \
          "Disable discovering of phantom reference")                       \
                                                                            \
  product(bool, RtNoDirtyCardMarking, false,                                \
          "Disable dirty card marking")                                     \

#ifdef ASSERT
        const bool RtLateClearGcMark = true;
#else 
        const bool RtLateClearGcMark = false;
#endif

// end of GC_RT_FLAGS

#endif // SHARE_GC_RT_RT_GLOBALS_HPP
