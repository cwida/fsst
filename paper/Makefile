SHELL := /bin/bash

OPT=-O3 -DNDEBUG

all: cw vcw cw-greedy cw-strncmp hcw hcw-opt filtertest linetest
clean:
	-@rm -f filtertest linetest cw cw-greedy cw-strncmp hcw hcw-opt 
cw: cw.cpp
	g++ -std=c++14 -W -Wall -ocw $(OPT) -g cw.cpp
cw-greedy: cw.cpp
	g++ -std=c++14 -W -Wall -DGREEDY -ocw-greedy $(OPT) -g cw.cpp
cw-strncmp: cw.cpp
	g++ -std=c++14 -W -Wall -DSTRNCMP -ocw-strncmp $(OPT) -g cw.cpp
vcw: vcw.cpp
	g++ -std=c++14 -W -Wall -ovcw $(OPT) -g vcw.cpp
hcw: hcw.cpp 
	g++ -std=c++14 -W -Wall  $(OPT) -g -ohcw -DNONOPT_FSST -I.. ../libfsst.cpp hcw.cpp 
hcw-opt: hcw.cpp ../libfsst.a 
	g++ -std=c++14 -W -Wall -ohcw-opt $(OPT) -g hcw.cpp -I.. -L.. -lfsst

filtertest: filtertest.cpp ../libfsst.a
	g++ -std=c++14 -W -Wall -ofiltertest -g $(OPT) filtertest.cpp -llz4 -I.. -L.. -lfsst 

linetest: linetest.cpp ../libfsst.a
	#g++ -std=c++14 -W -Wall -olinetest -Izstd -Lzstd -g $(OPT) linetest.cpp -llz4 -l:libzstd.so.1 -I.. -L.. -lfsst
	g++ -std=c++14 -W -Wall -olinetest -Izstd -Lzstd -g $(OPT) linetest.cpp -llz4 -lzstd -I.. -L.. -lfsst

experiments: results/perline.csv results/fullblock.csv results/evolution.csv results/filter.csv results/line.csv results/FSST-vs-LZ4.csv results/kernels.tex results/lz4-smallblocks.csv results/sorted.csv

results/evolution.csv: cw cw-greedy cw-strncmp hcw hcw-opt linetest dbtext/*
	@mkdir -p results
	. ./evolution.sh > $@.tmp
	mv $@.tmp $@

results/perline.csv: linetest dbtext/*
	@mkdir -p results
	./linetest compare dbtext/* > $@.tmp
	awk '{print $$0;if(k++)for(j=2;j<=NF;j++)r[j]+=$$j}END{printf"AVG";for(i=2;i<j;i++)printf"\t%4.3f",r[i]/k;print""}' $@.tmp > $@
	rm $@.tmp

results/filter.csv: filtertest dbtext/*
	@mkdir -p results
	./filtertest comparefilter 1000 dbtext/* > $@

results/fullblock.csv: filtertest dbtext/*
	@mkdir -p results
	./filtertest compare 1000 dbtext/* > $@.tmp
	awk '{print $$0;if(k++)for(j=2;j<=NF;j++)r[j]+=$$j}END{printf"AVG";for(i=2;i<j;i++)printf"\t%4.3f",r[i]/k;print""}' $@.tmp > $@
	rm $@.tmp

results/kernels.tex: filtertest dbtext/*
	@mkdir -p results
	. ./kernels.sh > $@

results/FSST-vs-LZ4.csv: results/fullblock.csv
	. ./compare.sh results/fullblock.csv > $@

results/line.csv: results/perline.csv results/fullblock.csv
	echo "" | awk '{ print "scheme\tcrate\tcMB/s\tdMB/s" }' > $@
	tail -1 results/perline.csv | awk '{printf"LZ4line\t%f\t%f\t%f\n",$$6,$$5/1000,$$7/1000}' >> $@
	tail -1 results/perline.csv | awk '{printf"LZ4dict\t%f\t%f\t%f\n",$$9,$$8/1000,$$10/1000}' >> $@
	tail -1 results/fullblock.csv | awk '{printf"LZ4\t%f\t%f\t%f\n",$$7,$$8/1000,$$11/1000}' >> $@
	tail -1 results/fullblock.csv | awk '{printf"FSST\t%f\t%f\t%f\n",$$2,$$3/1000,$$6/1000}' >> $@

results/lz4-smallblocks.csv: 
	. ./lz4-smallblocks.sh dbtext/urls > $@

results/sorted.csv: 
	. ./sorted.sh > $@
