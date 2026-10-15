/* Compile as C11 and link with libfsst and the C++ runtime. */
#include "fsst.h"

int main(void) {
   const unsigned char *input[] = {(const unsigned char *) "hello C API"};
   size_t length[] = {11}, compressed_length[1];
   unsigned char compressed[64], decoded[64], *output[1];
   fsst_encoder_t *encoder = fsst_create(1, length, input, 0);
   fsst_decoder_t decoder = fsst_decoder(encoder);
   size_t count = fsst_compress(encoder, 1, length, input, sizeof(compressed),
                              compressed, compressed_length, output);
   size_t size = fsst_decompress(&decoder, compressed_length[0], output[0],
                                 sizeof(decoded), decoded);
   fsst_destroy(encoder);
   return !(count == 1 && size == length[0] && !memcmp(input[0], decoded, size));
}
