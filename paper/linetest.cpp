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
#include <algorithm>
#include <fstream>
#include <iostream>
#include <random>
#include <string>
#include <vector>
#include <lz4.h>
#include <zdict.h>

using namespace std;

/// Base class for all compression tests.
class CompressionRunner {
   public:
   /// Store the compressed corpus. Returns the compressed size
   virtual uint64_t compressCorpus(const vector<string>& data, double& compressionTime, bool verbose) = 0;
   /// Decompress a single row. The target buffer is guaranteed to be large enough
   virtual uint64_t decompressRow(vector<char>& target, unsigned line) = 0;
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
   /// Store the compressed corpus. Returns the compressed size
   uint64_t compressCorpus(const vector<string>& data, double& compressionTime, bool verbose) override {
      compressedData.clear();
      offsets.clear();

      vector<unsigned long> rowLens, compressedRowLens;
      vector<unsigned char*> rowPtrs, compressedRowPtrs;
      rowLens.reserve(data.size());
      compressedRowLens.resize(data.size());
      rowPtrs.reserve(data.size());
      compressedRowPtrs.resize(data.size() + 1);
      unsigned totalLen = 0;
      for (auto& d : data) {
         totalLen += d.size();
         rowLens.push_back(d.size());
         rowPtrs.push_back(reinterpret_cast<unsigned char*>(const_cast<char*>(d.data())));
      }

      auto startTime = std::chrono::steady_clock::now();
      auto encoder = fsst_create(data.size(), rowLens.data(), rowPtrs.data(), false);
      auto createTime = std::chrono::steady_clock::now();
      vector<unsigned char> compressionBuffer;
      compressionBuffer.resize(16 + 2 * totalLen);
      auto compressTime = std::chrono::steady_clock::now();
      fsst_compress(encoder, data.size(), rowLens.data(), rowPtrs.data(), compressionBuffer.size(), compressionBuffer.data(), compressedRowLens.data(), compressedRowPtrs.data());
      auto stopTime = std::chrono::steady_clock::now();
      unsigned long compressedLen = data.empty() ? 0 : (compressedRowPtrs[data.size() - 1] + compressedRowLens[data.size() - 1] - compressionBuffer.data());

      compressedData.resize(compressedLen + 8192);
      memcpy(compressedData.data(), compressionBuffer.data(), compressedLen);
      offsets.reserve(data.size());
      compressedRowPtrs[data.size()] = compressionBuffer.data() + compressedLen;
      for (unsigned index = 0, limit = data.size(); index != limit; ++index)
         offsets.push_back(compressedRowPtrs[index + 1] - compressionBuffer.data());
      uint64_t result = compressedData.size() /*+ (offsets.size() * sizeof(unsigned))*/;
      {
         unsigned char buffer[sizeof(fsst_decoder_t)];
         unsigned dictLen = fsst_export(encoder, buffer);
         fsst_destroy(encoder);
         result += dictLen;

         fsst_import(&decoder, buffer);
      }
      if (verbose) {
         cout << "# symbol table construction time: " << std::chrono::duration<double>(createTime - startTime).count() << endl;
         cout << "# compress time: " << std::chrono::duration<double>(stopTime - compressTime).count() << endl;
      }
      compressionTime = std::chrono::duration<double>(createTime - startTime).count() + std::chrono::duration<double>(stopTime - compressTime).count();

      return result;
   }
   /// Decompress a single row. The target buffer is guaranteed to be large enough
   uint64_t decompressRow(vector<char>& target, unsigned line) override {
      char* writer = target.data();
      auto limit = writer + target.size();

      auto data = compressedData.data();
      auto offsets = this->offsets.data();
      auto start = line ? offsets[line - 1] : 0, end = offsets[line];
      unsigned len = fsst_decompress(&decoder, end - start, data + start, limit - writer, reinterpret_cast<unsigned char*>(writer));
      return len;
   }
};

/// LZ4 compression that compresses each  line separately
class LZ4CompressionRunner : public CompressionRunner {
   private:
   /// The compressed data
   vector<char> compressedData;
   /// The offsets
   vector<unsigned> offsets;

   LZ4CompressionRunner(const LZ4CompressionRunner&) = delete;
   void operator=(const LZ4CompressionRunner&) = delete;

   public:
   LZ4CompressionRunner() = default;

   /// Store the compressed corpus. Returns the compressed size
   uint64_t compressCorpus(const vector<string>& data, double& compressionTime, bool verbose) override {
      unsigned maxLen = 0;
      for (auto& d : data)
         if (d.length() > maxLen)
            maxLen = d.length();
      maxLen += (maxLen / 8) + 128;
      vector<char> compressionBuffer(maxLen);
      compressedData.clear();
      offsets.clear();
      offsets.reserve(data.size());

      auto startTime = std::chrono::steady_clock::now();
      for (auto& d : data) {
         unsigned lz4Len = LZ4_compress_default(d.data(), compressionBuffer.data(), d.length(), maxLen);
         compressedData.insert(compressedData.end(), compressionBuffer.data(), compressionBuffer.data() + lz4Len);
         offsets.push_back(compressedData.size());
      }
      auto stopTime = std::chrono::steady_clock::now();
      compressionTime = std::chrono::duration<double>(stopTime - startTime).count();

      if (verbose)
         cout << "# compress time: " << compressionTime << endl;
      return compressedData.size() /*+ (offsets.size() * sizeof(unsigned))*/;
   }
   /// Decompress a single row. The target buffer is guaranteed to be large enough
   uint64_t decompressRow(vector<char>& target, unsigned line) override {
      auto offsets = this->offsets.data();
      auto start = line ? offsets[line - 1] : 0, end = offsets[line];
      return LZ4_decompress_safe(compressedData.data() + start, target.data(), end - start, target.size());
   }
};

/// LZ4 compression that compresses each  line separately but uses a global dictionary
class LZ4DictCompressionRunner : public CompressionRunner {
   private:
   /// The compressed data
   vector<char> compressedData;
   /// The offsets
   vector<unsigned> offsets;
   /// The dictionary
   vector<char> dict;
   /// Decompression object
   LZ4_streamDecode_t* decompressor;

   LZ4DictCompressionRunner(const LZ4DictCompressionRunner&) = delete;
   void operator=(const LZ4DictCompressionRunner&) = delete;

   public:
   LZ4DictCompressionRunner() { decompressor = LZ4_createStreamDecode(); }
   ~LZ4DictCompressionRunner() { LZ4_freeStreamDecode(decompressor); }

   /// Store the compressed corpus. Returns the compressed size
   uint64_t compressCorpus(const vector<string>& data, double& compressionTime, bool verbose) override {
      unsigned maxLen = 0;
      for (auto& d : data)
         if (d.length() > maxLen)
            maxLen = d.length();
      maxLen += (maxLen / 8) + 128;
      vector<char> compressionBuffer(maxLen);
      compressedData.clear();
      offsets.clear();
      offsets.reserve(data.size());

      // Train a dictionary
      constexpr unsigned dictSize = 8 << 10;
      {
         // Form a sample
         auto sampleData = data;
         {
            // Use an explicit seed to get reproducibility
            mt19937 g(321);
            shuffle(sampleData.begin(), sampleData.end(), g);
         }
         constexpr unsigned sampleLimit = 64 << 10;
         string sample;
         vector<size_t> sampleLens;
         for (auto& line : sampleData) {
            if (line.size() < 2) continue;
            sample.append(line);
            sampleLens.push_back(line.size());
            if (sample.size() > sampleLimit)
               break;
         }

         dict.resize(dictSize);
         auto startTime = std::chrono::steady_clock::now();
         ZDICT_trainFromBuffer(dict.data(), dict.size(), sample.data(), sampleLens.data(), sampleLens.size());
         auto stopTime = std::chrono::steady_clock::now();
         compressionTime = std::chrono::duration<double>(stopTime - startTime).count();
      }

      auto startTime = std::chrono::steady_clock::now();
      auto stream = LZ4_createStream();
      for (auto& d : data) {
         LZ4_loadDict(stream, dict.data(), dict.size());
         unsigned lz4Len = LZ4_compress_fast_continue(stream, d.data(), compressionBuffer.data(), d.size(), maxLen, 1);
         compressedData.insert(compressedData.end(), compressionBuffer.data(), compressionBuffer.data() + lz4Len);
         offsets.push_back(compressedData.size());
      }
      LZ4_freeStream(stream);
      auto stopTime = std::chrono::steady_clock::now();
      compressionTime += std::chrono::duration<double>(stopTime - startTime).count();

      if (verbose)
         cout << "# compress time: " << compressionTime << endl;
      return compressedData.size() /*+ (offsets.size() * sizeof(unsigned))*/ + dict.size();
   }
   /// Decompress a single row. The target buffer is guaranteed to be large enough
   uint64_t decompressRow(vector<char>& target, unsigned line) override {
      LZ4_setStreamDecode(decompressor, dict.data(), dict.size());

      auto offsets = this->offsets.data();
      auto start = line ? offsets[line - 1] : 0, end = offsets[line];
      auto result = LZ4_decompress_safe_continue(decompressor, compressedData.data() + start, target.data(), end - start, target.size());

      return result;
   }
};

static tuple<bool, double, double, double> doTest(CompressionRunner& runner, const vector<string>& files, bool verbose)
// Test a runner for a given number of files
{
   double compressionSpeed = 0, decompressionSpeed = 0, compressionRatio = 0;
   for (auto& file : files) {
      // Read the corpus
      vector<string> corpus;
      uint64_t corpusLen = 0, maxLineLen = 0;
      constexpr uint64_t targetLen = 8 << 20;
      {
         ifstream in(file);
         if (!in.is_open()) {
            cerr << "unable to open " << file << endl;
            return {false, 0.0, 0.0, 0.0};
         }
         string line;
         while (getline(in, line)) {
            line.append("\n");
            corpusLen += line.length();
            if (line.length() > maxLineLen) maxLineLen = line.length();
            corpus.push_back(move(line));
            if (corpusLen > targetLen) break;
         }
         if (corpus.empty()) 
            return {false, 0.0, 0.0, 0.0};
         unsigned reader = 0;
         while (corpusLen < targetLen) {
            corpusLen += corpus[reader].length();
            corpus.push_back(corpus[reader++]);
         }
      }

      // Compress it
      double compressionTime = 0;
      compressionRatio += static_cast<double>(corpusLen) / runner.compressCorpus(corpus, compressionTime, verbose);
      compressionSpeed += static_cast<double>(corpusLen) / compressionTime;

      // Prepare row counts
      vector<unsigned> shuffledRows;
      for (unsigned index = 0, limit = corpus.size(); index != limit; ++index)
         shuffledRows.push_back(index);
      {
         // Use an explicit seed to get reproducibility
         mt19937 g(123);
         shuffle(shuffledRows.begin(), shuffledRows.end(), g);
      }

      // Decompress all lines (in a random order)
      vector<char> targetBuffer;
      targetBuffer.resize(maxLineLen + 128);

      auto startTime = std::chrono::steady_clock::now();
      constexpr unsigned repeat = 100;
      unsigned len;
      for (unsigned index = 0; index != repeat; ++index) {
         len = 0;
         for (unsigned line : shuffledRows)
            len += runner.decompressRow(targetBuffer, line);
      }
      auto stopTime = std::chrono::steady_clock::now();

      decompressionSpeed += (corpusLen*static_cast<double>(repeat)) / std::chrono::duration<double>(stopTime - startTime).count();

      if (len != corpusLen) {
         cerr << "result " << len << " mismatch " << corpusLen << endl;
         return {false, 0.0, 0.0, 0.0};
      }
   }
   if (files.size()) {
      // average the metrics over all files
      compressionRatio /= files.size();
      compressionSpeed /= files.size();
      decompressionSpeed /= files.size();
   }
   compressionSpeed /= 1 << 20; // convert to MB
   decompressionSpeed /= 1 << 20; // convert to MB;

   if (verbose) {
      cout << "# average compression ratio: " << compressionRatio << endl;
      cout << "# average compression speed in MB/s: " << compressionSpeed << endl;
      cout << "# average decompression speed in MB/s: " << decompressionSpeed << endl;
   }

   return {true, compressionSpeed, compressionRatio, decompressionSpeed};
}

template <class T>
void cmpCase(const string& file) {
   T runner;
   auto res = doTest(runner, {file}, false);
   if (!get<0>(res)) exit(1);
   cout << "\t" << get<1>(res) << "\t" << get<2>(res) << "\t" << get<3>(res);
}

int main(int argc, char* argv[]) {
   if (argc < 2)
      return -1;

   string method = argv[1];
   vector<string> files;
   for (int index = 2; index < argc; ++index) {
      string f = argv[index];
      if (f == "--exclude") {
         auto iter = find(files.begin(), files.end(), argv[++index]);
         if (iter != files.end()) files.erase(iter);
      } else {
         files.push_back(move(f));
      }
   }

   if (method == "fsst") {
      FSSTCompressionRunner runner;
      return !get<0>(doTest(runner, files, true));
   } else if (method == "lz4") {
      LZ4CompressionRunner runner;
      return !get<0>(doTest(runner, files, true));
   } else if (method == "lz4dict") {
      LZ4DictCompressionRunner runner;
      return !get<0>(doTest(runner, files, true));
   } else if (method == "compare") {
      cout << "file";
      for (auto name : {"FSST", "LZ4", "LZ4dict"})
         cout << "\t" << name << "-cMB/s\t" << name << "-crate\t" << name << "-dMB/s";
      cout << endl;
      for (auto& file : files) {
         string name = file;
         if (name.rfind('/') != string::npos)
            name = name.substr(name.rfind('/') + 1);
         cout << name;
         cmpCase<FSSTCompressionRunner>(file);
         cmpCase<LZ4CompressionRunner>(file);
         cmpCase<LZ4DictCompressionRunner>(file);
         cout << endl;
      }
   } else {
      cerr << "unknown method " << method << endl;
      return 1;
   }
}
