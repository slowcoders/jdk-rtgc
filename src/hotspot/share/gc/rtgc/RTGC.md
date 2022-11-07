
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
* for macosx
```
bash configure --with-jvm-variants=client \
  --enable-ccache \
  --with-native-debug-symbols=external --with-debug-level=fastdebug \
  --with-jtreg=./jtreg-6.1 \
  --with-gtest=./googletest

  --with-toolchain-type=clang \
```
* for linux container
```
bash configure --with-jvm-variants=client \
  --with-native-debug-symbols=external --with-debug-level=fastdebug \
  --with-jtreg=./jtreg-6.1 \
  --with-gtest=./googletest
```

## 3. Make Images
    `make images CONF=linux debug LOG_LEVEL=info`
    `make images CONF=macosx debug LOG_LEVEL=info`

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
  ENABLE_RTGC_STORE_HOOK = 1 (RTGC_HOOK enable)
  ENABLE_RTGC_STORE_TEST = 1 ( TEST Log 출력)


==============================
Test summary 2022 11/04 No-Cross-Check
==============================
   TEST                                              TOTAL  PASS  FAIL ERROR   
>> jtreg:test/hotspot/jtreg:tier1                     1610  1583    19     8 <<
>> jtreg:test/jdk:tier1                               2062  2058     1     3 <<
>> jtreg:test/langtools:tier1                         4215  4202     0    13 <<
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
