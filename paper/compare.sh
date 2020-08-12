#!/bin/bash
(for i in hex yago email wiki uuid urls2 urls firstname lastname city credentials street movies faust hamlet chinese japanese wikipedia genome location c_name l_commen ps_comment 
 do
  fgrep $i $1 | fgrep -v ${i}2 | fgrep -v ${i}pedia | awk '{ printf "% 16s   %1.2f  %1.2f   % 8.2f   % 8.2f   % 8.2f   % 8.2f\n", $1, $7, $2, $8, $3, $11, $6}'
 done) | awk '{print$0;k++;for(i=2;i<=NF;i++) r[i]+=$i;}END{printf "% 16s   %1.2f  %1.2f   % 8.2f   % 8.2f   % 8.2f   % 8.2f\n", "AVG",r[2]/k,r[3]/k,r[4]/k,r[5]/k,r[6]/k,r[7]/k,r[8]/k}'
