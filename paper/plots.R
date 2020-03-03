## apt install libcairo2-dev
## install R packages: install.packages(c('ggplot2', 'sqldf', 'Cairo'))

library(ggplot2)
library(sqldf)
library(Cairo)

#### FSST vs LZ4 relative

d = read.table('results/FSST-vs-LZ4.csv', header = TRUE)
d = sqldf("
select file, \"solo.crate\" as value, 'compression factor' as metric from d
union all
select file, \"solo.cMB.s\", 'compression speed' from d
union all
select file, \"dMB.s\", 'decompression speed' from d
")
d2 = read.table('results/fullblock.csv', header = TRUE)
d2 = sqldf("select file, file||'\n('||round(\"FSST.brate\",2) ||' '|| round(\"FSST.bMB.s\"/1024,2) ||' '|| round(\"FSST.dMB.s\"/1024, 2)||')' as lbl from d2 file")
d = sqldf("select d.file, value, metric, lbl as file2 from d join d2 on  d.file = d2.file")
ggplot(aes(file, value/100, fill=metric), data=d) +
    geom_hline(yintercept=1, color = 'darkgrey') +
    geom_bar(stat="identity", position = "dodge") +
#    ggtitle('FSST vs. LZ4 in file mode') +
    xlab('') +
    ylab("← LZ4 better   rel. perf.   FSST better →   ") +
    theme_bw(10) +
    theme(plot.margin = unit(c(0.1,.1,0,.1), "cm")) +
    theme(axis.text.x = element_text(angle = 30, hjust = 1)) +
    theme(legend.title = element_blank()) +
#    theme(legend.background = element_blank()) +
    theme(legend.position = c(0.4, 0.79))
ggsave(filename = 'results/fsstvslz4.pdf', device = cairo_pdf, width=8, height=3.1)

#### FSST absolute
d = read.table('results/fullblock.csv', header = TRUE)
d = sqldf("select file, round(\"FSST.brate\",2) as factor, round(\"FSST.bMB.s\"/1024,2) as cspeed, round(\"FSST.dMB.s\"/1024, 2) as dspeed from d order by file")

#### line vs block

d = read.table('results/line.csv', header = TRUE)
d = sqldf("
select scheme, \"crate\" as value, 'compr. factor' as metric from d union all
select scheme, \"cMB.s\", 'compr. speed [GB/s]' from d union all
select scheme, \"dMB.s\", 'decomp. speed [GB/s]' from d")
d$scheme=ordered(d$scheme, levels=c('LZ4line','LZ4dict','LZ4','FSST'), labels=c('LZ4 line','LZ4 dict','LZ4 block','FSST'))
ggplot(aes(scheme, value, group=metric), data=d) +
    geom_bar(stat="identity", position = "dodge") +
    facet_grid(. ~ metric) +
    xlab('') +
    ylab('') +
#    ggtitle('individual strings vs. blocks [geomean]') +
    theme_bw(10) +
    theme(axis.title.y = element_blank()) +
    theme(axis.text.x = element_text(angle = 45, hjust = 1)) +
    theme(plot.margin = unit(c(0.1,.1,0,.1), "cm")) +
    theme(legend.position="none")
ggsave(filename = 'results/linevsblock.pdf', device = cairo_pdf, width=4, height=2)

### filtertest, selectivity

d = read.table('results/filter.csv', header = TRUE)
d = sqldf("
select sel, lz4 as value, 'LZ4' as scheme from d union all
select sel, fsst as value, 'FSST' as scheme from d")
ggplot(aes(sel, value*1000, group=scheme), data=d) +
    geom_line(size=1) +
    geom_point() +
    annotate("text", x = c(10,10), y = c(60e6, 22e6), label = c('FSST', 'LZ4')) +
    scale_y_continuous('result tuples / s') +
    scale_x_log10('selectivity', breaks=c(1,3,10,30,100), labels=c('1%','3%','10%','30%','100%'))+
#    ggtitle('tuple selection [geomean]') +
    theme_bw() +
    theme(plot.margin = unit(c(0.1,.1,0,.1), "cm")) +
    theme(legend.position="none")
ggsave(filename = 'results/filter.pdf', device = cairo_pdf, width=4, height=1.7)

