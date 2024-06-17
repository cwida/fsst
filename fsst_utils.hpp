#pragma once

#include <stdint.h>
#include <fcntl.h>
#include <unistd.h>

#if defined(__linux__) || defined(__CYGWIN__)

#include <endian.h>

#elif defined(__APPLE__)

#include <libkern/OSByteOrder.h>

#define htole16(x) OSSwapHostToLittleInt16(x)
#define le16toh(x) OSSwapLittleToHostInt16(x)
#define htole32(x) OSSwapHostToLittleInt32(x)
#define le32toh(x) OSSwapLittleToHostInt32(x)
#define htole64(x) OSSwapHostToLittleInt64(x)
#define le64toh(x) OSSwapLittleToHostInt64(x)

#endif

class FDDefer {
    const int fd_;

public:
    FDDefer(int fd) : fd_(fd) {}
    ~FDDefer() {    ::close(fd_); }    
};

class MembufDefer {
    void* const buf_;

public:
    MembufDefer(void* buf) : buf_(buf) {}
    ~MembufDefer() { ::free(buf_); }
};

class FsstUtils {
public:
    template<typename T>
    static char* export_value(char* start, char* end, T val) {
        if (start + sizeof(val) < end) {
            val = htole(val);
            memcpy(start, &val, sizeof(val));
            return start + sizeof(val);
        } else {
            return nullptr;
        }
    }

    template<typename T>
    static const char* import_value(const char* start, const char* end, T& val) {
        if (start + sizeof(val) < end) {
            memcpy(&val, start, sizeof(val));
            val = letoh(val);
            return start + sizeof(val);
        } else {
            return nullptr;
        }
    }

private:
    template<typename T>
    static inline T htole(T val);

    template<typename T>
    static inline T letoh(T val);
};

template<>
inline uint16_t FsstUtils::htole<uint16_t>(uint16_t val) {
    return htole16(val);
}

template<>
inline uint16_t FsstUtils::letoh<uint16_t>(uint16_t val) {
    return le16toh(val);
}

template<>
inline uint32_t FsstUtils::htole<uint32_t>(uint32_t val) {
    return htole32(val);
}

template<>
inline uint32_t FsstUtils::letoh<uint32_t>(uint32_t val) {
    return le32toh(val);
}

template<>
inline uint64_t FsstUtils::htole<uint64_t>(uint64_t val) {
    return htole64(val);
}

template<>
inline uint64_t FsstUtils::letoh<uint64_t>(uint64_t val) {
    return le64toh(val);
}
