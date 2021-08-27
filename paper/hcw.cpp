#include "libfsst.hpp"
#include "PerfEvent.hpp"

extern "C" ssize_t read(int fildes, void *buf, size_t nbyte);

int main(int argc,char* argv[]) {
   bool zeroTerminated = false, noSuffixOpt = false, avoidBranch = false, opt = false;
   unsigned long compressed=0, uncompressed=0, lineSize = 511, sampleChunk=1<<23;
   int simd = 3;

   // read the file at once
   if (argc < 2) return -1;
   if (argc >= 3) lineSize = atoi(argv[2]);
   int fd = open(argv[1], O_RDONLY);
   struct stat stat_buf;
   (void) fstat(fd, &stat_buf);
   unsigned long inSize = stat_buf.st_size;
   vector<u8> cur(inSize + lineSize);
   if (read(fd, cur.data(), inSize) < 0) exit(-1);

   // figure out the other parameters
   if (argc >= 4) {
      char *s = strstr(argv[3], "-simd"); 
      simd = s?(s[5] >= '0' && s[5] <= '4')?(s[5]-'0'):3:0; // simd unroll factor - default 3
      bool adaptive = (strstr(argv[3], "-adaptive") != NULL);
      if (adaptive) simd = 0; 

      zeroTerminated = strstr(argv[3], "-zero") != NULL; 
      noSuffixOpt = strstr(argv[3], "-nosuffix") != NULL; 
      avoidBranch = strstr(argv[3], "-avoidbranch") != NULL; 
      opt = noSuffixOpt || avoidBranch || (strstr(argv[3], "-branch") != NULL); 
   }
   if (argc >= 5) sampleChunk = atoi(argv[4]);

   vector<u8*> strIn, strOut;
   vector<unsigned long> lenIn, lenOut;
   vector<u8> out(8192+sampleChunk*2);

   for(unsigned long chunkPos=0; chunkPos<inSize; chunkPos+=sampleChunk) {
      unsigned long n, lineEnd, linePos = chunkPos, chunkEnd = min(inSize, chunkPos+sampleChunk);

      for(n=0; linePos < chunkEnd; n++) {
         lineEnd = linePos + lineSize;
         strIn.push_back(cur.data() + linePos);
         unsigned long len;
         if (zeroTerminated) {
            for(len=0; linePos+len < lineEnd; len++)
               if(!cur[linePos+len]) break;
            if (linePos+len == lineEnd) cur[linePos+len-1] = 0;
            else len++; // count zero byte
         } else {
            len = lineEnd - linePos;
         }
         lenIn.push_back(len);
         strOut.push_back(NULL);
         lenOut.push_back(0);
         uncompressed += len;
         linePos = lineEnd;
      }
      Encoder *e;
      {
         PerfEventBlock a(8*1024*1024);
         e = (Encoder*) fsst_create(n, lenIn.data(), strIn.data(), zeroTerminated);
      }
      {
         PerfEventBlock a(chunkEnd - chunkPos);
         unsigned long m = opt?compressImpl(e, n, lenIn.data(), strIn.data(), out.size(), out.data(), lenOut.data(), strOut.data(), noSuffixOpt, avoidBranch,0):
                   (simd >= 0)?compressAuto(e, n, lenIn.data(), strIn.data(), out.size(), out.data(), lenOut.data(), strOut.data(), simd):
                   fsst_compress((fsst_encoder_t*) e, n, lenIn.data(), strIn.data(), out.size(), out.data(), lenOut.data(), strOut.data());
         assert(m == n);
      }
      fsst_decoder_t d = fsst_decoder((fsst_encoder_t*)e);
      vector<u8> decompressed(lineSize);
      for(unsigned long i=0; i<n; i++) {
         compressed += lenOut[i];
         unsigned long m = fsst_decompress(&d, lenOut[i], strOut[i], lineSize, decompressed.data());
         string s1 = string((char*) decompressed.data(), min(m,lineSize));
         string s2 = string((char*) strIn[i], lenIn[i]);
         assert(m == lenIn[i]);
         assert(s1 == s2); 
      }
      fsst_destroy((fsst_encoder_t*)e);
      strIn.clear(); strOut.clear();
      lenIn.clear(); lenOut.clear();
   }
cerr << ((double) uncompressed) / compressed << endl;
   return 0;
}
