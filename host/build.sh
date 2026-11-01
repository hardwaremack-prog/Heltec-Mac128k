#!/bin/sh
# Builds the PC test harness (Linux/macOS): ./build.sh && ./machost --testrom
set -e
C=../Heltec-Mac/src/core
mkdir -p obj
for f in umac_main via scc disc rom m68kcpu m68kops m68kdasm; do
  gcc -O2 -w -DMACMESH_HOST $EXTRA -I$C -c $C/$f.c -o obj/$f.o
done
gcc -O2 -w -I$C -c $C/softfloat/softfloat.c -o obj/softfloat.o
g++ -O2 -std=c++17 -Wall -DMACMESH_HOST -o machost hostmain.cpp ../Heltec-Mac/macmachine.cpp ../Heltec-Mac/mackeys.cpp \
    ../Heltec-Mac/macvnc.cpp ../Heltec-Mac/webview.cpp obj/*.o -lpthread -lm
echo built machost
