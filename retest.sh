#         --add-modules jdk.compiler \
#        --add-modules java.base \
#        --add-exports jdk.compiler/com.sun.tools.javac.main=ALL-UNNAMED \
#        --add-exports jdk.compiler/com.sun.tools.sjavac=ALL-UNNAMED \
#        --add-exports jdk.compiler/com.sun.tools.sjavac.server=ALL-UNNAMED \
#       -XX:+UnlockExperimentalVMOptions \
#       -XX:AbortVMOnExceptionMessage='compiler/c2/Test7190310$1' \
#        -XX:AbortVMOnExceptionMessage='[B' \
#        --patch-module java.base=/Users/zeedh/slowcoders/jdk-rtgc/build/macosx-x86_64-client-fastdebug/test-support/jtreg_test_hotspot_jtreg_gc_logging_TestMetaSpaceLog_java/patches/java.base \

# runtime/ErrorHandling/TestGZippedHeapDumpOnOutOfMemoryError
# runtime/ErrorHandling/TestHeapDumpPath
# runtime/Unsafe/InternalErrorTest
# runtime/modules/ModuleStress/ModuleStress
# runtime/modules/ClassLoaderNoUnnamedModuleTest
# gc/logging/TestMetaSpaceLog
# compiler/classUnloading/methodUnloading/TestOverloadCompileQueues
# runtime/logging/VtablesTest
# compiler/intrinsics/bigInteger/TestMulAdd
# gc/logging/TestMetaSpaceLog
# jdk/internal/reflect/CallerSensitive/CheckCSMs
# jdk/java/math/BigInteger/largeMemory/SymmetricRangeTests


pushd /Users/zeedh/slowcoders/jdk-rtgc/build/macosx-x86_64-client-fastdebug/test-support/jtreg_test_jdk_java_math_BigInteger_largeMemory_SymmetricRangeTests_java/scratch/0 && \
HOME=/Users/zeedh \
JDK8_HOME=/Library/Java/JavaVirtualMachines/adoptopenjdk-16.jdk/Contents/Home \
LANG=en_US.UTF-8 \
LC_ALL=C \
PATH=/bin:/usr/bin:/usr/sbin \
TEST_IMAGE_DIR=/Users/zeedh/slowcoders/jdk-rtgc/build/macosx-x86_64-client-fastdebug/images/test \
CLASSPATH=/Users/zeedh/slowcoders/jdk-rtgc/build/macosx-x86_64-client-fastdebug/test-support/jtreg_test_jdk_java_math_BigInteger_largeMemory_SymmetricRangeTests_java/classes/0/java/math/BigInteger/largeMemory/SymmetricRangeTests.d:/Users/zeedh/slowcoders/jdk-rtgc/test/jdk/java/math/BigInteger/largeMemory:/Users/zeedh/slowcoders/jdk-rtgc/build/macosx-x86_64-client-fastdebug/test-support/jtreg_test_jdk_java_math_BigInteger_largeMemory_SymmetricRangeTests_java/classes/0/test/lib:/Users/zeedh/slowcoders/jdk-rtgc/test/lib:/Users/zeedh/slowcoders/jdk-rtgc/jtreg-6.1/lib/javatest.jar:/Users/zeedh/slowcoders/jdk-rtgc/jtreg-6.1/lib/jtreg.jar \
    /Users/zeedh/slowcoders/jdk-rtgc/build/macosx-x86_64-client-fastdebug/images/jdk/bin/java \
        -Dtest.vm.opts='-Xmx768m -XX:MaxRAMPercentage=6.25 -Djava.io.tmpdir=/Users/zeedh/slowcoders/jdk-rtgc/build/macosx-x86_64-client-fastdebug/test-support/jtreg_test_jdk_java_math_BigInteger_largeMemory_SymmetricRangeTests_java/tmp -ea -esa' \
        -Dtest.tool.vm.opts='-J-Xmx768m -J-XX:MaxRAMPercentage=6.25 -J-Djava.io.tmpdir=/Users/zeedh/slowcoders/jdk-rtgc/build/macosx-x86_64-client-fastdebug/test-support/jtreg_test_jdk_java_math_BigInteger_largeMemory_SymmetricRangeTests_java/tmp -J-ea -J-esa' \
        -Dtest.compiler.opts= \
        -Dtest.java.opts= \
        -Dtest.jdk=/Users/zeedh/slowcoders/jdk-rtgc/build/macosx-x86_64-client-fastdebug/images/jdk \
        -Dcompile.jdk=/Users/zeedh/slowcoders/jdk-rtgc/build/macosx-x86_64-client-fastdebug/images/jdk \
        -Dtest.timeout.factor=4.0 \
        -Dtest.nativepath=/Users/zeedh/slowcoders/jdk-rtgc/build/macosx-x86_64-client-fastdebug/images/test/jdk/jtreg/native \
        -Dtest.root=/Users/zeedh/slowcoders/jdk-rtgc/test/jdk \
        -Dtest.name=java/math/BigInteger/largeMemory/SymmetricRangeTests.java \
        -Dtest.file=/Users/zeedh/slowcoders/jdk-rtgc/test/jdk/java/math/BigInteger/largeMemory/SymmetricRangeTests.java \
        -Dtest.src=/Users/zeedh/slowcoders/jdk-rtgc/test/jdk/java/math/BigInteger/largeMemory \
        -Dtest.src.path=/Users/zeedh/slowcoders/jdk-rtgc/test/jdk/java/math/BigInteger/largeMemory:/Users/zeedh/slowcoders/jdk-rtgc/test/lib \
        -Dtest.classes=/Users/zeedh/slowcoders/jdk-rtgc/build/macosx-x86_64-client-fastdebug/test-support/jtreg_test_jdk_java_math_BigInteger_largeMemory_SymmetricRangeTests_java/classes/0/java/math/BigInteger/largeMemory/SymmetricRangeTests.d \
        -Dtest.class.path=/Users/zeedh/slowcoders/jdk-rtgc/build/macosx-x86_64-client-fastdebug/test-support/jtreg_test_jdk_java_math_BigInteger_largeMemory_SymmetricRangeTests_java/classes/0/java/math/BigInteger/largeMemory/SymmetricRangeTests.d:/Users/zeedh/slowcoders/jdk-rtgc/build/macosx-x86_64-client-fastdebug/test-support/jtreg_test_jdk_java_math_BigInteger_largeMemory_SymmetricRangeTests_java/classes/0/test/lib \
        -Dtest.class.path.prefix=/Users/zeedh/slowcoders/jdk-rtgc/build/macosx-x86_64-client-fastdebug/test-support/jtreg_test_jdk_java_math_BigInteger_largeMemory_SymmetricRangeTests_java/classes/0/java/math/BigInteger/largeMemory/SymmetricRangeTests.d:/Users/zeedh/slowcoders/jdk-rtgc/test/jdk/java/math/BigInteger/largeMemory:/Users/zeedh/slowcoders/jdk-rtgc/build/macosx-x86_64-client-fastdebug/test-support/jtreg_test_jdk_java_math_BigInteger_largeMemory_SymmetricRangeTests_java/classes/0/test/lib \
        -Xmx768m \
        -XX:MaxRAMPercentage=6.25 \
        -Djava.io.tmpdir=/Users/zeedh/slowcoders/jdk-rtgc/build/macosx-x86_64-client-fastdebug/test-support/jtreg_test_jdk_java_math_BigInteger_largeMemory_SymmetricRangeTests_java/tmp \
        -ea \
        -esa \
        -Djava.library.path=/Users/zeedh/slowcoders/jdk-rtgc/build/macosx-x86_64-client-fastdebug/images/test/jdk/jtreg/native \
        -Xmx8g \
        -XX:+CompactStrings \
        com.sun.javatest.regtest.agent.MainWrapper /Users/zeedh/slowcoders/jdk-rtgc/build/macosx-x86_64-client-fastdebug/test-support/jtreg_test_jdk_java_math_BigInteger_largeMemory_SymmetricRangeTests_java/java/math/BigInteger/largeMemory/SymmetricRangeTests.d/main.0.jta