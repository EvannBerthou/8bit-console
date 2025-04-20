#!/bin/sh

python asset_packager.py
python compiler.py
gcc -Wall -Wextra main.c -o main -L ./lib -lraylib -lm -ggdb && ./main
