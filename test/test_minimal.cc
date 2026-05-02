// Minimal test: touch one cache line and exit via raw syscall
#include <cstdint>

static uint64_t data[8];  // 64 bytes = 1 cache line

// Raw exit syscall - bypasses libc entirely
static void raw_exit(int status) {
    asm volatile(
        "mov x0, %0\n"
        "mov x8, #93\n"  // SYS_exit_group
        "svc #0\n"
        :
        : "r"((uint64_t)status)
        : "x0", "x8"
    );
    __builtin_unreachable();
}

int main() {
    // Write one cache line
    for (int i = 0; i < 8; i++) {
        data[i] = i;
    }
    // Read it back
    uint64_t sum = 0;
    for (int i = 0; i < 8; i++) {
        sum += data[i];
    }
    // Exit via raw syscall
    raw_exit(sum == 28 ? 0 : 1);
}
