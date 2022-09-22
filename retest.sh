#         --add-modules jdk.compiler \
#        --add-exports jdk.compiler/com.sun.tools.javac.main=ALL-UNNAMED \
#        --add-exports jdk.compiler/com.sun.tools.sjavac=ALL-UNNAMED \
#        --add-exports jdk.compiler/com.sun.tools.sjavac.server=ALL-UNNAMED \

pushd /Users/zeedh/slowcoders/jdk-rtgc/build/macosx-x86_64-client-fastdebug/test-support/jtreg_test_langtools_tools_sjavac_IncludeExcludePatterns_java/scratch/0 && \
HOME=/Users/zeedh \
JDK8_HOME=/Library/Java/JavaVirtualMachines/adoptopenjdk-16.jdk/Contents/Home \
LANG=en_US.UTF-8 \
LC_ALL=C \
PATH=/bin:/usr/bin:/usr/sbin \
TEST_IMAGE_DIR=/Users/zeedh/slowcoders/jdk-rtgc/build/macosx-x86_64-client-fastdebug/images/test \
    /Users/zeedh/slowcoders/jdk-rtgc/build/macosx-x86_64-client-fastdebug/images/jdk/bin/java \
        -XX:+UnlockExperimentalVMOptions \
        --add-modules jdk.compiler \
        --add-exports jdk.compiler/com.sun.tools.javac.main=ALL-UNNAMED \
        --add-exports jdk.compiler/com.sun.tools.sjavac=ALL-UNNAMED \
        --add-exports jdk.compiler/com.sun.tools.sjavac.server=ALL-UNNAMED \
        -Dtest.vm.opts='-Xmx768m -XX:MaxRAMPercentage=6.25 -Djava.io.tmpdir=/Users/zeedh/slowcoders/jdk-rtgc/build/macosx-x86_64-client-fastdebug/test-support/jtreg_test_langtools_tools_sjavac_IncludeExcludePatterns_java/tmp -ea -esa' \
        -Dtest.tool.vm.opts='-J-Xmx768m -J-XX:MaxRAMPercentage=6.25 -J-Djava.io.tmpdir=/Users/zeedh/slowcoders/jdk-rtgc/build/macosx-x86_64-client-fastdebug/test-support/jtreg_test_langtools_tools_sjavac_IncludeExcludePatterns_java/tmp -J-ea -J-esa' \
        -Dtest.compiler.opts= \
        -Dtest.java.opts= \
        -Dtest.jdk=/Users/zeedh/slowcoders/jdk-rtgc/build/macosx-x86_64-client-fastdebug/images/jdk \
        -Dcompile.jdk=/Users/zeedh/slowcoders/jdk-rtgc/build/macosx-x86_64-client-fastdebug/images/jdk \
        -Dtest.timeout.factor=4.0 \
        -Dtest.root=/Users/zeedh/slowcoders/jdk-rtgc/test/langtools \
        -Dtest.name=tools/sjavac/IncludeExcludePatterns.java \
        -Dtest.file=/Users/zeedh/slowcoders/jdk-rtgc/test/langtools/tools/sjavac/IncludeExcludePatterns.java \
        -Dtest.src=/Users/zeedh/slowcoders/jdk-rtgc/test/langtools/tools/sjavac \
        -Dtest.src.path=/Users/zeedh/slowcoders/jdk-rtgc/test/langtools/tools/sjavac:/Users/zeedh/slowcoders/jdk-rtgc/test/langtools/tools/lib \
        -Dtest.classes=/Users/zeedh/slowcoders/jdk-rtgc/build/macosx-x86_64-client-fastdebug/test-support/jtreg_test_langtools_tools_sjavac_IncludeExcludePatterns_java/classes/0/tools/sjavac/IncludeExcludePatterns.d \
        -Dtest.class.path=/Users/zeedh/slowcoders/jdk-rtgc/build/macosx-x86_64-client-fastdebug/test-support/jtreg_test_langtools_tools_sjavac_IncludeExcludePatterns_java/classes/0/tools/sjavac/IncludeExcludePatterns.d:/Users/zeedh/slowcoders/jdk-rtgc/build/macosx-x86_64-client-fastdebug/test-support/jtreg_test_langtools_tools_sjavac_IncludeExcludePatterns_java/classes/0/tools/lib \
        -Dtest.class.path.prefix=/Users/zeedh/slowcoders/jdk-rtgc/build/macosx-x86_64-client-fastdebug/test-support/jtreg_test_langtools_tools_sjavac_IncludeExcludePatterns_java/classes/0/tools/sjavac/IncludeExcludePatterns.d:/Users/zeedh/slowcoders/jdk-rtgc/test/langtools/tools/sjavac:/Users/zeedh/slowcoders/jdk-rtgc/build/macosx-x86_64-client-fastdebug/test-support/jtreg_test_langtools_tools_sjavac_IncludeExcludePatterns_java/classes/0/tools/lib \
        -Dtest.modules='jdk.compiler/com.sun.tools.javac.main jdk.compiler/com.sun.tools.sjavac jdk.compiler/com.sun.tools.sjavac.server' \
        -classpath /Users/zeedh/slowcoders/jdk-rtgc/build/macosx-x86_64-client-fastdebug/test-support/jtreg_test_langtools_tools_sjavac_IncludeExcludePatterns_java/classes/0/tools/sjavac/IncludeExcludePatterns.d:/Users/zeedh/slowcoders/jdk-rtgc/test/langtools/tools/sjavac:/Users/zeedh/slowcoders/jdk-rtgc/build/macosx-x86_64-client-fastdebug/test-support/jtreg_test_langtools_tools_sjavac_IncludeExcludePatterns_java/classes/0/tools/lib:/Users/zeedh/slowcoders/jdk-rtgc/test/langtools/tools/lib:/Users/zeedh/slowcoders/jdk-rtgc/jtreg-6.1/lib/javatest.jar:/Users/zeedh/slowcoders/jdk-rtgc/jtreg-6.1/lib/jtreg.jar \
        Wrapper IncludeExcludePatterns