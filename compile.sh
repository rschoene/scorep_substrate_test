#!/bin/sh
export SCOREP_DIR="/home/rschoene/scorep-substrate-plugins-merge"


gcc -g -c -fPIC test_substrate.c -o substrate_plugin_test.o -I${SCOREP_DIR}/include
# 2nd step link
gcc -g -Wall -shared substrate_plugin_test.o  -Wl,-soname,libtest_substrate.so -o libtest_substrate.so -lpthread
