#!/bin/bash
# run some stats on LZ4 and FSST when we pre-sort the (non-book) dbtext files line-by-line
# make sure the right version of lz4 is in PATH

# make sorted version of dbtext
rm -rf .sorted 2>/dev/null
mkdir .sorted
cd dbtext
for i in * 
do 
  sort $i > ../.sorted/$i; 
done
cp chinese japanese faust hamlet ../.sorted/
cd ..

# note sizes, display stats
(for i in hex yago email wiki uuid urls2 urls firstname lastname city credentials street movies faust hamlet chinese japanese wikipedia genome location c_name l_comment ps_comment
 do 
  ./filtertest compare 1000 dbtext/$i | tail -1 | awk '{ printf "% 16s %1.2f %1.2f ",$1,$2,$7}'
  ./filtertest compare 1000 .sorted/$i | tail -1 | awk '{ printf "%1.2f %1.2f\n",$2,$7}'
 done) | 
awk '{ s1+=$2; s2+=$3; s3+=$4; s4+=$5; k++; print $0} END {printf "% 16s %1.2f% 1.2f %1.2f %1.2f\n", "avg",s1/k, s2/k, s3/k, s4/k}'
