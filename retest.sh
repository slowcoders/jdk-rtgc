#         --add-modules jdk.compiler \
#        --add-modules java.base \
#        --add-exports jdk.compiler/com.sun.tools.javac.main=ALL-UNNAMED \
#        --add-exports jdk.compiler/com.sun.tools.sjavac=ALL-UNNAMED \
#        --add-exports jdk.compiler/com.sun.tools.sjavac.server=ALL-UNNAMED \
# -XX:+UnlockExperimentalVMOptions 
# -XX:AbortVMOnExceptionMessage='compiler/c2/Test7190310$1'
# compiler/uncommontrap/TestDeoptOOM
#
# gc/logging/TestMetaSpaceLog
# runtime/ErrorHandling/TestGZippedHeapDumpOnOutOfMemoryError
# runtime/ErrorHandling/TestHeapDumpPath
# runtime/Unsafe/InternalErrorTest
# runtime/modules/ModuleStress/ModuleStress
# runtime/modules/ClassLoaderNoUnnamedModuleTest
# runtime/logging/VtablesTest

pushd /Users/zeedh/slowcoders/jdk-rtgc/build/macosx-x86_64-client-fastdebug/test-support/jtreg_test_hotspot_jtreg_compiler_classUnloading_methodUnloading_TestMethodUnloading_java/scratch/0 && \
HOME=/Users/zeedh \
JDK8_HOME=/Library/Java/JavaVirtualMachines/adoptopenjdk-16.jdk/Contents/Home \
LANG=en_US.UTF-8 \
LC_ALL=C \
PATH=/bin:/usr/bin:/usr/sbin \
TEST_IMAGE_DIR=/Users/zeedh/slowcoders/jdk-rtgc/build/macosx-x86_64-client-fastdebug/images/test \
CLASSPATH=/Users/zeedh/slowcoders/jdk-rtgc/build/macosx-x86_64-client-fastdebug/test-support/jtreg_test_hotspot_jtreg_compiler_classUnloading_methodUnloading_TestMethodUnloading_java/classes/0/compiler/classUnloading/methodUnloading/TestMethodUnloading.d:/Users/zeedh/slowcoders/jdk-rtgc/test/hotspot/jtreg/compiler/classUnloading/methodUnloading:/Users/zeedh/slowcoders/jdk-rtgc/build/macosx-x86_64-client-fastdebug/test-support/jtreg_test_hotspot_jtreg_compiler_classUnloading_methodUnloading_TestMethodUnloading_java/classes/0/test/lib:/Users/zeedh/slowcoders/jdk-rtgc/test/lib:/Users/zeedh/slowcoders/jdk-rtgc/build/macosx-x86_64-client-fastdebug/test-support/jtreg_test_hotspot_jtreg_compiler_classUnloading_methodUnloading_TestMethodUnloading_java/classes/0:/Users/zeedh/slowcoders/jdk-rtgc/test/hotspot/jtreg:/Users/zeedh/slowcoders/jdk-rtgc/jtreg-6.1/lib/javatest.jar:/Users/zeedh/slowcoders/jdk-rtgc/jtreg-6.1/lib/jtreg.jar \
    /Users/zeedh/slowcoders/jdk-rtgc/build/macosx-x86_64-client-fastdebug/images/jdk/bin/java \
        --patch-module java.base=/Users/zeedh/slowcoders/jdk-rtgc/build/macosx-x86_64-client-fastdebug/test-support/jtreg_test_hotspot_jtreg_compiler_classUnloading_methodUnloading_TestMethodUnloading_java/patches/java.base \
        -Dtest.vm.opts='-XX:MaxRAMPercentage=6.25 -Djava.io.tmpdir=/Users/zeedh/slowcoders/jdk-rtgc/build/macosx-x86_64-client-fastdebug/test-support/jtreg_test_hotspot_jtreg_compiler_classUnloading_methodUnloading_TestMethodUnloading_java/tmp' \
        -Dtest.tool.vm.opts='-J-XX:MaxRAMPercentage=6.25 -J-Djava.io.tmpdir=/Users/zeedh/slowcoders/jdk-rtgc/build/macosx-x86_64-client-fastdebug/test-support/jtreg_test_hotspot_jtreg_compiler_classUnloading_methodUnloading_TestMethodUnloading_java/tmp' \
        -Dtest.compiler.opts= \
        -Dtest.java.opts= \
        -Dtest.jdk=/Users/zeedh/slowcoders/jdk-rtgc/build/macosx-x86_64-client-fastdebug/images/jdk \
        -Dcompile.jdk=/Users/zeedh/slowcoders/jdk-rtgc/build/macosx-x86_64-client-fastdebug/images/jdk \
        -Dtest.timeout.factor=4.0 \
        -Dtest.nativepath=/Users/zeedh/slowcoders/jdk-rtgc/build/macosx-x86_64-client-fastdebug/images/test/hotspot/jtreg/native \
        -Dtest.root=/Users/zeedh/slowcoders/jdk-rtgc/test/hotspot/jtreg \
        -Dtest.name=compiler/classUnloading/methodUnloading/TestMethodUnloading.java \
        -Dtest.file=/Users/zeedh/slowcoders/jdk-rtgc/test/hotspot/jtreg/compiler/classUnloading/methodUnloading/TestMethodUnloading.java \
        -Dtest.src=/Users/zeedh/slowcoders/jdk-rtgc/test/hotspot/jtreg/compiler/classUnloading/methodUnloading \
        -Dtest.src.path=/Users/zeedh/slowcoders/jdk-rtgc/test/hotspot/jtreg/compiler/classUnloading/methodUnloading:/Users/zeedh/slowcoders/jdk-rtgc/test/lib:/Users/zeedh/slowcoders/jdk-rtgc/test/hotspot/jtreg \
        -Dtest.classes=/Users/zeedh/slowcoders/jdk-rtgc/build/macosx-x86_64-client-fastdebug/test-support/jtreg_test_hotspot_jtreg_compiler_classUnloading_methodUnloading_TestMethodUnloading_java/classes/0/compiler/classUnloading/methodUnloading/TestMethodUnloading.d \
        -Dtest.class.path=/Users/zeedh/slowcoders/jdk-rtgc/build/macosx-x86_64-client-fastdebug/test-support/jtreg_test_hotspot_jtreg_compiler_classUnloading_methodUnloading_TestMethodUnloading_java/classes/0/compiler/classUnloading/methodUnloading/TestMethodUnloading.d:/Users/zeedh/slowcoders/jdk-rtgc/build/macosx-x86_64-client-fastdebug/test-support/jtreg_test_hotspot_jtreg_compiler_classUnloading_methodUnloading_TestMethodUnloading_java/classes/0/test/lib:/Users/zeedh/slowcoders/jdk-rtgc/build/macosx-x86_64-client-fastdebug/test-support/jtreg_test_hotspot_jtreg_compiler_classUnloading_methodUnloading_TestMethodUnloading_java/classes/0 \
        -Dtest.class.path.prefix=/Users/zeedh/slowcoders/jdk-rtgc/build/macosx-x86_64-client-fastdebug/test-support/jtreg_test_hotspot_jtreg_compiler_classUnloading_methodUnloading_TestMethodUnloading_java/classes/0/compiler/classUnloading/methodUnloading/TestMethodUnloading.d:/Users/zeedh/slowcoders/jdk-rtgc/test/hotspot/jtreg/compiler/classUnloading/methodUnloading:/Users/zeedh/slowcoders/jdk-rtgc/build/macosx-x86_64-client-fastdebug/test-support/jtreg_test_hotspot_jtreg_compiler_classUnloading_methodUnloading_TestMethodUnloading_java/classes/0/test/lib:/Users/zeedh/slowcoders/jdk-rtgc/build/macosx-x86_64-client-fastdebug/test-support/jtreg_test_hotspot_jtreg_compiler_classUnloading_methodUnloading_TestMethodUnloading_java/classes/0 \
        -Dtest.modules=java.base/jdk.internal.misc \
        --add-modules java.base \
        --add-exports java.base/jdk.internal.misc=ALL-UNNAMED \
        -XX:MaxRAMPercentage=6.25 \
        -Djava.io.tmpdir=/Users/zeedh/slowcoders/jdk-rtgc/build/macosx-x86_64-client-fastdebug/test-support/jtreg_test_hotspot_jtreg_compiler_classUnloading_methodUnloading_TestMethodUnloading_java/tmp \
        -Djava.library.path=/Users/zeedh/slowcoders/jdk-rtgc/build/macosx-x86_64-client-fastdebug/images/test/hotspot/jtreg/native \
        -Xbootclasspath/a:. \
        -XX:+IgnoreUnrecognizedVMOptions \
        -XX:+UnlockDiagnosticVMOptions \
        -XX:+WhiteBoxAPI \
        -XX:-BackgroundCompilation \
        -XX:-UseCompressedOops \
        -XX:CompileCommand=compileonly,compiler.classUnloading.methodUnloading.TestMethodUnloading::doWork \
        com.sun.javatest.regtest.agent.MainWrapper /Users/zeedh/slowcoders/jdk-rtgc/build/macosx-x86_64-client-fastdebug/test-support/jtreg_test_hotspot_jtreg_compiler_classUnloading_methodUnloading_TestMethodUnloading_java/compiler/classUnloading/methodUnloading/TestMethodUnloading.d/main.0.jta