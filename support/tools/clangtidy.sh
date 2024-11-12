#!/bin/sh

C_FILES=$(find kernel -name '*.c')
H_FILES=$(find kernel -name '*.h')
clang-tidy $C_FILES $H_FILES
