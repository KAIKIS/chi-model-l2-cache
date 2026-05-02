#include <cstdint>
#include <cstdio>
#include <cstring>
#include <unistd.h>

constexpr size_t SIZE = 128 * 1024;  // 128KB
constexpr size_t ELEMENTS = SIZE / sizeof(uint64_t);

static uint64_t memory[SIZE / sizeof(uint64_t)];

uint64_t computeChecksum(const uint64_t* data, size_t count) {
    uint64_t sum = 0;
    for (size_t i = 0; i < count; i++) {
        sum += data[i];
    }
    return sum;
}

int main() {
    // Disable stdio buffering so output is visible immediately
    setvbuf(stdout, NULL, _IONBF, 0);

    for (size_t i = 0; i < ELEMENTS; i++) {
        memory[i] = static_cast<uint64_t>(i);
    }

    uint64_t checksum = computeChecksum(memory, ELEMENTS);
    uint64_t expected = (ELEMENTS * (ELEMENTS - 1)) / 2;

    printf("Elements: %lu\n", ELEMENTS);
    printf("Checksum: %lu\n", checksum);
    printf("Expected: %lu\n", expected);

    if (checksum == expected) {
        printf("PASS\n");
    } else {
        printf("FAIL\n");
    }

    // Use _exit to skip libc cleanup (atexit, stdio flush) which
    // hangs gem5's Ruby drain after exit() syscall.
    _exit(checksum == expected ? 0 : 1);
}
