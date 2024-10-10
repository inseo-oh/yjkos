#!/bin/sh
# Finds #includes, sorts it, and prints if there are differences.
# Note that some source files include files conditionally, so this may report files
# even if it's already sorted correctly.

FILES=$(find kernel -type f)

for f in $FILES; do
    GREP1=$(grep "#include" $f)
    GREP2=$(grep "#include" $f | sort)
    if [ "$GREP1" != "$GREP2" ]; then
        echo "-------------------- $f --------------------"
        echo "Sorted include:"
        grep "#include" $f | sort
    fi
done