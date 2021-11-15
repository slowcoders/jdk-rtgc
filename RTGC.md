

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
