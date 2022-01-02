

 2. [Run configure](#running-configure): \
    `bash configure`

bash configure --with-native-debug-symbols=external --with-debug-level=fastdebug \
  --with-jtreg=/workspaces/jdk-rtgc/jtreg-6.0

bash configure --with-jvm-variants=client \
  --with-native-debug-symbols=external --with-debug-level=fastdebug \
  --with-jtreg=/workspaces/jdk-rtgc/jtreg-6.0

 3. [Run make](#running-make): \
    `make images CONF=client`

 4. Verify your newly built JDK: \
    `./build/*/images/jdk/bin/java -version`

 5. [Run basic tests](##running-tests): \
    `make run-test-tier1`


6. Test file build
   javac test/rtgc/Main.java

7. Test 실행
   ./build/linux-x86_64-client-fastdebug/images/jdk/bin/java
   java -cp test/rtgc Main 2 1 // interpreter 
   java -cp test/rtgc Main 2 1000 // compile by c1_LIRGenerator 

8. Debugging 
  .vscode/launch.json "Launch Main" 실행.
  ENABLE_RTGC_STORE_HOOK = 1 (RTGC_HOOK enable)
  ENABLE_RTGC_STORE_TEST = 1 ( TEST Log 출력)


9. arraycopy 의 추가적인 최적화
LIR_OpArrayCopy::emit_code() 
   -> LIR_Assembler::emit_arraycopy() // c1_LIRAssembler_x86.cpp 
      StubRoutines::select_arraycopy_function() 함수를 통해
         stubGenerator_x86_64.cpp 의
            generate_*****_int_oop_copy 등에 의해 생성된 함수 호출


10. JVM SetField
- putfield
- putstatic
- clone() -> JVM_Clone()
- arraycopy()
- Unsafe::PutReference
- Unsafe::CompareAndSetReference
- Unsafe::ComapreAndExchageReference
- JNI jni_SetObjectField jni_SetObjectArrayElement


RuntimeDispatch<decorators,..> 를 이용하여 모드 Hook을 처리??

CollectedHeap 을 상속한 genCollectedHeap 가 default 선택
genCollectedHeap->initialize() 에서 CardTableBarrierSet 을 생성.
   BarrierSet::set_barrier_set() 호출.