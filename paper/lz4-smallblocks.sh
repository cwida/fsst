#!/bin/bash
maxsize=100000 # input file needs to be at least this size
dd if=$1 of=tmpsplit.out bs=$maxsize count=1 2> /dev/null
for blocksize in {16,64,256,1024,4096,16384,65536}; do
    mkdir tmpsplit$blocksize
    split -b $blocksize tmpsplit.out tmpsplit$blocksize/x
    echo -n $blocksize ""
    size=$((for f in tmpsplit$blocksize/x*; do lz4 -c $f | wc -c; done) | awk '{s+=$1} END {print s}')
    echo "$maxsize / $size" | bc -l
    rm -rf tmpsplit$blocksize/
done
rm tmpsplit.out
