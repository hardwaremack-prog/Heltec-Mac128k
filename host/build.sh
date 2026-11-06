#!/bin/sh
# Builds the PC test harness (Linux/macOS): ./build.sh && ./machost --testrom
#   V4=1 ./build.sh   builds machost-v4: the Heltec V4 variant (256 KB ROMs incl. the Mac SE)
set -e
C=../Heltec-Mac/src/core
DEF=""; OUT=machost; OBJ=obj
if [ -n "$V4" ]; then DEF="-DHELTEC_V4"; OUT=machost-v4; OBJ=obj-v4; fi
mkdir -p $OBJ
for f in umac_main via scc disc rom adb m68kcpu m68kops m68kdasm; do
  gcc -O2 -w -DMACMESH_HOST $DEF $EXTRA -I$C -c $C/$f.c -o $OBJ/$f.o
done
gcc -O2 -w -I$C -c $C/softfloat/softfloat.c -o $OBJ/softfloat.o
g++ -O2 -std=c++17 -Wall -DMACMESH_HOST $DEF -o $OUT hostmain.cpp ../Heltec-Mac/macmachine.cpp ../Heltec-Mac/mackeys.cpp \
    ../Heltec-Mac/macvnc.cpp ../Heltec-Mac/webview.cpp $OBJ/*.o -lpthread -lm
echo built $OUT
