
## 1. Prepare external libraries
-  jtreg 
```sh
   git clone https://github.com/openjdk/jtreg.git -b jtreg-6.1+1
   bash build.sh --jdk /usr/lib/jvm/adoptopenjdk-14-hotspot-amd64
```

-  google test  
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

## 5. test codedump 파일 자동삭제 방지<br>
  RunTests.gmk 파일을 아래와 같이 수정. <br>
```  
   -> JTREG_RETAIN ?= fail,error,hs_err_pid*
```
