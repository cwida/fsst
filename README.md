# FSST
Fast Static Symbol Table (FSST): fast text compression that allows random access 

[![Watch the video](https://github.com/cwida/fsst/raw/master/fsst-presentation.png)](https://github.com/cwida/fsst/raw/master/fsst-presentation.mp4)

Authors:
- Peter Boncz (CWI)
- Viktor Leis (FSU Jena)
- Thomas Neumann (TU Munchen)

You can contact the authors via the issues of this FSST source repository : https://github.com/cwida/fsst

FSST: Fast Static Symbol Table compression
see the PVLDB paper https://github.com/cwida/fsst/raw/master/fsstcompression.pdf

FSST is a compression scheme focused on string/text data: it can compress strings from distributions with many different values (i.e. where dictionary compression will not work well). It allows *random-access* to compressed data: it is not block-based, so individual strings can be decompressed without touching the surrounding data in a compressed block. When compared to e.g. LZ4 (which is block-based), FSST further achieves similar decompression speed and compression speed, and better compression ratio.

FSST encodes strings using a symbol table -- but it works on pieces of the string, as it maps "symbols" (1-8 byte sequences) onto "codes" (single-bytes). FSST can also represent a byte as an exception (255 followed by the original byte). Hence, compression transforms a sequence of bytes into a (supposedly shorter) sequence of codes or escaped bytes. These shorter byte-sequences could be seen as strings again and fit in whatever your program is that manipulates strings. An optional 0-terminated mode (like, C-strings) is also supported.

FSST ensures that strings that are equal, are also equal in their compressed form. This means equality comparisons can be performed without decompressing the strings.

FSST compression is quite useful in database systems and data file formats. It e.g., allows fine-grained decompression of values in case of selection predicates that are pushed down into a scan operator. But, very often FSST even allows to postpone decompression of string data. This means hash tables (in joins and aggregations) become smaller, and network communication (in case of distributed query processing) is reduced. All of this without requiring much structural changes to existing systems: after all, FSST compressed strings still remain strings.

The implementation of FSST is quite portable, using CMake and has been verified to work on 64-bits x86 computers running Linux, Windows and MacOS (the latter also using arm64).

FSST12 is an alternative version of FSST that uses 12-bits symbols, and hence can encode up to 4096 symbols (of max 8 bytes long). 
It does not need an escaping mechanism as the first 256 codes are single-byte sybols consisting of only that byte.
Lookup tables are 16x bigger than for 8-bits FSST (~8KB on average in storage, 32KB in memory), so a larger granularity of encoding volume is needed.
FSST12 can deal with data distributions that are less focused than natural text. For instance, json and xml compress better with it.
Decoding it does need a larger lookup table, and encoding it is  slower due to the increased memory pressure needed for 4096x4096 counters (and the absence of AVX512 path - for x86).
