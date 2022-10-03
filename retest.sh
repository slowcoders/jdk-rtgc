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


pushd /Users/zeedh/slowcoders/jdk-rtgc/build/macosx-x86_64-client-fastdebug/test-support/jtreg_test_jdk_java_lang_ref_ReferenceEnqueue_java/scratch/0 && \
HOME=/Users/zeedh \
JDK8_HOME=/Library/Java/JavaVirtualMachines/adoptopenjdk-16.jdk/Contents/Home \
LANG=en_US.UTF-8 \
LC_ALL=C \
PATH=/bin:/usr/bin:/usr/sbin \
TEST_IMAGE_DIR=/Users/zeedh/slowcoders/jdk-rtgc/build/macosx-x86_64-client-fastdebug/images/test \
    /Users/zeedh/slowcoders/jdk-rtgc/build/macosx-x86_64-client-fastdebug/images/jdk/bin/java \
        -Dtest.vm.opts='-Xmx768m -XX:MaxRAMPercentage=6.25 -Djava.io.tmpdir=/Users/zeedh/slowcoders/jdk-rtgc/build/macosx-x86_64-client-fastdebug/test-support/jtreg_test_jdk_java_lang_ref_ReferenceEnqueue_java/tmp -ea -esa' \
        -Dtest.tool.vm.opts='-J-Xmx768m -J-XX:MaxRAMPercentage=6.25 -J-Djava.io.tmpdir=/Users/zeedh/slowcoders/jdk-rtgc/build/macosx-x86_64-client-fastdebug/test-support/jtreg_test_jdk_java_lang_ref_ReferenceEnqueue_java/tmp -J-ea -J-esa' \
        -Dtest.compiler.opts= \
        -Dtest.java.opts= \
        -Dtest.jdk=/Users/zeedh/slowcoders/jdk-rtgc/build/macosx-x86_64-client-fastdebug/images/jdk \
        -Dcompile.jdk=/Users/zeedh/slowcoders/jdk-rtgc/build/macosx-x86_64-client-fastdebug/images/jdk \
        -Dtest.timeout.factor=4.0 \
        -Dtest.nativepath=/Users/zeedh/slowcoders/jdk-rtgc/build/macosx-x86_64-client-fastdebug/images/test/jdk/jtreg/native \
        -Dtest.root=/Users/zeedh/slowcoders/jdk-rtgc/test/jdk \
        -Dtest.name=java/lang/ref/ReferenceEnqueue.java \
        -Dtest.file=/Users/zeedh/slowcoders/jdk-rtgc/test/jdk/java/lang/ref/ReferenceEnqueue.java \
        -Dtest.src=/Users/zeedh/slowcoders/jdk-rtgc/test/jdk/java/lang/ref \
        -Dtest.src.path=/Users/zeedh/slowcoders/jdk-rtgc/test/jdk/java/lang/ref \
        -Dtest.classes=/Users/zeedh/slowcoders/jdk-rtgc/build/macosx-x86_64-client-fastdebug/test-support/jtreg_test_jdk_java_lang_ref_ReferenceEnqueue_java/classes/0/java/lang/ref/ReferenceEnqueue.d \
        -Dtest.class.path=/Users/zeedh/slowcoders/jdk-rtgc/build/macosx-x86_64-client-fastdebug/test-support/jtreg_test_jdk_java_lang_ref_ReferenceEnqueue_java/classes/0/java/lang/ref/ReferenceEnqueue.d \
        -Dtest.class.path.prefix=/Users/zeedh/slowcoders/jdk-rtgc/build/macosx-x86_64-client-fastdebug/test-support/jtreg_test_jdk_java_lang_ref_ReferenceEnqueue_java/classes/0/java/lang/ref/ReferenceEnqueue.d:/Users/zeedh/slowcoders/jdk-rtgc/test/jdk/java/lang/ref \
        -classpath /Users/zeedh/slowcoders/jdk-rtgc/build/macosx-x86_64-client-fastdebug/test-support/jtreg_test_jdk_java_lang_ref_ReferenceEnqueue_java/classes/0/java/lang/ref/ReferenceEnqueue.d:/Users/zeedh/slowcoders/jdk-rtgc/test/jdk/java/lang/ref:/Users/zeedh/slowcoders/jdk-rtgc/jtreg-6.1/lib/javatest.jar:/Users/zeedh/slowcoders/jdk-rtgc/jtreg-6.1/lib/jtreg.jar \
        ReferenceEnqueue