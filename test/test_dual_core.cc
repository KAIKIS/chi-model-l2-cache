#include <cstdint>
#include <unistd.h>

// Test data region: larger than L1 (32KB) to ensure L2 involvement.
// Each core gets its own 16KB region (256 cache lines).
// Core 0: data_a[0..2047], Core 1: data_b[0..2047]
// These are placed AFTER a large padding to ensure they're NOT in L1
// when the test starts (BSS zeroing evicts them).
static volatile uint64_t padding[4096] __attribute__((aligned(64)));  // 32KB padding
static volatile uint64_t data_a[2048] __attribute__((aligned(64)));   // 16KB for Core 0
static volatile uint64_t data_b[2048] __attribute__((aligned(64)));   // 16KB for Core 1


// Raw syscall write for output
static void raw_write(const char* msg) {
    size_t len = 0;
    while (msg[len]) len++;
    write(1, msg, len);
}

// Core ID counter
static volatile uint64_t core_id_counter __attribute__((aligned(64)));

// Get this core's ID (0 or 1) using non-atomic read-modify-write.
// In gem5's deterministic simulation, one core always executes first.
static int get_core_id() {
    uint64_t old = core_id_counter;
    __asm__ volatile("dmb sy" ::: "memory");
    core_id_counter = old + 1;
    __asm__ volatile("dmb sy" ::: "memory");
    return static_cast<int>(old);
}

// Test 1: Sequential write and readback
// Writes a pattern to NUM_LINES cache lines, then reads back and verifies.
// This tests L2 cache fill (write miss → L2 → memory → fill) and
// L2 cache lookup (read hit/miss).
static bool test_write_readback(volatile uint64_t* data, int num_lines,
                                 uint64_t pattern) {
    // Write phase: each write is likely an L1 miss → L2 request
    for (int i = 0; i < num_lines; i++) {
        data[i * 8] = pattern ^ static_cast<uint64_t>(i);
    }

    // Read phase: verify data
    for (int i = 0; i < num_lines; i++) {
        uint64_t expected = pattern ^ static_cast<uint64_t>(i);
        uint64_t actual = data[i * 8];
        if (actual != expected) {
            raw_write("FAIL: write_readback mismatch\n");
            return false;
        }
    }
    return true;
}

// Test 2: Stride pattern — access every Nth cache line
// This tests L2 cache set conflict behavior.
static bool test_stride_pattern(volatile uint64_t* data, int num_lines,
                                 int stride) {
    // Write with stride
    for (int i = 0; i < num_lines; i += stride) {
        data[i * 8] = static_cast<uint64_t>(i) * 0xBBBBBBBB;
    }

    // Verify with stride
    for (int i = 0; i < num_lines; i += stride) {
        uint64_t expected = static_cast<uint64_t>(i) * 0xBBBBBBBB;
        if (data[i * 8] != expected) {
            raw_write("FAIL: stride pattern mismatch\n");
            return false;
        }
    }
    return true;
}

// Test 3: Reverse order access — read cache lines in reverse
// This tests L2 cache under different access patterns.
static bool test_reverse_access(volatile uint64_t* data, int num_lines) {
    // Write forward
    for (int i = 0; i < num_lines; i++) {
        data[i * 8] = static_cast<uint64_t>(i) * 0xCCCCCCCC;
    }

    // Read backward
    for (int i = num_lines - 1; i >= 0; i--) {
        uint64_t expected = static_cast<uint64_t>(i) * 0xCCCCCCCC;
        if (data[i * 8] != expected) {
            raw_write("FAIL: reverse access mismatch\n");
            return false;
        }
    }
    return true;
}

int main() {
    int my_id = get_core_id();

    // Each core works on its own data region
    volatile uint64_t* my_data = (my_id == 0) ? data_a : data_b;
    constexpr int NUM_LINES = 128;  // 128 cache lines = 8KB per core

    bool ok = true;

    // Test 1: Write and readback with unique pattern per core
    uint64_t pattern = (my_id == 0) ? 0xDEAD0000 : 0xBEEF0000;
    ok = test_write_readback(my_data, NUM_LINES, pattern) && ok;
    if (ok) {
        if (my_id == 0) raw_write("PASS: core0_write_readback\n");
        else            raw_write("PASS: core1_write_readback\n");
    }

    // Test 2: Stride pattern
    ok = test_stride_pattern(my_data, NUM_LINES, 4) && ok;
    if (ok) {
        if (my_id == 0) raw_write("PASS: core0_stride\n");
        else            raw_write("PASS: core1_stride\n");
    }

    // Test 3: Reverse access
    ok = test_reverse_access(my_data, NUM_LINES) && ok;
    if (ok) {
        if (my_id == 0) raw_write("PASS: core0_reverse\n");
        else            raw_write("PASS: core1_reverse\n");
    }

    // Report final result
    if (ok) {
        raw_write("ALL TESTS PASSED\n");
    } else {
        raw_write("SOME TESTS FAILED\n");
    }

    _exit(ok ? 0 : 1);
}
