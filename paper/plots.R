## apt install libcairo2-dev
## install R packages: install.packages(c('ggplot2', 'sqldf', 'Cairo'))

library(ggplot2)
library(sqldf)
library(Cairo)

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

