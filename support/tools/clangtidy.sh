#!/bin/sh

C_FILES=$(find kernel -name '*.c')
H_FILES=$(find kernel -name '*.h' ! -name 'PureDOOM.h')
clang-tidy $C_FILES $H_FILES
