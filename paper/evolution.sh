#!/bin/bash
# output format: STCB CCB CR
# STCB: symbol table construction cost in cycles-per-compressed byte (constructing a new ST per 8MB text)
# CCB:  compression speed cycles-per-compressed byte 
# CR:   compression (=size reduction) factor achieved

(for i in dbtext/*; do (./cw-strncmp $i 2>&1) | awk '{ l++; if (l==3) t=$2; if (l==6) c=$2; d=$1}END{print t " " c " " d}'; done) | awk '{t+=$1;c+=$2;d+=$3;k++}END{ print (t/k) " " (c/k) " " d/k " iterative|suffix-array|dynp-matching|strncmp|scalar" }'
(for i in dbtext/*; do (./cw $i 2>&1) | awk '{ l++; if (l==3) t=$2; if (l==6) c=$2; d=$1}END{print t " " c " " d}'; done) | awk '{t+=$1;c+=$2;d+=$3;k++}END{ print (t/k) " " (c/k) " " d/k " iterative|suffix-array|dynp-matching|str-as-long|scalar"}'
(for i in dbtext/*; do (./cw-greedy $i 2>&1) | awk '{ l++; if (l==3) t=$2; if (l==6) c=$2; d=$1}END{print t " " c " " d}'; done) | awk '{t+=$1;c+=$2;d+=$3;k++}END{ print (t/k) " " (c/k) " " d/k " iterative|suffix-array|greedy-match|str-as-long|scalar" }'
(for i in dbtext/*; do (./vcw $i 2>&1) | fgrep -v target | awk '{ l++; if (l==2) t=$2; if (l==4) c=$2; d=$1}END{print t " " c " " d}'; done) | awk '{t+=$1;c+=$2;d+=$3;k++}END{ print (t/k) " " (c/k) " " d/k " bottom-up|binary-search|greedy-match|str-as-long|scalar" }'
(for i in dbtext/*; do (./hcw $i 511 -adaptive 2>&1) | fgrep -v target | awk '{ l++; if (l==2) t=$2; if (l==4) c=$2; d=$1}END{print t " " c " " d}'; done) | awk '{t+=$1;c+=$2;d+=$3;k++}END{ print (t/k) " " (c/k) " " d/k " bottom-up|lossy-hash|greedy-match|str-as-long|branch-scalar" }'
#(for i in dbtext/*; do (./hcw-opt $i 511 -branch 2>&1) | fgrep -v target | awk '{ l++; if (l==2) t=$2; if (l==4) c=$2; d=$1}END{print t " " c " " d}'; done) | awk '{t+=$1;c+=$2;d+=$3;k++}END{ print (t/k) " " (c/k) " " d/k " bottom-up|lossy-hash|greedy-match|str-as-long|branch-scalar|optimized-construction" }'
(for i in dbtext/*; do (./hcw-opt $i 511 -adaptive 2>&1) | fgrep -v target | awk '{ l++; if (l==2) t=$2; if (l==4) c=$2; d=$1}END{print t " " c " " d}'; done) | awk '{t+=$1;c+=$2;d+=$3;k++}END{ print (t/k) " " (c/k) " " d/k " bottom-up|lossy-hash|greedy-match|str-as-long|adaptive-scalar|optimized-construction" }'
(for i in dbtext/*; do (./hcw-opt $i 2>&1) | fgrep -v target | awk '{ l++; if (l==2) t=$2; if (l==4) c=$2; d=$1}END{print t " " c " " d}'; done) | awk '{t+=$1;c+=$2;d+=$3;k++}END{ print (t/k) " " (c/k) " " d/k " bottom-up|lossy-hash|greedy-match|str-as-long|avx512|optimized-construction" }'

# on Intel SKX CPUs| the results look like:
#
# 75.117,160.11,1.97194 iterative|suffix-array|dynp-matching|strncmp|scalar
#   \--> 160 cycles per byte produces a very slow compression speed (say ~20MB/s on a 3Ghz CPU) 
#
# 73.6948,81.6404,1.97194 iterative|suffix-array|dynp-matching|str-as-long|scalar
#   \--> str-as-long (i.e. FSST focusing on 8-byte word symbols) improves compression speed 2x 
#
# 74.4996,37.457,1.94764 iterative|suffix-array|greedy-match|str-as-long|scalar
#   \--> dynamic programming brought only 3% smaller size. So drop it and gain another 2x compression speed.
#
# 2.10217,19.9739,2.33083 bottom-up|binary-search|greedy-match|str-as-long|scalar
#   \--> bottom-up is *really* better in terms of compression factor than iterative with suffix array.
#
# 1.74783,10.7009,2.28103 bottom-up|lossy-hash|greedy-match|str-as-long|scalar-branch
#   \--> hashing significantly improves compression speed at only 5% size cost (due to hash collisions) 
#
# 1.74783,9.8142,2.28103 bottom-up|lossy-hash|greedy-match|str-as-long|scalar-adaptive
#   \--> adaptive use of encoding kernels gives compression speed a small bump
#
# 0.820435,4.12261,2.19227 bottom-up|lossy-hash|greedy-match|str-as-long|avx512|optimized-construction
#   \--> symboltable optimizations & AVX512 kick in, resp. for construction time and compression speed.
#
# optimized construction refers to the combination of three changes:
# - reducing the amount of bottom-up passes from 10 to 5 (less learning time, but.. slighty worsens CR)
# - looking at subsamples in early rounds (increasing the sample as the rounds go up). Less compression work.
# - splitting the counters for less cache pressure and aiding fast skipping over counts-of-0 
