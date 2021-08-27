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
#include "PerfEvent.hpp"
#include "fsst.h"
#include "PerfEvent.hpp"
#include <algorithm>
#include <fstream>
#include <iostream>
#include <random>
#include <string>
#include <vector>
#include <map>
#include <lz4.h>

using namespace std;

/// Base class for all compression tests.
class CompressionRunner {
   public:
   /// Store the compressed corpus. Returns the compressed size
   virtual uint64_t compressCorpus(const vector<string>& data, unsigned long &bareSize, double &bulkTime, double& compressionTime, bool verbose) = 0;
   /// Decompress some selected rows, separated by newlines. The line number are in ascending order. The target buffer is guaranteed to be large enough
   virtual uint64_t decompressRows(vector<char>& target, const vector<unsigned>& lines) = 0;
};

/// No compresssion. Just used for debugging
class NoCompressionRunner : public CompressionRunner {
   private:
   /// The uncompressed data
   vector<string> data;

   public:
   /// Store the compressed corpus. Returns the compressed size
   uint64_t compressCorpus(const vector<string>& data, unsigned long& bareSize, double& bulkTime, double& compressionTime, bool /*verbose*/) override {
      auto startTime = std::chrono::steady_clock::now();
      this->data = data;
      uint64_t result = sizeof(uint32_t);
      for (auto& d : data)
         result += d.length() + sizeof(uint32_t);
      auto stopTime = std::chrono::steady_clock::now();
      bareSize = result;
      bulkTime = compressionTime = std::chrono::duration<double>(stopTime - startTime).count();
      return result;
   }
   /// Decompress some selected rows, separated by newlines. The line number are in ascending order. The target buffer is guaranteed to be large enough
   virtual uint64_t decompressRows(vector<char>& target, const vector<unsigned>& lines) {
      char* writer = target.data();
      for (auto l : lines) {
         auto& s = data[l];
         auto len = s.length();
         memcpy(writer, s.data(), len);
         writer[len] = '\n';
         writer += len + 1;
      }
      return writer - target.data();
   }
};

/// FSST compression
class FSSTCompressionRunner : public CompressionRunner {
   private:
   /// The decode
   fsst_decoder_t decoder;
   /// The compressed data
   vector<unsigned char> compressedData;
   /// The offsets
   vector<unsigned> offsets;

   public:
   FSSTCompressionRunner() {}
   FSSTCompressionRunner(unsigned /*blockSizeIgnored*/) {}

   /// Store the compressed corpus. Returns the compressed size
   uint64_t compressCorpus(const vector<string>& data, unsigned long& bareSize, double& bulkTime, double& compressionTime, bool verbose) override {
      compressedData.clear();
      offsets.clear();

      vector<unsigned long> rowLens, compressedRowLens;
      vector<unsigned char*> rowPtrs, compressedRowPtrs;
      rowLens.reserve(data.size());
      compressedRowLens.resize(data.size());
      rowPtrs.reserve(data.size());
      compressedRowPtrs.resize(data.size() + 1);
      unsigned long totalLen = 0;
      for (auto& d : data) {
         totalLen += d.size();
         rowLens.push_back(d.size());
         rowPtrs.push_back(reinterpret_cast<unsigned char*>(const_cast<char*>(d.data())));
      }

      auto firstTime = std::chrono::steady_clock::now();
      vector<unsigned long> dummy;
      if (getenv("LOOP"))
         for (int i = 0; i < 10000; i++) fsst_destroy(fsst_create(data.size(), rowLens.data(), rowPtrs.data(), false));
      auto encoder = fsst_create(data.size(), rowLens.data(), rowPtrs.data(), false);
      auto createTime = std::chrono::steady_clock::now();
      vector<unsigned char> compressionBuffer, fullBuffer;
      fullBuffer.resize(totalLen);
      unsigned char *fullBuf = fullBuffer.data();
      unsigned stringEnd = 0;
      for (auto& d : data) {
         memcpy(fullBuf + stringEnd, d.data(), d.length());
         stringEnd += d.length();
      }
      compressionBuffer.resize(16 + 2 * totalLen);
      auto copyTime = std::chrono::steady_clock::now();
      fsst_compress(encoder, 1, &totalLen, &fullBuf, compressionBuffer.size(), compressionBuffer.data(), compressedRowLens.data(), compressedRowPtrs.data());
      auto startTime = std::chrono::steady_clock::now();
      fsst_compress(encoder, data.size(), rowLens.data(), rowPtrs.data(), compressionBuffer.size(), compressionBuffer.data(), compressedRowLens.data(), compressedRowPtrs.data());
      auto stopTime = std::chrono::steady_clock::now();
      unsigned long compressedLen = data.empty() ? 0 : (compressedRowPtrs[data.size() - 1] + compressedRowLens[data.size() - 1] - compressionBuffer.data());

      compressedData.resize(compressedLen + 8192);
      memcpy(compressedData.data(), compressionBuffer.data(), compressedLen);
      offsets.reserve(data.size());
      compressedRowPtrs[data.size()] = compressionBuffer.data() + compressedLen;
      for (unsigned index = 0, limit = data.size(); index != limit; ++index)
         offsets.push_back(compressedRowPtrs[index + 1] - compressionBuffer.data());
      bareSize = compressedData.size();
      uint64_t result = bareSize + (offsets.size() * sizeof(unsigned));
      {
         unsigned char buffer[sizeof(fsst_decoder_t)];
         unsigned dictLen = fsst_export(encoder, buffer);
         fsst_destroy(encoder);
         result += dictLen;

         fsst_import(&decoder, buffer);
      }
      double oneTime = std::chrono::duration<double>(createTime - firstTime).count();
      bulkTime = std::chrono::duration<double>(startTime - copyTime).count();
      compressionTime = std::chrono::duration<double>(stopTime - startTime).count();
      if (verbose) {
         cout << "# symbol table construction time: " << oneTime << endl;
         cout << "# compress-bulk time: " << bulkTime << endl;
         cout << "# compress time: " << compressionTime << endl;
      }
      bulkTime += oneTime;
      compressionTime += oneTime;

      return result;
   }
   /// Decompress some selected rows, separated by newlines. The line number are in ascending order. The target buffer is guaranteed to be large enough
   virtual uint64_t decompressRows(vector<char>& target, const vector<unsigned>& lines) {
      char* writer = target.data();
      auto limit = writer + target.size();

      auto data = compressedData.data();
      auto offsets = this->offsets.data();
      for (auto l : lines) {
         auto start = l ? offsets[l - 1] : 0, end = offsets[l];
         unsigned len = fsst_decompress(&decoder, end - start, data + start, limit - writer, reinterpret_cast<unsigned char*>(writer));
         writer[len] = '\n';
         writer += len + 1;
      }
      return writer - target.data();
   }
};

/// LZ4 compression with a given block size
class LZ4CompressionRunner : public CompressionRunner {
   private:
   /// An uncompressed block
   struct Block {
      /// The row count
      unsigned rows;
      /// The row offsets
      unsigned offsets[];

      /// Get the string offer
      char* data() { return reinterpret_cast<char*>(offsets + rows); }
   };
   /// A compressed block
   struct CompressedBlock {
      /// The compressed size
      unsigned compressedSize;
      /// The uncompressed size
      unsigned uncompressedSize;
      /// The compressed data
      char data[];
   };
   /// The block size
   unsigned blockSize;
   /// The blocks
   vector<CompressedBlock*> blocks;

   LZ4CompressionRunner(const LZ4CompressionRunner&) = delete;
   void operator=(const LZ4CompressionRunner&) = delete;

   public:
   /// Constructor. Sets the block size to the given number of rows
   explicit LZ4CompressionRunner(unsigned blockSize) : blockSize(blockSize) {}
   /// Destructor
   ~LZ4CompressionRunner() {
      for (auto b : blocks)
         free(b);
   }

   /// Store the compressed corpus. Returns the compressed size
   uint64_t compressCorpus(const vector<string>& data, unsigned long &bareSize, double &bulkTime, double& compressionTime, bool verbose) override {
      for (auto b : blocks)
         free(b);
      blocks.clear();

      bulkTime = compressionTime = 0;
      bareSize = 0;
      uint64_t result = 0;
      vector<char> compressionBuffer, blockBuffer;
      for (unsigned blockStart = 0, limit = data.size(); blockStart != limit;) {
         unsigned next = blockStart + blockSize;
         if (next > limit) next = limit;

         // Form a block of rows
         unsigned baseLen = sizeof(Block);
         for (unsigned index = blockStart; index != next; ++index)
            baseLen += data[index].length();
         unsigned len = baseLen + (sizeof(unsigned) * (next - blockStart));
         if (len > blockBuffer.size()) blockBuffer.resize(len);

         auto& block = *reinterpret_cast<Block*>(blockBuffer.data());
         block.rows = next - blockStart;
         unsigned maxLen = len + (len / 8) + 128;
         if (maxLen > compressionBuffer.size()) compressionBuffer.resize(maxLen);

         // just compress strings without the offsets, to measure that, also
         auto firstTime = std::chrono::steady_clock::now();
         bareSize += LZ4_compress_default(block.data(), compressionBuffer.data(), baseLen, maxLen);
         auto startTime = std::chrono::steady_clock::now();
         bulkTime += std::chrono::duration<double>(startTime - firstTime).count();

         char* strings = block.data();
         unsigned stringEnd = 0;
         for (unsigned index = blockStart; index != next; ++index) {
            memcpy(strings + stringEnd, data[index].data(), data[index].length());
            stringEnd += data[index].length();
            block.offsets[index - blockStart] = stringEnd;
         }

         // Compress it
         unsigned lz4Len = LZ4_compress_default(blockBuffer.data(), compressionBuffer.data(), len, maxLen);
         auto stopTime = std::chrono::steady_clock::now();
         compressionTime += std::chrono::duration<double>(stopTime - startTime).count();

         // And store the compressed data
         result += sizeof(CompressedBlock) + lz4Len;
         auto compressedBlock = static_cast<CompressedBlock*>(malloc(sizeof(CompressedBlock) + lz4Len));
         compressedBlock->compressedSize = lz4Len;
         compressedBlock->uncompressedSize = len;
         memcpy(compressedBlock->data, compressionBuffer.data(), lz4Len);

         blocks.push_back(compressedBlock);
         blockStart = next;
      }
      if (verbose)
         cout << "# compress time: " << compressionTime << endl;
      return result;
   }
   /// Decompress some selected rows, separated by newlines. The line number are in ascending order. The target buffer is guaranteed to be large enough
   virtual uint64_t decompressRows(vector<char>& target, const vector<unsigned>& lines) {
      char* writer = target.data();
      vector<char> decompressionBuffer;
      unsigned currentBlock = 0;
      for (auto l : lines) {
         // Switch block on demand
         if (decompressionBuffer.empty() || (l < (currentBlock * blockSize)) || (l >= ((currentBlock + 1) * blockSize))) {
            currentBlock = l / blockSize;
            auto compressedBlock = blocks[currentBlock];
            if (decompressionBuffer.size() < compressedBlock->uncompressedSize) decompressionBuffer.resize(compressedBlock->uncompressedSize);
            LZ4_decompress_safe(compressedBlock->data, decompressionBuffer.data(), compressedBlock->compressedSize, compressedBlock->uncompressedSize);
         }

         // Unpack the string
         unsigned localOfs = l - (currentBlock * blockSize);
         auto& block = *reinterpret_cast<Block*>(decompressionBuffer.data());
         auto start = localOfs ? block.offsets[localOfs - 1] : 0;
         auto end = block.offsets[localOfs];
         auto len = end - start;
         memcpy(writer, block.data() + start, len);
         writer[len] = '\n';
         writer += len + 1;
      }
      return writer - target.data();
   }
};

static pair<bool, vector<pair<unsigned, double>>> doTest(CompressionRunner& runner, const vector<string>& files, bool verbose)
// Test a runner for a given number of files
{
   uint64_t totalSize = 0;
   bool debug = getenv("DEBUG");
   NoCompressionRunner debugRunner;
   map<unsigned, vector<pair<double, unsigned>>> timings;
   constexpr unsigned repeat = 100;
   for (auto& file : files) {
      // Read the corpus
      vector<string> corpus;
      uint64_t corpusLen = 0;
      {
         ifstream in(file);
         if (!in.is_open()) {
            cerr << "unable to open " << file << endl;
            return {false, {}};
         }
         string line;
         while (getline(in, line)) {
            corpusLen += line.length() + 1;
            corpus.push_back(move(line));
            if (corpusLen > 7000000) break;
         }
      }
      corpusLen += 4096;

      // Compress it
      double bulkTime, compressionTime;
      unsigned long bareSize;
      totalSize += runner.compressCorpus(corpus, bareSize, bulkTime, compressionTime, verbose);
      if (debug) {
         double ignored;
         debugRunner.compressCorpus(corpus, bareSize, ignored, ignored, false);
      }

      // Prepare row counts
      vector<unsigned> shuffledRows;
      for (unsigned index = 0, limit = corpus.size(); index != limit; ++index)
         shuffledRows.push_back(index);
      {
         // Use an explicit seed to get reproducibility
         mt19937 g(123);
         shuffle(shuffledRows.begin(), shuffledRows.end(), g);
      }

      // Test different selectivities
      vector<char> targetBuffer, debugBuffer;
      targetBuffer.resize(corpusLen);
      if (debug) debugBuffer.resize(corpusLen);
      for (unsigned sel : {1,3,10,30,100}) {
         auto hits = shuffledRows;
         hits.resize(hits.size() * sel / 100);
         if (hits.empty()) continue;
         sort(hits.begin(), hits.end());

         unsigned len = 0;
         for (unsigned index = 0; index != repeat; ++index)
            len = runner.decompressRows(targetBuffer, hits);

         auto startTime = std::chrono::steady_clock::now();
         len = 0;
         for (unsigned index = 0; index != repeat; ++index)
            len = runner.decompressRows(targetBuffer, hits);
         auto stopTime = std::chrono::steady_clock::now();

         timings[sel].push_back(pair<double, unsigned>(std::chrono::duration<double>(stopTime - startTime).count(), hits.size()));

         if (debug) {
            unsigned len2 = debugRunner.decompressRows(debugBuffer, hits);
            if ((len != len2) || (memcmp(targetBuffer.data(), debugBuffer.data(), len) != 0)) {
               cerr << "result mismatch" << endl;
               return {false, {}};
            }
         }
      }
   }

   if (verbose)
      cout << "# total compress size: " << totalSize << endl;
   vector<pair<unsigned, double>> result;
   for (auto& t : timings) {
      double prod1 = 1, prod2 = 1;
      for (auto e : t.second) {
         prod1 *= e.first;
         prod2 *= (e.second / e.first) * repeat / 1000;
      }
      prod1 = pow(prod1, 1.0 / t.second.size());
      prod2 = pow(prod2, 1.0 / t.second.size());
      if (verbose)
         cout << t.first << " " << prod1 << " " << prod2 << endl;
      result.push_back({t.first, prod2});
   }
   return {true, result};
}

template <class T>
void cmpCase(unsigned blockSize, const string& file) {
   unsigned long bareSize = 0, totalSize = 0;
   double bulkTime = 0, compressionTime = 0, decompressionTime = 0, compressionRatio;
   T runner(blockSize);
   constexpr unsigned repeat = 100;
   {
      // Read the corpus
      vector<string> corpus;
      uint64_t corpusLen = 0;
      constexpr uint64_t targetLen = 8 << 20;
      {
         ifstream in(file);
         if (!in.is_open()) {
            cerr << "unable to open " << file << endl;
            exit(1);
         }
         string line;
         while (getline(in, line)) {
            corpusLen += line.length() + 1;
            corpus.push_back(move(line));
            if (corpusLen > targetLen) break;
         }
         if (corpus.empty()) return;
         unsigned reader = 0;
         while (corpusLen < targetLen) {
            corpusLen += corpus[reader].length() + 1;
            corpus.push_back(corpus[reader++]);
         }
      }

      // Compress it
      totalSize += runner.compressCorpus(corpus, bareSize, bulkTime, compressionTime, false);
      compressionRatio = static_cast<double>(corpusLen) / totalSize;

      // Prepare hits vector counts
      vector<unsigned> hits;
      for (unsigned index = 0, limit = corpus.size(); index != limit; ++index)
         hits.push_back(index);

      vector<char> targetBuffer;
      targetBuffer.resize(corpusLen + 4096);
      {
         for (unsigned index = 0; index != repeat; ++index) {
            runner.decompressRows(targetBuffer, hits);
         }
         auto startTime = std::chrono::steady_clock::now();
         for (unsigned index = 0; index != repeat; ++index) {
            runner.decompressRows(targetBuffer, hits);
         }
         auto stopTime = std::chrono::steady_clock::now();
         decompressionTime += std::chrono::duration<double>(stopTime - startTime).count();
      }
      cout << "\t" << static_cast<double>(corpusLen)/bareSize << "\t" << (corpusLen/bulkTime)/(1<<20) << "\t" << compressionRatio << "\t" << (corpusLen/compressionTime)/(1<<20) << "\t" << (corpusLen*repeat/decompressionTime)/(1<<20);
   }
}

template <class T>
vector<pair<unsigned, double>> cmpFilter(unsigned blockSize, const vector<string>& files) {
   T runner(blockSize);
   auto res = doTest(runner, files, false);
   if (!res.first) exit(1);
   return res.second;
}

int main(int argc, const char* argv[]) {
   if (argc < 3)
      return -1;

   string method = argv[1];
   int blockSize = atoi(argv[2]);
   vector<string> files;
   for (int index = 3; index < argc; ++index) {
      string f = argv[index];
      if (f == "--exclude") {
         auto iter = find(files.begin(), files.end(), argv[++index]);
         if (iter != files.end()) files.erase(iter);
      } else {
         files.push_back(move(f));
      }
   }

   if (method == "nocompression") {
      NoCompressionRunner runner;
      return !doTest(runner, files, true).first;
   } else if (method == "fsst") {
      FSSTCompressionRunner runner;
      return !doTest(runner, files, true).first;
   } else if (method == "lz4") {
      LZ4CompressionRunner runner(blockSize);
      return !doTest(runner, files, true).first;
   } else if (method == "compare") {
      cout << "file";
      for (auto name : {"FSST", "LZ4"})
         cout << "\t" << name << "-brate\t" << "\t" << name << "-bMB/s\t" << "\t" << name << "-crate\t" << name << "-cMB/s\t" << name << "-dMB/s";
      cout << endl;
      for (auto& file : files) {
         string name = file;
         if (name.rfind('/') != string::npos)
            name = name.substr(name.rfind('/') + 1);
         cout << name;
         cmpCase<FSSTCompressionRunner>(blockSize, file);
         cmpCase<LZ4CompressionRunner>(blockSize, file);
         cout << endl;
      }
   } else if (method == "comparefilter") {
      auto r1 = cmpFilter<LZ4CompressionRunner>(blockSize, files);
      auto r2 = cmpFilter<FSSTCompressionRunner>(blockSize, files);
      cout << "sel\tlz4\tfsst" << endl;
      for (unsigned index = 0; index != r1.size(); ++index)
         cout << r1[index].first << "\t" << r1[index].second << "\t" << r2[index].second << endl;
   } else {
      cerr << "unknown method " << method << endl;
      return 1;
   }
}
