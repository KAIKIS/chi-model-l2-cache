#include <cstdint>
#include <cstdio>
#include <unistd.h>

static volatile uint64_t val = 0;

int main() {
    setvbuf(stdout, NULL, _IONBF, 0);
    val = 42;
    uint64_t readback = val;
    if (readback == 42) printf("PASS\n");
    else printf("FAIL: expected 42, got %lu\n", readback);
    _exit(readback == 42 ? 0 : 1);
}
