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
#include <sys/stat.h>

static void print_usage() {
    fprintf(stderr, "Usage:\n"
            "    fsst_tools -m train -i <in_file> -d <out_dict_file>\n"
            "    fsst_tools -m encode -i <in_file> -o <out_encoded_file> [-d <in_dict_file>]\n"
            "    fsst_tools -m decode -i <in_file> -o <out_decoded_file> [-d <in_dict_file>]\n");
}

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
        if ((nwrite = write(fd, ptr, nleft)) < 0) {
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
        if ((nread = read(fd, ptr, nleft)) < 0) {
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

    int fd = ::open(dict_file, O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if (fd < 0) {
        perror(dict_file);
        free(dict_buf);
        return false;
    }

    if (writen(fd, dict_buf, dict_len) != dict_len) {
        perror("write");
        free(dict_buf);
        close(fd);
        return false;
    }

    close(fd);
    free(dict_buf);
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

    struct stat st;
    if (fstat(fd, &st) < 0) {
        perror("fstat");
        close(fd);
        return nullptr;
    }

    char* buf = (char *)malloc(st.st_size);
    if (buf == nullptr) {
        perror("malloc");
        close(fd);
        return nullptr;
    }

    ssize_t nread = readn(fd, buf, st.st_size);
    close(fd);
    if (nread != st.st_size) {
        perror(dict_file);
        free(buf);
        return nullptr;
    }

    fsst_encoder_t *encoder = fsst_encoder_import(buf, nread);
    free(buf);
    if (encoder == nullptr) {
        fprintf(stderr, "failed to import dictionary");
        return nullptr;
    }

    fprintf(stdout, "Dictionary load from %s\n", dict_file);
    return encoder;
}

/**
 * @title       fsst_train()
 * @description train the dictionary from the input file
 *
 * @param in_file    input file to train the dictionary
 * @param dict_file  output file to save the dictionary
 * @return true on success, false on error
 */
bool fsst_train(const char* dict_file, const char* in_file) {
    // Read the input file
    int fd = ::open(in_file, O_RDONLY);
    if (fd < 0) {
        perror(in_file);
        return false;
    }
    
    // read raw data
    struct stat st;
    if (fstat(fd, &st) < 0) {
        perror("fstat");
        close(fd);
        return false;
    }

    size_t raw_size = st.st_size; // std::min(st.st_size, (off_t)4096 * 32);
    unsigned char* raw_buf = (unsigned char *)malloc(raw_size);
    if (raw_buf == nullptr) {
        fprintf(stderr, "malloc failed\n");
        close(fd);
        return false;
    }

    if (readn(fd, raw_buf, raw_size) != raw_size) {
        perror("read file");
        free(raw_buf);
        close(fd);
        return false;
    }

    // train the dictionary
    unsigned long lenIn[] = {raw_size};
    const unsigned char *strIn[] = { (const unsigned char*)raw_buf};
    fsst_encoder_t *encoder = fsst_create(1, lenIn, strIn, 0);
    free(raw_buf);

    // Write the dictionary to the output file
    bool r = save_dict(dict_file, encoder);
    fsst_destroy(encoder);
    return r;
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
bool fsst_encode(const char* in_file, const char* out_file, const char* dict_file) {
    // load dictionary
    fsst_encoder_t *encoder = load_dict(dict_file);
    if ( encoder == nullptr ) {
        return false;
    }

    // open input and output files
    int ifd = ::open(in_file, O_RDONLY);
    if (ifd < 0) {
        perror(in_file);
        fsst_destroy(encoder);
        return false;
    }

    int ofd = ::open(out_file, O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if (ofd < 0) {
        perror(out_file);
        close(ifd);
        fsst_destroy(encoder);
        return false;
    }

    // read raw data and compress
    ssize_t nread = 0;
    unsigned char src_buf[4096];
    unsigned char dst_buf[sizeof(src_buf) * 2];

    bool success = true;
    while ((nread = read(ifd, src_buf, sizeof(src_buf))) > 0) {
        unsigned long src_len_array[] = { (unsigned long)nread };
        const unsigned char* src_buf_array[] = { (const unsigned char *)src_buf };

        unsigned long dst_len_array[] = { 0 };
        unsigned char* dst_buf_array[] = { nullptr };
        if (fsst_compress(encoder, 1, src_len_array, src_buf_array, sizeof(dst_buf), dst_buf, dst_len_array, dst_buf_array) < 1) {
            fprintf(stderr, "failed to compress data\n");
            success = false;
            break;
        }

        if (writen(ofd, dst_buf_array[0], dst_len_array[0]) != dst_len_array[0]) {
            perror("write");
            success = false;
            break;
        }
    }

    close(ifd);
    close(ofd);
    fsst_destroy(encoder);

    printf("Data written to %s, succ=%d\n", out_file, success);
    return success;
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

    int ofd = ::open(out_file, O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if (ofd < 0) {
        perror(out_file);
        close(ifd);
        return false;
    }

    // read raw data and compress
    ssize_t nread = 0;
    unsigned char src_buf[4096];
    unsigned char dst_buf[sizeof(src_buf) * 4];

    bool success = true;
    while ((nread = read(ifd, src_buf, sizeof(src_buf))) > 0) {
        size_t decoded_size = fsst_decompress(&decoder, nread, src_buf, sizeof(dst_buf), dst_buf);
        if (writen(ofd, dst_buf, decoded_size) != decoded_size) {
            perror("write");
            success = false;
            break;
        }
    }

    close(ifd);
    close(ofd);

    printf("Data written to %s, succ=%d\n", out_file, success);
    return success;

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
