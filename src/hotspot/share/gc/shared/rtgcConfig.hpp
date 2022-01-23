#ifndef SHARE_GC_RTGCCONFIG_HPP
#define SHARE_GC_RTGCCONFIG_HPP

#define USE_RTGC                      true

#define USE_RTGC_BARRIERSET_ASSEMBLER (true && USE_RTGC)
#define USE_RTGC_BARRIERSET_C1        (true && USE_RTGC)
#define USE_RTGC_BARRIERSET           (true && USE_RTGC)

#endif