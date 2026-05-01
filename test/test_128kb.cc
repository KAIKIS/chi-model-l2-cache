#include <cstdint>
#include <cstdio>
#include <cstring>

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
        return 0;
    } else {
        printf("FAIL\n");
        return 1;
    }
}
