#/bin/bash
PARAMS='simd1 simd2 simd3 simd4 nosuffix avoidbranch branch adaptive'
(echo | awk '{ print "{\\renewcommand{\\tabcolsep}{0.5mm}\\renewcommand{\\arraystretch}{0.93}\\begin{tabular}{|rrrr|rrrr|l|}\n\\hline"}'
echo $PARAMS | awk "{for(i=1;i<=NF;i++) printf \"{\\\\scriptsize{X%d\$%s\$}}&\",i,\$i}" | sed 's/simd/simd_/g'
echo "\\\\"
echo "\\hline"
echo "\\hline"
(for i in dbtext/*
 do 
   for m in $PARAMS
   do
     (./hcw-opt $i 511 -$m 2>&1) | tail -2 | head -1 | awk '{ printf "%f ", $2 }'
   done
   echo $i
 done) | awk '{for(i=1;i<NF;i++){r[i]+=$i;printf "{\\scriptsize{X%d%5.2f}}& ",i,$i}k++;printf "{\\scriptsize %s}\\\\\n",$NF}END{print "\\hline"; for(j=1;j<i;j++)printf "{\\scriptsize{X%d%5.2f}}& ",j,r[j]/k;print "\\\\\n\\hline\n\\end{tabular}}"}' | sed 's/_/\\_/g' | sed 's/[0-9]*-//') | sed 's/X[38]/\\bf /g' | sed 's/X[1-9]//g' | sed 's/adaptive/adptv/' | sed 's/nosuffix/short/'| sed 's/avoidbranch/single/' | sed 's/branch/hash/' 
