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
#include <iostream>
#include <fstream>
#include <string>
#include <cstring>
#include <vector>
#include <unordered_map>
#include <unordered_set>
#include <algorithm>
#include <numeric>
#include <cstring>
#include <queue>
#include <array>
#include "PerfEvent.hpp"
#include "sais.hxx"

using namespace std;

// Helper class to pick frequent subsets up to length 8
class SubsetSelect
{
   vector<char> data;
   vector<bool> used;

   public:
   /// Constructor
   SubsetSelect();
   /// Destructor
   ~SubsetSelect();

   /// Add a string for statistics computations
   void add(const string& s);
   /// Build a symbol table
   vector<uint64_t> buildSymbolTable();
};

SubsetSelect::SubsetSelect()
   // Constructor
{
   used.resize(256);
}

SubsetSelect::~SubsetSelect()
   // Destructor
{
}

static inline uint64_t limitTo(uint64_t v,unsigned len)
{
   unsigned garbageBits=(8-len)*8;
   return (v<<garbageBits)>>garbageBits;
}

static inline unsigned getSymbolLen(uint64_t v)
   // Get the length of a symbol
{
   return v?(8-(__builtin_clzll(v)>>3)):0;
}

inline bool contains0(uint64_t v,unsigned len)
   // Contains a 0 within a given length
{
   constexpr uint64_t highMask = 0x8080808080808080ull;
   constexpr uint64_t lowMask = 0x7F7F7F7F7F7F7F7Full;
   uint64_t high=v&highMask;
   uint64_t couldBe0=(~((v&lowMask)+lowMask))&highMask;
   return limitTo(couldBe0&(~high),len);
}

void SubsetSelect::add(const string& s)
{
   // Compute used
   for (char c:s)
      used[static_cast<unsigned char>(c)]=true;
   if (s.length()<2)
      return;

   // Remember the text
   data.insert(data.end(),s.begin(),s.end());
   data.push_back(0);
}

static unsigned computeGain(unsigned len,unsigned count)
{
   unsigned saved=(len-1)*count;
   return (len&&(saved>len))?(saved-len):0;
}

/// A symbol candidate
struct Candidate {
   /// The symbol
   uint64_t symbol;
   /// The count
   unsigned count;
   /// The gain
   unsigned gain;
   /// The position range
   unsigned from,to;
   /// The modification step
   unsigned modificationStep;

   /// Comparison
   bool operator<(const Candidate& o) const { return gain<o.gain; }
   /// Comparison
   bool operator>(const Candidate& o) const { return gain>o.gain; }
};

static vector<unsigned> computeLCP(const vector<char>& data,const vector<unsigned>& suffixArray)
   // Compute the longest common prefix array
{
   vector<unsigned> inverseSuffixArray;
   inverseSuffixArray.resize(suffixArray.size());
   for (unsigned index=0,limit=suffixArray.size();index!=limit;++index)
      inverseSuffixArray[suffixArray[index]]=index;

   vector<unsigned> lcp;
   lcp.resize(suffixArray.size());
   unsigned height=0;
   for (unsigned index=0,limit=suffixArray.size();index!=limit;++index) {
      auto pos=inverseSuffixArray[index];
      if (pos) {
         unsigned index2=suffixArray[pos-1];
         while ((data[index+height]==data[index2+height])&&(data[index+height]))
            ++height;
         lcp[pos]=height;
         if (height) --height;
      }
   }
   return lcp;
}

/// Helper class to remember modified positions
class BitMask
{
   private:
   /// The words
   vector<uint64_t> words;

   /// Construct ones
   static constexpr uint64_t getOnes(unsigned len) { return (~0ull)>>(64-len); }

   public:
   /// Resize
   void resize(unsigned size) { words.resize((size+63)/64); }

   /// Mark
   void mark(unsigned pos,unsigned len) {
      unsigned word=(pos>>6),ofs=(pos&63);
      if (ofs+len>64) {
         words[word]|=getOnes(64-ofs)<<ofs;
         words[word+1]|=getOnes(len-(64-ofs));
      } else {
         words[word]|=getOnes(len)<<ofs;
      }
   }
   /// Check if anything in the range is marked
   bool isAnyMarked(unsigned pos,unsigned len) const {
      unsigned word=(pos>>6),ofs=(pos&63);
      if (ofs+len>64) {
         return (words[word]&(getOnes(64-ofs)<<ofs))||(words[word+1]&getOnes(len-(64-ofs)));
      } else {
         return words[word]&(getOnes(len)<<ofs);
      }
   }
   /// Return the unmarked entries
   unsigned getUnmarked(unsigned pos,unsigned len) const {
      unsigned word=(pos>>6),ofs=(pos&63);
      if (ofs+len>64) {
         return len-(__builtin_popcountll(words[word]&(getOnes(64-ofs)<<ofs))+__builtin_popcountll(words[word+1]&getOnes(len-(64-ofs))));
      } else {
         return len-__builtin_popcountll(words[word]&(getOnes(len)<<ofs));
      }
   }
};

static void recomputeGain(Candidate& c,const vector<unsigned>& suffixArray,const BitMask& modified)
   // Recompute the gain of an entry
{
   unsigned len=getSymbolLen(c.symbol);
#if 1
   unsigned invalid=0;
   for (auto iter=suffixArray.data()+c.from,limit=suffixArray.data()+c.to;iter<limit;++iter)
      invalid+=modified.isAnyMarked(*iter,len);
   c.count=(c.to-c.from)-invalid;
   c.gain=computeGain(len,c.count);
#else
   unsigned sumGain=0;
   for (auto iter=suffixArray.data()+c.from,limit=suffixArray.data()+c.to;iter<limit;++iter) {
      unsigned unmarked=modified.getUnmarked(*iter,len);
      if (unmarked) sumGain+=unmarked-1;
   }
   if (sumGain>len) sumGain-=len; else sumGain=0;
   c.gain=sumGain;
#endif
}

static void invalidatePositions(const Candidate& c,const vector<unsigned>& suffixArray,BitMask& modified)
   // Invalidate all positions
{
   unsigned len=getSymbolLen(c.symbol);
   for (auto iter=suffixArray.data()+c.from,limit=suffixArray.data()+c.to;iter<limit;++iter)
      modified.mark(*iter,len);
}

vector<uint64_t> SubsetSelect::buildSymbolTable()
   /// Build a symbol table
{
   // Compute the suffix array
   vector<unsigned> suffixArray;
   suffixArray.resize(data.size());
   saisxx(reinterpret_cast<unsigned char*>(data.data()),reinterpret_cast<int*>(suffixArray.data()),static_cast<int>(data.size()));

   // Append \0 to allow for unchecked reads
   {
      char buffer[8]={0};
      data.insert(data.end(),buffer,buffer+8);
   }

   // Build candidates heap
   priority_queue<Candidate,vector<Candidate>> candidates;
   {
      // Compute the longest common prefix array
      vector<unsigned> lcp=computeLCP(data,suffixArray);

      // Build the entries
      array<priority_queue<Candidate,vector<Candidate>,greater<Candidate>>,9> perSizeLists;
      unsigned begins[9]={0};
      unsigned currentDepth=0;
      auto flushTo=[this,&begins,&perSizeLists,&suffixArray,&currentDepth](unsigned pos,unsigned targetLevel) {
         for (unsigned len=max<unsigned>(targetLevel+1,2),lenLimit=currentDepth;len<=lenLimit;++len) {
            unsigned count=pos-begins[len];
            unsigned gain=computeGain(len,count);
            constexpr unsigned maxQueueSize = 8*256;
            if (gain&&((perSizeLists[len].size()<maxQueueSize)||(gain>perSizeLists[len].top().gain))) {
               uint64_t symbol=limitTo(*reinterpret_cast<uint64_t*>(data.data()+suffixArray[pos-1]),len);
               perSizeLists[len].push(Candidate{symbol,count,gain,begins[len],pos,0});
               if (perSizeLists[len].size()>maxQueueSize)
                  perSizeLists[len].pop();
            }
         }
      };
      for (unsigned index=0,limit=lcp.size();index!=limit;++index) {
         unsigned newDepth=lcp[index];
         if (newDepth>8) newDepth=8;
         if (newDepth<currentDepth) {
            flushTo(index,newDepth);
         } else if (newDepth>currentDepth) {
            for (unsigned level=currentDepth+1;level<=newDepth;++level)
               begins[level]=index-1;
         }
         currentDepth=newDepth;
      }
      flushTo(lcp.size(),0);

      // Build the candidates
      for (auto& list:perSizeLists) {
         while (!list.empty()) {
            candidates.push(list.top());
            list.pop();
         }
      }
   }

   // Built the result table
   BitMask modified[8];
   for(auto &m:modified)
      m.resize(suffixArray.size());
   vector<uint64_t> result;
   for (unsigned index=0;index!=256;++index) {
      if (used[index]||candidates.empty()) {
         result.push_back(index);
         continue;
      }

      // Pick the best choice
      auto best=candidates.top();
      unsigned len = 1;
      candidates.pop();

      // Recompute gain if needed
      if (best.modificationStep<index) {
         recomputeGain(best,suffixArray,modified[len-1]);
         if (best.gain) {
            best.modificationStep=index;
            candidates.push(best);
	 }
         --index;
         continue;
      }

      // Pick choice
      result.push_back(best.symbol);

      // And update
      for(unsigned index=0; index<len; index++)
      	invalidatePositions(best,suffixArray,modified[index]);
   }
   return result;
}

static inline uint64_t loadString(const char* input,unsigned len)
{
   uint64_t str;
#if 1
   if (len>=8) {
      str=reinterpret_cast<const uint64_t*>(input)[0];
   } else if (!len) {
      str=1;
   } else if ((reinterpret_cast<uintptr_t>(input)&63)<=(64-8)) {
      str=limitTo(reinterpret_cast<const uint64_t*>(input)[0],len);
   } else {
      str=reinterpret_cast<const uint64_t*>(input+len-8)[0]>>(8*(8-len));
   }
#else
   str=0;
   memcpy(&str,input,min<unsigned>(len,8));
#endif
   return str;
}

/// A simple map from symbol to char
class SymbolMap {
   struct Entry { uint64_t symbol; char c; char len; };

   vector<Entry> entries;
   unsigned char table[257];

   public:
   SymbolMap() { }

   /// Insert
   void addEntry(uint64_t symbol,char c) {
      char len=getSymbolLen(symbol);
      if (len>1)
         entries.push_back({symbol,c,len});
   }
   /// Build the table
   void buildTable() {
      sort(entries.begin(),entries.end(),[](const Entry& a,const Entry& b) { return (a.symbol&0xFF)<(b.symbol&0xFF); });
      unsigned current=0;
      table[0]=0;
      for (unsigned index=0,limit=entries.size();index!=limit;++index) {
         unsigned v=entries[index].symbol&0xFF;
         if (v!=current) {
            for (unsigned index2=current+1;index2<v;++index2)
               table[index2]=index;
            table[v]=index;
            current=v;
         }
      }
      for (unsigned index2=current+1;index2<=256;++index2)
         table[index2]=entries.size();
      for (unsigned i=0; i<256; i++)
         sort(entries.begin()+table[i],entries.begin()+table[i+1],[](const Entry& a,const Entry& b) { return a.len>b.len; });
   }
   /// An expansion
   struct Expansion { char c; char len; };
   /// Find expansions
   unsigned findExpansions(const char* input,unsigned len,Expansion target[8]) const {
      Expansion* writer=target;
      uint64_t next=loadString(input,len);
      unsigned slot=next&0xFF;
      for (auto start=entries.data(),iter=start+table[slot],limit=start+table[slot+1];iter<limit;++iter)
#ifdef STRNCMP
         if (len >= (unsigned) iter->len && !strncmp(input,(char*) &iter->symbol, iter->len)) {
#else
         if ((next&limitTo(~0ull,iter->len))==iter->symbol) {
#endif
            writer->c=iter->c;
            writer->len=iter->len;
            ++writer;
         }
      return writer-target;
   }
   /// Find longest expansion
   Expansion findExpansion(const char* input,unsigned len) const {
      uint64_t next=loadString(input,len);
      unsigned slot=next&0xFF;
      for (auto start=entries.data(),iter=start+table[slot],limit=start+table[slot+1];iter<limit;++iter)
#ifdef STRNCMP
         if (len >= (unsigned) iter->len && !strncmp(input,(char*) &iter->symbol, iter->len)) 
#else
         if ((next&limitTo(~0ull,iter->len))==iter->symbol)
#endif
            return {iter->c,iter->len};
      return {input[0],1};
   }

};

static void compress128(const SymbolMap& symbols,string& result,const char* data,unsigned len)
   // Compress up to 128 chars
{
   // Initialize DP table
   struct DPEntry { unsigned char prev,cost; char c; char pad; };
   SymbolMap::Expansion expansions[8];
   DPEntry dpTable[129];
   for (unsigned index=1;index<=len;++index)
      dpTable[index].cost=len+1;
   dpTable[0].prev=0;
   dpTable[0].cost=0;
   dpTable[0].c=0;

   // Fill DP table
   for (unsigned index=0;index!=len;++index) {
      // We can always advance one step
      unsigned cost=dpTable[index].cost;
      if (cost+1<=dpTable[index+1].cost) {
         auto& d=dpTable[index+1];
         d.prev=index;
         d.cost=cost+1;
         d.c=data[index];
      }

      // Try multi-step advances
      unsigned count=symbols.findExpansions(data+index,len-index,expansions);
      for (auto iter=expansions,limit=expansions+count;iter!=limit;++iter) {
         if (cost+1<dpTable[index+iter->len].cost) {
            auto& d=dpTable[index+iter->len];
            d.prev=index;
            d.cost=cost+1;
            d.c=iter->c;
         }
      }
   }

   // Recover
   unsigned compressedSize=0;
   for (auto index=len;index;index=dpTable[index].prev)
      ++compressedSize;
   result.resize(result.size()+compressedSize);
   auto writer=((char*) result.data())+result.size();
   for (auto index=len;index;index=dpTable[index].prev)
      *(--writer)=dpTable[index].c;
}

string compress(const SymbolMap& symbols,const string& line)
{
   string result;
   for (unsigned index=0,limit=line.length();index!=limit;) {
      unsigned chunk=limit-index;
      if (chunk>128) chunk=128;
      compress128(symbols,result,line.data()+index,chunk);
      index+=chunk;
   }
   return result;
}

string compressGreedy(const SymbolMap& symbols,const string& line)
{
   string result;
   auto data = line.data();
   auto len = line.size();
   for (unsigned index=0; index<len;) {
      SymbolMap::Expansion expansion=symbols.findExpansion(data+index,len-index);
#if 0
      if (index+1 < len) {
         SymbolMap::Expansion expansion2=symbols.findExpansion(data+index+1,-1+len-index);
	 if (expansion2.len > expansion.len+1) { 
            result.push_back(data[index]);
            index++;
	    continue;
         } 
      } 
#endif
      result.push_back(expansion.c);
      index+=expansion.len;
   }
   return result;
}

string decompress(const string& compressed,const vector<uint64_t>& table)
{
   string result;
   for (char c:compressed) {
      union { uint64_t v; char buffer[8]; };
      v=table[static_cast<unsigned char>(c)];
      result.append(buffer,getSymbolLen(v));
   }
   return result;
}

int main(int argc,char* argv[])
{
   unsigned original = 0;
   if (argc<2) {
      cerr << "usage: " << argv[0] << endl;
      return 1;
   }
   cerr << "reading" << endl;
   vector<string> data;
   {
      ifstream in(argv[1]);
      string line;
      while (getline(in,line)) {
         data.push_back(line + '\n');
         original += line.size() + 1;
      }
   }

   SubsetSelect select;
   for (auto& l:data)
      select.add(l);
   vector<uint64_t> table; 
   { PerfEventBlock b(8*1024*1024);
     table=select.buildSymbolTable();
   }


   unsigned unused=0;
   for (auto& e:table) {
      if (e>>8) {
         ++unused;
      }
   }

   cerr << "used: " << (256-unused) << ", unused " << unused << endl;
   SymbolMap symbols;
   for (unsigned index=0;index!=256;++index)
      symbols.addEntry(table[index],index);

   symbols.buildTable();


   unsigned compressed=0;
   { PerfEventBlock b(original);
      for (auto& l:data) {
#ifdef GREEDY
         auto c=compressGreedy(symbols,l);
#else
         auto c=compress(symbols,l);
#endif
         //auto d=decompress(c,table);
         //assert(l==d);
         compressed+=c.length();
      }
   }
   //cerr << "original: " << original << ", compressed " << compressed << endl;
   cerr <<  static_cast<double>(original)/compressed << endl;
   return 0;
}
