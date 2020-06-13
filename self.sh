#!/bin/bash
set -e

TMP=$1
CC=$2
OUTPUT=$3

rm -rf $TMP
mkdir -p $TMP

zcc() {
    $CC -Iinclude -I/usr/local/include -I/usr/include \
        -I/usr/include/linux -I/usr/include/x86_64-linux-gnu \
        -o $TMP/${1%.c}.s $1
    gcc -c -o $TMP/${1%.c}.o $TMP/${1%.c}.s
}

cc() {
    gcc -c -o $TMP/${1%.c}.o $1
}

zcc main.c
zcc type.c
zcc parse.c
zcc codegen.c
zcc tokenize.c
zcc preprocess.c

(cd $TMP; gcc -static -o ../$OUTPUT *.o)
