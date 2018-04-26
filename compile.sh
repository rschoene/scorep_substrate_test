#!/bin/sh

gcc -g -c -fPIC test_substrate.c -o substrate_plugin_test.o `scorep-config --cppflags`
# 2nd step link
gcc -g -Wall -shared substrate_plugin_test.o  -Wl,-soname,libscorep_substrate_test.so -o libscorep_substrate_test.so -lpthread
