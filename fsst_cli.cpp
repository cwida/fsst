#ifdef FSST12
#include "fsst12.h" // the official FSST API -- also usable by C mortals
#else
#include "fsst.h" // the official FSST API -- also usable by C mortals
#endif

#include <unistd.h>
#include <stdlib.h>
#include <stdio.h>
#include <fcntl.h>
#include <errno.h>
#include <vector>
#include <algorithm>
#include <sys/stat.h>
#include "fsst_utils.hpp"

static void print_usage() {
    fprintf(stderr, "Usage:\n"
            "    fsst_cli -m train -i <in_file> -d <out_dict_file>\n"
            "    fsst_cli -m encode -i <in_file> -o <out_encoded_file> [-d <in_dict_file>]\n"
            "    fsst_cli -m decode -i <in_file> -o <out_decoded_file> [-d <in_dict_file>]\n");
}

class FsstEncoderDefer {
    fsst_encoder_t* const encoder_;
public:
    FsstEncoderDefer(fsst_encoder_t* encoder) : encoder_(encoder) {}
    ~FsstEncoderDefer() {
        fsst_destroy(encoder_);
    }
};

/**
 * @title       writen()
 * @description write n bytes to a file descriptor
 *
 * @param fd    file descriptor 
 * @param buf   buffer to write
 * @param n     number of bytes to write
 * @return number of bytes written, or -1 on error
 */
static ssize_t writen(int fd, void *buf, size_t n) {
    ssize_t nleft = n;
    ssize_t nwrite;
    char *ptr = (char *)buf;

    while (nleft > 0) {
        if ((nwrite = write(fd, ptr, std::min(nleft, (ssize_t)0x7ffff000))) < 0) {
            if (errno == EINTR) {
                nwrite = 0;
            } else {
                return -1;
            }
        }
        nleft -= nwrite;
        ptr += nwrite;
    }
    return n - nleft;
}

/**
 * @title       readn()
 * @description read n bytes from a file descriptor
 *
 * @param fd    file descriptor 
 * @param buf   buffer to read
 * @param n     number of bytes to read
 * @return number of bytes read, or -1 on error
 */
static ssize_t readn(int fd, void *buf, size_t n) {
    ssize_t nleft = n;
    ssize_t nread;
    char *ptr = (char *)buf;

    while (nleft > 0) {
        if ((nread = ::read(fd, ptr, std::min(nleft, (ssize_t)0x7ffff000))) < 0) {
            if (errno == EINTR) {
                nread = 0;
            } else {
                return -1;
            }
        } else if (nread == 0) {
            break;
        }
        nleft -= nread;
        ptr += nread;
    }

    return n - nleft;
}

/**
 * @title       save_dict()
 * @description export the dictionary to a file
 *
 * @param dict_file    filename to save the dictionary 
 * @param encoder      dictionary to save
 * @return true on success, false on error
 */
static bool save_dict(const char* dict_file, fsst_encoder_t* encoder) {
    size_t dict_len = 0;
    char* dict_buf = fsst_encoder_export(encoder, &dict_len);
    if (dict_buf == nullptr) {
        fprintf(stderr, "failed to export dictionary");
        return false;
    }
    MembufDefer membuf_defer(dict_buf);

    int fd = ::open(dict_file, O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if (fd < 0) {
        perror(dict_file);
        return false;
    }
    FDDefer fd_defer(fd);

    if (writen(fd, dict_buf, dict_len) != dict_len) {
        perror("write");
        return false;
    }

    fprintf(stdout, "Dictionary written to %s\n", dict_file);
    return true;
}

/**
 * @title       load_dict()
 * @description load the dictionary from a file
 *
 * @param dict_file   filename to load the dictionary 
 * @return dictionary on success, nullptr on error
 */
static fsst_encoder_t* load_dict(const char* dict_file) {
    int fd = ::open(dict_file, O_RDONLY);
    if (fd < 0) {
        perror(dict_file);
        return nullptr;
    }
    FDDefer fd_defer(fd);

    struct stat st;
    if (fstat(fd, &st) < 0) {
        perror("fstat");
        return nullptr;
    }

    char* buf = (char *)malloc(st.st_size);
    if (buf == nullptr) {
        perror("malloc");
        return nullptr;
    }
    MembufDefer membuf_defer(buf);

    ssize_t nread = readn(fd, buf, st.st_size);
    if (nread != st.st_size) {
        perror(dict_file);
        return nullptr;
    }

    fsst_encoder_t *encoder = fsst_encoder_import(buf, nread);
    if (encoder == nullptr) {
        fprintf(stderr, "failed to import dictionary\n");
        return nullptr;
    }

    fprintf(stdout, "Dictionary load from %s\n", dict_file);
    return encoder;
}

/**
 * @title       build_dict()
 * @description train the dictionary from the input file
 *
 * @param fd             input file to train the dictionary
 * @param train_max_len  maximum length of the training data
 * @return fsst_encoder_t* on success, nullptr on error
 */
fsst_encoder_t *build_dict(int fd, size_t train_max_len) {
    // read raw data
    struct stat st;
    if (fstat(fd, &st) < 0) {
        perror("fstat");
        return nullptr;
    }

    size_t raw_size = st.st_size;
    if (train_max_len > 0 && raw_size > train_max_len) {
        raw_size = train_max_len;
    }
    unsigned char* raw_buf = (unsigned char *)malloc(raw_size);
    if (raw_buf == nullptr) {
        fprintf(stderr, "malloc failed\n");
        return nullptr;
    }
    MembufDefer membuf_defer(raw_buf);

    if (readn(fd, raw_buf, raw_size) != raw_size) {
        perror("read file");
        return nullptr;
    }

    // train the dictionary
    unsigned long lenIn[] = {raw_size};
    const unsigned char *strIn[] = { (const unsigned char*)raw_buf};
    return fsst_create(1, lenIn, strIn, 0);
}

/**
 * @title       fsst_train()
 * @description train the dictionary from the input file
 *
 * @param in_file    input file to train the dictionary
 * @param dict_file  output file to save the dictionary
 * @return true on success, false on error
 */
static bool fsst_train(const char* dict_file, const char* in_file) {
    // Read the input file
    int fd = ::open(in_file, O_RDONLY);
    if (fd < 0) {
        perror(in_file);
        return false;
    }
    FDDefer fd_defer(fd);

    fsst_encoder_t* encoder = build_dict(fd, 0);
    if (encoder == nullptr) {
        return false;
    }
    FsstEncoderDefer encoder_defer(encoder);
    
    // Write the dictionary to the output file
    return save_dict(dict_file, encoder);
}

/**
 * @title       fsst_encode()
 * @description encode the input file with the dictionary
 *
 * @param in_file     input file to encode
 * @param out_file    output file to save the encoded data
 * @param dict_file   input dictionary file, optional
 * @return true on success, false on error
 */
static bool fsst_encode(const char* in_file, const char* out_file, const char* dict_file) {
    // load dictionary
    fsst_encoder_t *encoder = load_dict(dict_file);
    if ( encoder == nullptr ) {
        return false;
    }
    FsstEncoderDefer encoder_defer(encoder);

    // open input and output files
    int ifd = ::open(in_file, O_RDONLY);
    if (ifd < 0) {
        perror(in_file);
        return false;
    }
    FDDefer ifd_defer(ifd);

    int ofd = ::open(out_file, O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if (ofd < 0) {
        perror(out_file);
        return false;
    }
    FDDefer ofd_defer(ofd);

    // read raw data
    struct stat st;
    if (fstat(ifd, &st) < 0) {
        perror("fstat");
        return false;
    }

    size_t src_buf_size = st.st_size;
    unsigned char *src_buf = (unsigned char *) malloc(src_buf_size);
    MembufDefer src_membuf_defer(src_buf);
    if (readn(ifd, src_buf, src_buf_size) != src_buf_size) {
        perror("read file");
        return false;
    }
    unsigned long src_len_array[] = { (unsigned long)src_buf_size };
    const unsigned char* src_buf_array[] = { (const unsigned char *)src_buf };

    // compress the data
    size_t dst_buf_size = 8 + src_buf_size * 2;
    unsigned char *dst_buf = (unsigned char *) malloc(dst_buf_size);
    MembufDefer dst_membuf_defer(dst_buf);

    unsigned long dst_len_array[] = { 0 };
    unsigned char* dst_buf_array[] = { nullptr };
    if(fsst_compress(encoder, 1, src_len_array, src_buf_array, dst_buf_size, dst_buf, dst_len_array, dst_buf_array) != 1) {
        fprintf(stderr, "failed to compress data\n");
        return false;
    }
    printf("Compressed %ld bytes to %ld bytes, ratio=%.2f.\n", 
        src_len_array[0], dst_len_array[0], (double)dst_len_array[0] / src_len_array[0]);

    // write the compressed data
    if (writen(ofd, dst_buf_array[0], dst_len_array[0]) != dst_len_array[0]) {
        perror("write");
        return false;
    }

    printf("Data written to %s\n", out_file);
    return true;
}

int fsst_decode(const char* in_file, const char* out_file, const char* dict_file) {
    // load dictionary
    fsst_encoder_t *encoder = load_dict(dict_file);
    if ( encoder == nullptr ) {
        return false;
    }
    fsst_decoder_t decoder = fsst_decoder(encoder);
    fsst_destroy(encoder);

    // open input and output files
    int ifd = ::open(in_file, O_RDONLY);
    if (ifd < 0) {
        perror(in_file);
        return false;
    }
    FDDefer fd_defer(ifd);

    int ofd = ::open(out_file, O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if (ofd < 0) {
        perror(out_file);
        return false;
    }
    FDDefer ofd_defer(ofd);

    // read raw data
    struct stat st;
    if (fstat(ifd, &st) < 0) {
        perror("fstat");
        return false;
    }

    size_t src_buf_size = st.st_size;
    unsigned char *src_buf = (unsigned char *) malloc(src_buf_size);
    MembufDefer src_membuf_defer(src_buf);
    if (readn(ifd, src_buf, src_buf_size) != src_buf_size) {
        perror("read file");
        return false;
    }

    // decompress the data
    size_t dst_buf_size = src_buf_size * 4; // one code to 4 characters at most
    unsigned char *dst_buf = (unsigned char *) malloc(dst_buf_size);
    MembufDefer dst_membuf_defer(dst_buf);

    size_t decoded_size = fsst_decompress(&decoder, src_buf_size, src_buf, dst_buf_size, dst_buf);
    if (writen(ofd, dst_buf, decoded_size) != decoded_size) {
        perror("write");
        return false;
    }

    printf("Data written to %s\n", out_file);
    return true;
}

int main(int argc, char** argv) {
    const char* mode = nullptr;
    const char* dict_file = nullptr;
    const char* in_file = nullptr;
    const char* out_file = nullptr;

    int opt;
    while ((opt = getopt(argc, argv, "m:i:o:d:")) != -1) {
        switch (opt) {
        case 'm':
            mode = optarg;
            break;
        case 'd':
            dict_file = optarg;
            break;
        case 'i':
            in_file = optarg;
            break;
        case 'o':
            out_file = optarg;
            break;
        default: /* '?' */
            print_usage();
            exit(EXIT_FAILURE);
        }
    }

    if (mode == nullptr) {
        print_usage();
        exit(EXIT_FAILURE);
    }

    if (strcmp(mode, "train") == 0) {
        // Train the dictionary
        if (in_file == nullptr || dict_file == nullptr) {
            print_usage();
            exit(EXIT_FAILURE);
        }
        fsst_train(dict_file, in_file);
    } else if (strcmp(mode, "encode") == 0) {
        // Encode the input file
        if (in_file == nullptr || out_file == nullptr || dict_file == nullptr) {
            print_usage();
            exit(EXIT_FAILURE);
        }
        fsst_encode(in_file, out_file, dict_file);
    } else if (strcmp(mode, "decode") == 0) {
        // Decode the input file
        if (in_file == nullptr || out_file == nullptr || dict_file == nullptr) {
            print_usage();
            exit(EXIT_FAILURE);
        }
        fsst_decode(in_file, out_file, dict_file);
    } else {
        print_usage();
        exit(EXIT_FAILURE);
    }

    return 0;
}
