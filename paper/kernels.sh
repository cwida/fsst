#/bin/bash
PARAMS='simd1 simd2 simd3 simd4 adaptive'
(echo | awk '{ print "{\\begin{tabular}{|rrrr|r|l|}\n\\hline"}'
echo $PARAMS | awk "{for(i=1;i<=NF;i++) printf \"{\\\\footnotesize{X%d\$%s\$}}&\",i,\$i}" | sed 's/simd/simd_/g'
echo "\\\\"
echo "\\hline"
echo "\\hline"
(for i in hex yago email wiki uuid urls2 urls firstname lastname city credentials street movies faust hamlet chinese japanese wikipedia genome location c_name l_comment ps_comment 
 do 
   for m in $PARAMS
   do
     (./hcw-opt dbtext/$i 511 -$m 2>&1) | tail -2 | head -1 | awk '{ printf "%f ", $2 }'
   done
   echo $i
 done) | awk '{for(i=1;i<NF;i++){r[i]+=$i;printf "{\\footnotesize{X%d%5.2f}}& ",i,$i}k++;printf "{\\footnotesize %s}\\\\\n",$NF}END{print "\\hline"; for(j=1;j<i;j++)printf "{\\footnotesize{X%d%5.2f}}& ",j,r[j]/k;print "{\\footnotesize average}\\\\\n\\hline\n\\end{tabular}}"}' | sed 's/_/\\_/g' | sed 's/[0-9]*-//') | sed 's/X[38]/\\bf /g' | sed 's/X[1-9]//g' | sed 's/adaptive/scalar/' 
