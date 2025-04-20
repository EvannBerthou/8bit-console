#!/bin/sh

set -xe

python compiler.py
gcc -Wall -Wextra main.c -o main -L ./lib -lraylib -lm -ggdb
./main
