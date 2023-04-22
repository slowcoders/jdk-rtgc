
## 1. Prepare external libraries
-  jtreg 
```sh
   git clone https://github.com/openjdk/jtreg.git -b jtreg-6.1+1
   bash build.sh --jdk /usr/lib/jvm/adoptopenjdk-14-hotspot-amd64
```

-  google test (소스만 필요)
```sh
   git clone https://github.com/google/googletest.git -b release-1.8.1
```
- ccache 설치
```sh
   brew install ccache
```
- VS Code C++ in Macosx
   https://code.visualstudio.com/docs/cpp/config-clang-mac

## 2. Run configure
### for macosx debug
```
bash configure --with-jvm-variants=client \
  --with-native-debug-symbols=external --with-debug-level=fastdebug \
  --with-jtreg=./jtreg-6.1 \
  --enable-ccache \
  --with-gtest=./googletest
# --with-toolchain-type=clang \
```
optmized mode (--with-debug-level=optimized)
```
bash configure --with-jvm-variants=client \
  --with-native-debug-symbols=external --with-debug-level=optimized \
  --with-jtreg=./jtreg-6.1 \
  --enable-ccache \
  --with-gtest=./googletest
# --with-toolchain-type=clang \
```
for release
```
bash configure --with-jvm-variants=client \
  --with-native-debug-symbols=external \
  --with-jtreg=./jtreg-6.1 \
  --enable-ccache \
  --with-gtest=./googletest
# --with-toolchain-type=clang \
```

### for linux (without ccache -> is docker problrem??)
```
bash configure --with-jvm-variants=client \
  --with-native-debug-symbols=external --with-debug-level=fastdebug \
  --with-jtreg=./jtreg-6.1 \
  --with-gtest=./googletest
```

## 3. Make Images
    `make images CONF=linux debug LOG_LEVEL=info`
    `make images CONF=macosx debug LOG_LEVEL=info`

### client CDS 사용 강제.
   `./build/macosx-x86_64-client-release/images/jdk/bin/java -Xshare:on`
### client CDS 생성.
   `./build/macosx-x86_64-client-release/images/jdk/bin/java -Xshare:dump`

## 4. Run basic tests
   `ulimit -c unlimited; make run-test-tier1 CONF=linux debug`
   `ulimit -c unlimited; make run-test-tier1 CONF=macosx debug`

   `make test CONF=macosx LOG_LEVEL=info TEST=jtreg:test/jdk:tier1`
   `make test CONF=macosx LOG_LEVEL=info TEST=jtreg:test/langtools:tier1`
   `make test CONF=macosx LOG_LEVEL=info TEST=jtreg:test/hotspot/jtreg:tier1`

## 5. Test tips
- test codedump 파일 자동삭제 방지<br>
  RunTests.gmk 파일을 아래와 같이 수정. <br>
```  
   -> JTREG_RETAIN ?= fail,error,hs_err_pid*
```

- logTrigger
```
   new java.util.concurrent.atomic.AtomicLong().compareAndExchange(
      0x876543DB876543DBL, 0x123456DB00000000L + (category << 24) + functions));` 
```

- module jdk.compiler does not export com.sun.tools.javac.xxx to unnamed module 오류 해결 방법
```
      --add-opens=jdk.compiler/com.sun.tools.javac.xxx=ALL-UNNAMED \
```

- retest.sh 실행 시 WhiteBox jni link error 발생 시, 아래 스크립트를 먼저 실행.
```
   rerun: ... 
      jdk.test.lib.helpers.ClassFileInstaller sun.hotspot.WhiteBox
```

- retest.sh 실행 시 log 가 출력되지 않으면, inheritIO() 추가.
```
   ProcessBuiler.inheritIO().start();
```

6. Test file build
   javac test/rtgc/Main.java

7. Custom Test 실행
   ./build/macosx-x86_64-client-fastdebug/images/jdk/bin/java -Xlog:gc=trace -cp test/rtgc Main 200 100000

8. Debugging 
  .vscode/launch.json "Launch Main" 실행.

==============================
Test summary 2023 04/22 rt-17 v4.1 tested
==============================
   TEST                                              TOTAL  PASS  FAIL ERROR   
>> jtreg:test/hotspot/jtreg:tier1                     1610  1588    18     4 <<
>> jtreg:test/jdk:tier1                               2062  2056     0     6 <<
>> jtreg:test/langtools:tier1                         4215  4199     0    16 <<
   jtreg:test/jaxp:tier1                                 0     0     0     0   
   jtreg:test/lib-test:tier1                             0     0     0     0   
==============================

==============================
Test summary 2023 04/19 rollback-to rt-17 v4.1
==============================
   TEST                                              TOTAL  PASS  FAIL ERROR   
>> jtreg:test/hotspot/jtreg:tier1                     1610  1584    21     5 <<
>> jtreg:test/jdk:tier1                               2062  2056     0     6 <<
>> jtreg:test/langtools:tier1                         4215  4190     0    25 <<
   jtreg:test/jaxp:tier1                                 0     0     0     0   
   jtreg:test/lib-test:tier1                             0     0     0     0   
==============================

==============================
Test summary 2022 03/21 Acyclic/AnchorList-LinkTrackable-Only
==============================
   TEST                                              TOTAL  PASS  FAIL ERROR   
>> jtreg:test/hotspot/jtreg:tier1                     1610  1572    29     9 <<
>> jtreg:test/jdk:tier1                               2062  2056     1     5 <<
>> jtreg:test/langtools:tier1                         4215  4196     1    18 <<
   jtreg:test/jaxp:tier1                                 0     0     0     0   
   jtreg:test/lib-test:tier1                             0     0     0     0   
==============================

Test summary 2022 12/11 UseModifyFlag
==============================
   TEST                                              TOTAL  PASS  FAIL ERROR   
>> jtreg:test/hotspot/jtreg:tier1                     1610  1583    21     6 <<
>> jtreg:test/jdk:tier1                               2062  2054     2     6 <<
>> jtreg:test/langtools:tier1                         4215  4191     0    24 <<
   jtreg:test/jaxp:tier1                                 0     0     0     0   
   jtreg:test/lib-test:tier1                             0     0     0     0    
==============================
TEST FAILURE

==============================
Test summary 2022 11/12 Opt Anchor List
==============================
>> jtreg:test/hotspot/jtreg:tier1                     1610  1585    22     3 <<
>> jtreg:test/jdk:tier1                               2062  2058     0     4 <<
>> jtreg:test/langtools:tier1                         4215  4199     0    16 <<
   jtreg:test/jaxp:tier1                                 0     0     0     0   
   jtreg:test/lib-test:tier1                             0     0     0     0 
==============================

==============================
Test summary 2022 11/09 Full RTGC
==============================
   TEST                                              TOTAL  PASS  FAIL ERROR   
>> jtreg:test/hotspot/jtreg:tier1                     1610  1586    19     5 <<
>> jtreg:test/jdk:tier1                               2062  2060     0     2 <<
>> jtreg:test/langtools:tier1                         4215  4200     0    15 <<
   jtreg:test/jaxp:tier1                                 0     0     0     0   
   jtreg:test/lib-test:tier1                             0     0     0     0   
==============================


==============================
Test summary 2022 10/20 Cross-Check
==============================
   TEST                                              TOTAL  PASS  FAIL ERROR   
>> jtreg:test/hotspot/jtreg:tier1                     1610  1587    19     4 <<
>> jtreg:test/jdk:tier1                               2062  2059     0     3 <<
>> jtreg:test/langtools:tier1                         4215  4201     0    14 <<
   jtreg:test/jaxp:tier1                                 0     0     0     0   
   jtreg:test/lib-test:tier1                             0     0     0     0   
==============================

==============================
Test summary 2022 10/09
==============================
   TEST                                              TOTAL  PASS  FAIL ERROR   
>> jtreg:test/hotspot/jtreg:tier1                     1610  1582    25     3 <<
>> jtreg:test/jdk:tier1                               2062  2058     0     4 <<
>> jtreg:test/langtools:tier1                         4215  4200     0    15 <<
   jtreg:test/jaxp:tier1                                 0     0     0     0   
   jtreg:test/lib-test:tier1                             0     0     0     0   
==============================

==============================
Test summary 2022 07/20
==============================
>> jtreg:test/hotspot/jtreg:tier1                     1610  1586    21     3 <<
>> jtreg:test/jdk:tier1                               2062  2055     3     4 <<
>> jtreg:test/langtools:tier1                         4214  4199     0    15 <<
   jtreg:test/jaxp:tier1                                 0     0     0     0   
   jtreg:test/lib-test:tier1                             0     0     0     0   

   TEST                                              TOTAL  PASS  FAIL ERROR   
>> jtreg:test/hotspot/jtreg:tier1                     1610  1582    24     4 <<
>> jtreg:test/jdk:tier1                               2062  2054     3     5 <<
>> jtreg:test/langtools:tier1                         4215  4205     0    10 <<
   jtreg:test/jaxp:tier1                                 0     0     0     0   
   jtreg:test/lib-test:tier1                             0     0     0     0 

==============================
Test summary Orignal version
==============================
   TEST                                              TOTAL  PASS  FAIL ERROR   
>> jtreg:test/hotspot/jtreg:tier1                     1610  1593    16     1 <<
>> jtreg:test/jdk:tier1                               2062  2048     6     8 <<
>> jtreg:test/langtools:tier1                         4019  3874   136     9 <<
   jtreg:test/jaxp:tier1                                 0     0     0     0   
   jtreg:test/lib-test:tier1                             0     0     0     0   
==============================


  yg->process_roots(ScanningOption SO_ScavengeCodeCache, 
                OopClosure* strong_roots = DefNewScanClosure,
                CLDClosure* strong_cld_closure = cld_closure, 
                CLDClosure* weak_cld_closure = cld_closure, 
                CodeBlobToOopClosure* code_roots = &MarkingCodeBlobClosure(root_closure,
                                                   CodeBlobToOopClosure::FixRelocations);
                
  old_g->process_roots(ScanningOption GenCollectedHeap::SO_None,
               OopClosure* strong_roots = MarkSweep::FollowRootClosure(root_closure),
               CLDClosure* strong_cld_closure = CLDToOopClosure
               CLDClosure* weak_cld_closure = only_strong_roots ? NULL : cld_closure,
               CodeBlobToOopClosure* code_roots = &MarkingCodeBlobClosure(root_closure, is_adjust_phase)


## Young Generation Scan
  1) GC 시작 전에 old-G 에 allocate 된 객체(=recycle)를 trackable 로 marking 하고, Unsafe 검사를 한다.
  2) Stack 및 root 객체를 marking 한다.
  3) evacuate_followers.do_void()
  4) YoungRoot 객체를 마킹한다. 
    rtHeap::iterate_younger_gen_roots()
    + evacuate_followers.do_void()
  4) rtHeap::process_weak_soft_references()
    // YG-GC 시에서는 weak/soft reference 는 garbage 처리하지 않는다.
  5) rtHeap::process_final_phantom_references()
    -> rtHeap__clear_garbage_young_roots()
      -> _rtgc.g_pGarbageProcessor->collectAndDestroyGarbage(is_full_gc);
    GC 종료 후 marking 된 phantom_ref 객체의 주소 변경.
  6) weak-oop clean-up. WeakProcessor::weak_oops_do

evacuate_followers.do_void() {
   evacuated 객체: 
      - to() 영역으로 새로 옮겨지 객체
      - old_G 로 옮겨진 객체-
      - recycled 객체 .
    이때 Resurrection 이 발생할 수 있다.
}

## old generation scan
  0) modified field 의 anchorList 를 update 한다.
  1) weak/softReference 를 referent 의 anchorList 에서 임시 제거.
     rtHeapEx::break_reference_links(policy);
  1-1) GC 시작 전에 old-G 에 allocate 된 객체(=recycle)를 trackable 로 marking 하고, Unsafe 검사를 한다.
  2) Stack 및 root 객체를 marking 한다.
  3) untracked object 의 anchorList 를 등록한다.
     rtHeap::oop_recycled_iterate(&untracked_closure);  // resurrection 전용으로 활용한다.??
  4) young object 를 marking 한다.
     rtHeap::iterate_younger_gen_roots(NULL, true);
  5) follow_stack_closure.do_void();
  6) rtHeap::process_weak_soft_references(&keep_alive, &follow_stack_closure, true);
      -> for (RefIterator<true> iter(g_softList); (ref = iter.next_ref(DetectGarbageRef)) != NULL; )
         -> iter.clear_weak_soft_garbage_referent();
      -> for (RefIterator<true> iter(g_weakList); (ref = iter.next_ref(DetectGarbageRef)) != NULL; )
         -> iter.clear_weak_soft_garbage_referent();

  6) rtHeap::process_final_phantom_references(&keep_alive, &follow_stack_closure, true);
      -> complete_gc->do_void();
      -> rtHeap__clear_garbage_young_roots(is_full_gc);
  7) WeakProcessor::weak_oops_do(&is_alive, &do_nothing_cl);


## rtgc 개요.
### On Compile
 * c1_LIRgenerator
### At runtime
 * mark_trackable object that allocated at tenured-heap (huge array)
 * anchorList 변경 정보 저장.
   --> modified anchor + modified field
   content_modified, field_modified.
 * refCount 변경 또는 변경 정보 저장.
 * FinalReference 의 referent 를 finalier-reachable marking.
 * referenceList 구성. -> RTGC는 모든 객체를 scan 하지 않으므로 별도 list 구성 필수.
 * classLoaderData.cpp
 * rtThreadLocal.cpp
 * jniHandles.cpp
### Prepare to tracking
 * modified anchor 의 anchor-list 갱신
 * refCount 갱신
 * weak/soft-referent 에서 Reference Anchor 임시 제거. (mark_unrackable 로 대체 가능)

### Middle of marking
 * modified anchor 의 follower 중 young-g marking.
   SerialGC 의 경우, DirtyCardToOopClosure::walk_mem_region 이용 가능
   참고) SerialGC RemSet : 1 byte dirty flag per 1024 bytes.
 * garbage resurrection.
 * rtgc_fill_dead_space
   dead space -> as garbage. 
 * follow_klass, follow_cld, follow_object, follow_stack

### At finsh
 * adjust anchor-list pointers.
 * clear weak handle
 * remove garbage ClassLoaderData (count of trackable = ref-count of cld)

### MISC
 * JSA 파일
   metaspaceShared.cpp

### About G1GC 
 * At the end of a collection, G1 chooses the regions to be collected in the next collection (the collection set). The collection set will contain young regions.
 * https://www.oracle.com/technical-resources/articles/java/g1gc.html
 * https://thinkground.studio/일반적인-gc-내용과-g1gc-garbage-first-garbage-collector-내용/ 
