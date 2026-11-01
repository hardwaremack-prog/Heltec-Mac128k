#!/bin/sh
# Builds heltec-mac.wasm with wasi-sdk:  WASI_SDK=/path/to/wasi-sdk ./build.sh
set -e
W=${WASI_SDK:-/opt/wasi/wasi-sdk-25.0-x86_64-linux}
C=../Heltec-Mac/src/core
mkdir -p obj
for f in umac_main via scc disc rom m68kcpu m68kops m68kdasm softfloat/softfloat; do
  $W/bin/clang --target=wasm32-wasip1 -O2 -w -I$C -c $C/$f.c -o obj/$(basename $f).o
done
$W/bin/clang++ --target=wasm32-wasip1 -O2 -fno-exceptions -mexec-model=reactor \
  -Wl,--export=malloc -Wl,--initial-memory=33554432 -Wl,--max-memory=134217728 -Wl,--strip-all \
  -o heltec-mac.wasm wasm_glue.cpp ../Heltec-Mac/macmachine.cpp ../Heltec-Mac/mackeys.cpp obj/*.o
ls -la heltec-mac.wasm
