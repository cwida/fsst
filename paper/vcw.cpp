// this software is distributed under the MIT License (http://www.opensource.org/licenses/MIT):
//
// Copyright 2018-2019, CWI, TU Munich
//
// Permission is hereby granted, free of charge, to any person obtaining a copy of this software and associated documentation files
// (the "Software"), to deal in the Software without restriction, including without limitation the rights to use, copy, modify,
// merge, publish, distribute, sublicense, and/or sell copies of the Software, and to permit persons to whom the Software is
// furnished to do so, subject to the following conditions:
//
// - The above copyright notice and this permission notice shall be included in all copies or substantial portions of the Software.
//
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES
// OF MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE
// LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR
// IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
//
// You can contact the authors via the FSST source repository : https://github.com/cwida/fsst
#include <algorithm>
#include <cassert>
#include <cstring>
#include <fstream>
#include <iostream>
#include <numeric>
#include <queue>
#include <string>
#include <unordered_set>
#include <vector>
#include "PerfEvent.hpp"

using namespace std;

typedef uint8_t u8;
typedef uint16_t u16;
typedef uint32_t u32;
typedef uint64_t u64;

typedef uint16_t Counter; // should correspond to sample size

/// Symbol of up to 8 bytes
struct Symbol {
   static const unsigned maxLength = 8;

   union {
      u64 word;
      u8 buffer[maxLength];
   };
   u8 length;
   u32 gain;

   Symbol() {}

   explicit Symbol(u8 c) : length(1) { word = c; }
   explicit Symbol(const char* begin, const char* end) : Symbol(begin, end-begin) {}
   explicit Symbol(u8* begin, u8* end) : Symbol((const char*)begin, end-begin) {}
   explicit Symbol(const char* input, unsigned len) {
      if (len>=8) {
         word = reinterpret_cast<const uint64_t*>(input)[0];
         length = 8;
      } else if ((reinterpret_cast<uintptr_t>(input)&63)<=(64-8)) {
         u64 eight = reinterpret_cast<const uint64_t*>(input)[0];
         u64 garbageBits = (8-len) * 8;
         word = (eight<<garbageBits)>>garbageBits;
         length = len;
      } else {
         word = reinterpret_cast<const uint64_t*>(input+len-8)[0]>>(8*(8-len));
         length = len;
      }
   }

   u8 first() const { return word & 0xFF; }
   u16 first2() const { return word & 0xFFFF; }

   bool operator==(const Symbol& other) const { return word==other.word && length==other.length; }

   bool isPrefixOf(const Symbol& other) const {
      u64 garbageBits = (8-length)*8;
      return word == ((other.word<<garbageBits)>>garbageBits);
   }
};

Symbol concat(Symbol a, Symbol b) {
   Symbol s;
   s.length = min(8, a.length+b.length);
   s.word = (b.word << (8*a.length)) | a.word;
   return s;
}

namespace std {
template <>
class hash<Symbol> {
   public:
   size_t operator()(const Symbol& s) const {
      uint64_t k = s.word;
      const uint64_t m = 0xc6a4a7935bd1e995;
      const int r = 47;
      uint64_t h = 0x8445d61a4e774912 ^ (8*m);
      k *= m;
      k ^= k >> r;
      k *= m;
      h ^= k;
      h *= m;
      h ^= h >> r;
      h *= m;
      h ^= h >> r;
      return h;
   }
};
}

bool isEscapeCode(u16 code) { return code >= 256; }

std::ostream& operator<<(std::ostream& out, const Symbol& s) {
   for (unsigned i=0; i<s.length; i++)
      out << s.buffer[i];
   return out;
}

struct SymbolMap {
   Symbol symbols[512]; // 0-254: normal symbols, 256-511: escape pseudo symbols
   unsigned symbolCount; // number of normal symbols currently in map
   u16 index1[256]; // index for single-byte symbols
   u8 index2[256*256+1]; // index for longer symbols (prefixed by first two bytes)

   SymbolMap() : symbolCount(0) {
      memset(index2, 0, sizeof(index2));
      for (unsigned i=0; i<256; i++)
         index1[i] = 256+i;
      // Create escape pseudo symbols
      for (unsigned i=256; i<512; i++)
         symbols[i] = Symbol(i);
   }

   void add(Symbol s) {
      symbols[symbolCount++] = s;
   }

   void clear() {
      symbolCount = 0;
   }

   void buildIndex() {
      // split single-byte from longer symbols
      unsigned longCount = partition(symbols, symbols + symbolCount, [](const Symbol& a) { return a.length > 1; }) - symbols;

      // sort longer symbols by first char, then by length descending
      sort(symbols, symbols + longCount, [](const Symbol& a, const Symbol& b) {
            if (a.first2() == b.first2()) {
               return a.length > b.length;
            } else {
               return a.first2() < b.first2();
            }
         });

      // construct index2
      index2[0] = 0;
      unsigned prev = 0;
      for (unsigned i=0; i<longCount; i++) {
         unsigned curr = symbols[i].first2();
         if (curr != prev) {
            for (unsigned j=prev+1; j<curr; j++)
               index2[j] = i;
            index2[curr] = i;
            prev = curr;
         }
      }
      for (unsigned i=prev+1; i<(256*256+1); i++)
         index2[i] = longCount;

      // construct index1
      for (unsigned i=0; i<256; i++)
         index1[i] = 256+i;
      for (unsigned i=longCount; i<symbolCount; i++)
         index1[symbols[i].first()] = i;
   }

   /// Find longest expansion
   u16 findExpansion(Symbol s) const {
      // check long symbols first
      unsigned first = s.first2();
      for (unsigned i=index2[first]; i<index2[first+1]; i++)
         if (symbols[i].isPrefixOf(s))
            return i;

      // check single-byte symbol
      return index1[s.first()];
   }

   // renumber codes so they are grouped by length, and compute serialized dictionary size.
   unsigned finalize() {
      // proposed serialization:
      // 8xu8: [#8,#7,#6,#5,#4,#3,#2,#1] +  // #i being the amount of symbols of length i
      //       [all concatenated bytes of #8 symbols] +
      //       [all concatenated bytes of #7 symbols] +
      //       ..
      //       [all concatenated bytes of #1 symbols]
      // hence 8 bytes header and then just all bytes. With an avg
      // symbol length of 3 bytes this just takes ~770 bytes or so
      //
      // to make this possible, the codes must be ordered by symbol length
      Symbol tmp[256];
      memcpy(tmp, symbols, sizeof(tmp));
      unsigned serialSize = 8; // header = 8xu8 counts (one count per symbol length)
      for(unsigned len=8, newCode=0; len>0; len--)
         for(unsigned code=0; code<255; code++)
            if (tmp[code].length == len) {
               symbols[newCode++] = tmp[code];
               serialSize += len;
            }
#ifdef GRAMSTATS
      // calculate some stats
      u8 conflict2[256]={0}, conflict3[256]={0};
      vector<u8> cnt2, cnt3;
      cnt2.resize(256*256); memset(cnt2.data(), 0, 256*256);
      cnt3.resize(256*256*256); memset(cnt3.data(), 0, 256*256*256);
      for(unsigned code=0; code<255; code++) {
         ((u8*) cnt2.data())[symbols[code].word & 0xFFFF]++;
         ((u8*) cnt3.data())[symbols[code].word & 0xFFFFFF]++;
      }
      for(unsigned code=0; code<256*256; code++)
         conflict2[cnt2[code]]++;
      for(unsigned code=0; code<256*256*256; code++)
         conflict3[cnt3[code]]++;
      for(unsigned code=1; code<255; code++)
         if (conflict2[code] > 1) cerr << "2gram-conflicts: " << code << " = " << ((int) conflict2[code]) << endl;
      for(unsigned code=1; code<255; code++)
         if (conflict3[code] > 1) cerr << "3gram-conflicts: " << code << " = " << ((int) conflict3[code]) << endl;
#endif
      buildIndex();
      return serialSize; // bytesize needed to serialize dictionary
   }
};

SymbolMap buildSymbolMap(vector<string>& sample, unsigned sampleSize) {
   SymbolMap symbolMap, bestMap, baseMap;
   Counter bestThreshold = 0, baseThreshold=sampleSize/4096, countThreshold=baseThreshold;
   Counter count[512];
   Counter pairCount[512][512];
   unsigned compressedSize = 0, bestSize = 2*sampleSize; // worst case (everything exception)
#ifdef DEBUG
   unsigned len[8] = {0};
#endif
   auto countDict = [&](unsigned target) {
      // compress sample, and compute (pair-)frequencies
      compressedSize = 0;

      for (auto& s : sample) {
         unsigned compressedLine = 0;
         u8* cur = (u8*)s.data();
         u8* end = (u8*)s.data() + s.size();

         if (cur < end) {
            u16 code1 = symbolMap.findExpansion(Symbol(cur, end));
            while (true) {
               count[code1]++;
               compressedLine += 1+isEscapeCode(code1);
               cur += symbolMap.symbols[code1].length;
#ifdef DEBUG
cerr << (isEscapeCode(code1)?"*":"|");
for(int i=1; i<symbolMap.symbols[code1].length; i++) cerr << " ";
#endif
               if (cur==end)
                  break;

               u16 code2 = symbolMap.findExpansion(Symbol(cur, end));
               pairCount[code1][code2]++;
               code1 = code2;
            }
#ifdef DEBUG
cerr << " " << s.size() << " => " << compressedLine << endl;
cerr << s.data() << endl;
#endif
            compressedSize += compressedLine;
         }
      }
      cerr << "target=" << target << " ratio=" << sampleSize/((double) compressedSize);
#ifdef DEBUG
      cerr << " 1=" << len[0] << " 2=" << len[1] << " 3=" << len[2] << " 4=" << len[3] << " 5=" << len[4] << " 6=" << len[5] << " 7=" << len[6] << " 8=" << len[7]
           << " tot=" << len[0]+len[1]+len[2]+len[3]+len[4]+len[5]+len[6]+len[7];
#endif
      if (compressedSize < bestSize) { // a new best solution!
         cerr << " best";
         bestMap = symbolMap;
         bestSize = compressedSize;
         bestThreshold = countThreshold;
      }
      cerr << endl;
   };

   for (unsigned target : {50, 100, 150, 200, 220, 240, 250,
#ifdef ADAPTIVE_THRESHOLD
      151, 201, 221, 241, 251, 152, 202, 222, 242, 252,
#endif
      254, 255, 255, 255, 255}) {
      memset(count, 0, sizeof(count));
      memset(pairCount, 0, sizeof(pairCount));
#ifdef ADAPTIVE_THRESHOLD
      // we try 150,200,220,240,250 with three countThresholds
      if (target == 254) { symbolMap = bestMap; countThreshold = bestThreshold; } // done: stick with what works best
      else if (target == 150) baseMap = symbolMap;
      else if (target == 151 || target == 152) symbolMap = baseMap;
      if (target < 254) countThreshold = (target%5)*baseThreshold;
      target = (target/5)*5;
      if (target == 150) symbolMap = baseMap;
#endif
#ifdef GREEDY_CONVERGE
      // in the convergence phase (target=255) we are greedy and hillclimby
      unsigned lastSize = compressedSize;
      countDict(target);
      if (target == 255 && lastSize <= compressedSize) return bestMap;
#else
      countDict(target);
#endif
      // Find candidates
      unordered_set<Symbol> candidates;
      auto addCandidate = [&](Symbol s, unsigned count) {
         unsigned gain = count * s.length;
         auto it = candidates.find(s);
         if (it == candidates.end()) {
            s.gain = gain;
            candidates.insert(s);
         } else {
            const_cast<Symbol&>(*it).gain += gain;
         }
      };
      for (unsigned code=0; code<512; code++) {
         if (count[code]) {
            Symbol s = symbolMap.symbols[code];
            addCandidate(s, count[code]);
         }
      }
      for (unsigned code1=0; code1<512; code1++) {
         for (unsigned code2=0; code2<512; code2++) {
            if (pairCount[code1][code2]>countThreshold) {
               Symbol s1 = symbolMap.symbols[code1];
               if (s1.length==Symbol::maxLength)
                  continue;
               Symbol s = concat(s1, symbolMap.symbols[code2]);
               addCandidate(s, pairCount[code1][code2]);
            }
         }
      }

      // Insert candidates into priority queue (by gain)
      auto compareGain = [](const Symbol& s1, const Symbol& s2) { return s1.gain < s2.gain; };
      priority_queue<Symbol,vector<Symbol>,decltype(compareGain)> queue(compareGain);
      for (auto& s : candidates)
         queue.push(s);

#ifdef DEBUG
      memset(len, 0, 8*sizeof(*len));
#endif
      // Create new symbol map using best candidates
      symbolMap.clear();
      while (symbolMap.symbolCount < target && !queue.empty()) {
         symbolMap.add(queue.top());
#ifdef DEBUG
         len[queue.top().length-1]++;
#endif
         queue.pop();
      }
      symbolMap.buildIndex();
   }
   countDict(256); // test last map

   return bestMap;
}

string compress(const SymbolMap& symbolMap, const string& uncompressed) {
   string compressed;
   auto cur = uncompressed.data();
   auto end = cur + uncompressed.size();
   while (cur<end) {
      u16 code = symbolMap.findExpansion(Symbol(cur,end));
      if (isEscapeCode(code)) {
         compressed.push_back(static_cast<char>(255));
         compressed.push_back(*cur++);
      } else {
         compressed.push_back(code);
         cur += symbolMap.symbols[code].length;
      }
   }
   return compressed;
}

string decompress(const SymbolMap& symbols, const string& compressed) {
   const u8 *s = (const u8*) &(compressed[0]);
   string uncompressed;
   for (unsigned i=0; i<compressed.size(); i++) {
      if (s[i] == 255) {
         uncompressed.push_back(s[++i]);
      } else {
         Symbol sym = symbols.symbols[s[i]];
         uncompressed.append((char*)sym.buffer,sym.length);
      }
   }
   return uncompressed;
}

void compressAdaptive(ifstream& in, unsigned sampleLimit, unsigned sampleRepeat) {
   vector<string> data;
   unsigned totSize = 0, inSize = 0, outSize = 0;

   auto compressData = [&]() {
      vector<string> sample;
      unsigned sampleSize = 0;
      random_shuffle(data.begin(), data.end()); // hack: should actually sample instead
      for (auto& s : data) {
         sample.push_back(s);
         sampleSize += s.size();
         if (sampleSize>sampleLimit)
            break;
      }
      SymbolMap symbolMap = buildSymbolMap(sample, sampleSize);
      outSize += symbolMap.finalize();

      for (auto& str : data) {
         string compressed = compress(symbolMap, str);
         outSize += compressed.size();
         string decompressed = decompress(symbolMap, compressed);
         assert(str == decompressed);
      }
   };

   string line;
   while (getline(in,line)) {
      data.push_back(line + '\n');
      inSize += line.size() + 1;
      if (inSize > sampleRepeat) {
         compressData();
         totSize += inSize; inSize = 0;
         data.clear();
      }
   }
   if (!data.empty()) compressData();
   inSize += totSize;

   cerr << "original: " << inSize << ", compressed " << outSize << " (" << (static_cast<double>(inSize)/outSize) << ")" << endl;
}

/// Find longest expansion
inline u16 fastExpansion(u16 index1[256], u8 index2[256*256+1], uint64_t words[512], uint64_t masks[512], uint64_t word) {
   // check long symbols first
   unsigned first2 = word & 0xFFFF, first = word & 0xFF;
   unsigned begin = index2[first2], end = index2[first2+1];

   switch (end-begin) {
      case 0:
         return index1[first];
      case 1:
         if ((word & masks[begin]) == words[begin])
            return begin;
         return index1[first];
      case 2:
         if ((word & masks[begin]) == words[begin])
            return begin;
         if ((word & masks[begin+1]) == words[begin+1])
            return begin+1;
         return index1[first];
      default:
         for (unsigned i=begin; i<end; i++)
            if ((word & masks[i]) == words[i])
               return i;
         return index1[first];
   }
}

void compressBulk(ifstream& in, unsigned sampleLimit) {
   vector<string> all;
   string line;
   while (getline(in,line))
      all.push_back(line);

   vector<string> data; data.push_back("");
   for (auto& line : all)
      data[0].append(line + '\n');

   random_shuffle(all.begin(), all.end());
   vector<string> sample; sample.push_back("");
   for (auto& line : all) {
      sample[0].append(line + '\n');
      if (sample[0].size()>sampleLimit)
         break;
   }

   unsigned n = data[0].size();
   SymbolMap symbolMap;
{
   PerfEventBlock b(8*1024*1024);
   symbolMap = buildSymbolMap(sample, sample[0].size());
}
   const char* cur = data[0].data();
   const char* end = data[0].data()+n;
   vector<char> outVector(n*8);
   char* out = outVector.data();

{
   PerfEventBlock b(n);
   if (n>8) {
      u64 words[512];
      u64 masks[512];
      u8 length[512];
      for (unsigned i=0; i<512; i++) {
         auto& s = symbolMap.symbols[i];
         words[i] = s.word;
         masks[i] = ~0ull >> ((8-s.length)*8);
         length[i] = s.length;
      }
      while (cur<end) {
         u16 code = fastExpansion(symbolMap.index1, symbolMap.index2, words, masks, *((u64*)cur));
         if (__builtin_expect(isEscapeCode(code),0)) {
            *out++ = static_cast<char>(255);
            *out++ = *cur++;
         } else {
            *out++ = code;
            cur += length[code];
         }
      }
      end+=8;
   }

   while (cur<end) {
      u16 code = symbolMap.findExpansion(Symbol(cur,end));
      if (isEscapeCode(code)) {
         *out++ = static_cast<char>(255);
         *out++ = *cur++;
      } else {
         *out++ = code;
         cur += symbolMap.symbols[code].length;
      }
   }
}
cerr << ((double) n) / (out - outVector.data()) << endl;
}

int main(int argc,char* argv[]) {
   if (argc < 2)
      return -1;
   ifstream in(argv[1]);
   unsigned sampleLimit = 16*1024;
   if (argc >= 3)
      sampleLimit = atoi(argv[2]);

   if (argc >= 4) {
      unsigned sampleRepeat = atoi(argv[3]);
      compressAdaptive(in, sampleLimit, sampleRepeat);
   } else {
      compressBulk(in, sampleLimit);
   }

   return 0;
}
