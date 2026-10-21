#ifdef FSST12
#include "../libfsst12.hpp"
using Length = unsigned long;
static_assert(sizeof(libfsst::Encoder) == sizeof(std::shared_ptr<libfsst::SymbolMap>),
              "Encoder must not retain construction counters");
#else
#include "../libfsst.hpp"
using Length = size_t;
#endif

#include <stdexcept>

namespace {
using Byte = unsigned char;
using EncoderPtr = std::unique_ptr<fsst_encoder_t, decltype(&fsst_destroy)>;

void check(bool condition, const char* expression, int line) {
   if (!condition)
      throw std::runtime_error("line " + std::to_string(line) + ": " + expression);
}
#define CHECK(expression) check((expression), #expression, __LINE__)

void roundTrip(std::vector<std::string> lines, bool zeroTerminated = false) {
   std::vector<Length> lengths;
   std::vector<const Byte*> inputs;
   Length total = 0;
   for (auto& line : lines) {
      if (zeroTerminated) line.push_back('\0');
      lengths.push_back(line.size());
      inputs.push_back(reinterpret_cast<const Byte*>(line.data()));
      total += line.size();
   }
   EncoderPtr encoder(fsst_create(lines.size(), lengths.data(), inputs.data(), zeroTerminated), fsst_destroy);
   CHECK(encoder != nullptr);
   std::vector<Byte> header(FSST_MAXHEADER);
   header.resize(fsst_export(encoder.get(), header.data()));
   CHECK(header.size() <= FSST_MAXHEADER);
   auto decoder = fsst_decoder(encoder.get());
   fsst_decoder_t imported;
   CHECK(fsst_import(&imported, header.data()) == header.size());

   auto compress = [&](fsst_encoder_t* current) {
      std::vector<Byte> output(2 * total + 8 * lines.size() + 32);
      std::vector<Length> outLengths(lines.size());
      std::vector<Byte*> outStrings(lines.size());
      CHECK(fsst_compress(current, lines.size(), lengths.data(), inputs.data(), output.size(),
                          output.data(), outLengths.data(), outStrings.data()) == lines.size());
      Length used = 0;
      for (size_t i = 0; i < lines.size(); ++i) {
         for (const auto* d : {&decoder, &imported}) {
            // Cover both exact-sized output and the wide-store decoding path.
            for (Length padding : {0, 32}) {
               Length capacity = lengths[i] + padding;
               std::vector<Byte> restored(std::max<Length>(1, capacity));
               CHECK(fsst_decompress(d, outLengths[i], outStrings[i], capacity, restored.data()) == lengths[i]);
               CHECK(std::memcmp(restored.data(), inputs[i], lengths[i]) == 0);
            }
            Length capacity = lengths[i] / 2;
            if (capacity) {
               std::vector<Byte> restored(capacity);
               CHECK(fsst_decompress(d, outLengths[i], outStrings[i], capacity, restored.data()) == lengths[i]);
               CHECK(std::memcmp(restored.data(), inputs[i], capacity - zeroTerminated) == 0);
               if (zeroTerminated) CHECK(restored.back() == 0);
            }
         }
         if (zeroTerminated) {
            CHECK(outLengths[i] > 0);
            CHECK(outStrings[i][outLengths[i] - 1] == 0);
            CHECK(std::memchr(outStrings[i], 0, outLengths[i] - 1) == nullptr);
         }
         used += outLengths[i];
      }
      output.resize(used);
      return std::make_pair(outLengths, output);
   };

   auto expected = compress(encoder.get());
   EncoderPtr duplicate(fsst_duplicate(encoder.get()), fsst_destroy);
   encoder.reset();
   std::vector<Byte> duplicateHeader(FSST_MAXHEADER);
   duplicateHeader.resize(fsst_export(duplicate.get(), duplicateHeader.data()));
   CHECK(duplicateHeader == header);
   CHECK(compress(duplicate.get()) == expected);
}

void invalidHeaders() {
   const Byte* input = reinterpret_cast<const Byte*>("a");
   Length length = 1;
   EncoderPtr encoder(fsst_create(1, &length, &input, 0), fsst_destroy);
   std::vector<Byte> header(FSST_MAXHEADER);
   fsst_export(encoder.get(), header.data());
   auto decoder = std::unique_ptr<fsst_decoder_t>(new fsst_decoder_t());
   auto invalid = header;
   invalid[7] ^= 0x40;
   CHECK(fsst_import(decoder.get(), invalid.data()) == 0);

#ifdef FSST12
   uint16_t histogram[8] = {4097};
   invalid = header;
   std::memcpy(invalid.data() + 8, histogram, sizeof(histogram));
   CHECK(fsst_import(decoder.get(), invalid.data()) == 0);

   histogram[0] = 1;
   std::memcpy(invalid.data() + 8, histogram, sizeof(histogram));
   for (Byte symbolLength : {0, 9, 255}) {
      invalid[24] = symbolLength;
      CHECK(fsst_import(decoder.get(), invalid.data()) == 0);
   }
   invalid[24] = 2; // valid length, but inconsistent with the histogram
   CHECK(fsst_import(decoder.get(), invalid.data()) == 0);

   histogram[0] = 4096;
   std::memcpy(invalid.data() + 8, histogram, sizeof(histogram));
   for (size_t i = 0; i < 4096; ++i) {
      invalid[24 + 2 * i] = 1;
      invalid[25 + 2 * i] = static_cast<Byte>(i);
   }
   CHECK(fsst_import(decoder.get(), invalid.data()) == 24 + 2 * 4096);
#else
   invalid = header;
   std::memset(invalid.data() + 9, 0, 8);
   invalid[9] = 255;
   invalid[10] = 1; // 256 symbols cannot fit in the decoder
   CHECK(fsst_import(decoder.get(), invalid.data()) == 0);

   invalid[10] = 0; // maximum valid symbol count
   CHECK(fsst_import(decoder.get(), invalid.data()) == 17 + 255);
   invalid[8] = 2; // invalid zero-termination flag
   CHECK(fsst_import(decoder.get(), invalid.data()) == 0);
   invalid[8] = 1;
   invalid[9] = 0; // zero-terminated dictionary must include a one-byte terminator
   CHECK(fsst_import(decoder.get(), invalid.data()) == 0);
#endif
}

void runTests() {
   invalidHeaders();
   const std::vector<std::string> shortLines = {"", "a", "ab", "abc", "abcd", "abcdefgh",
                                                "abcdefghijklmnop", "hello world", "hello world"};
   roundTrip(shortLines);

   std::vector<std::string> text;
   for (unsigned i = 0; i < 2048; ++i)
      text.push_back("{\"name\":\"test string " + std::to_string(i % 97) +
                     "\",\"path\":\"https://example.com/api/" + std::to_string(i) + "\"}");
   roundTrip(text);

   std::vector<std::string> binary = {""};
   uint32_t random = 12345;
   for (unsigned i = 1; i <= 256; ++i) {
      std::string value(i, '\0');
      for (auto& byte : value) {
         random = random * 1664525U + 1013904223U;
         byte = static_cast<char>(random >> 24);
      }
      binary.push_back(value);
   }
   roundTrip(binary);

   std::string longLine;
   for (unsigned i = 0; i < 10000; ++i)
      longLine += "long repetitive line with symbols and digits 0123456789\n";
   roundTrip({longLine});
#ifndef FSST12
   roundTrip(shortLines, true);
   roundTrip(text, true);
   roundTrip({longLine}, true);
#endif
}
} // namespace

int main() {
   try {
      runTests();
      std::cout << "FSST regression tests passed\n";
      return 0;
   } catch (const std::exception& error) {
      std::cerr << error.what() << '\n';
      return 1;
   }
}
