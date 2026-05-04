#include <cstdint>
#include <cstring>
#include <unistd.h>
#include <pthread.h>

// Test data region: larger than L1 (32KB) to ensure L2 involvement.
static volatile uint64_t padding[4096] __attribute__((aligned(64)));  // 32KB padding
static volatile uint64_t data_a[2048] __attribute__((aligned(64)));   // 16KB for Core 0
static volatile uint64_t data_b[2048] __attribute__((aligned(64)));   // 16KB for Core 1

// Raw syscall write for output
static void raw_write(const char* msg) {
    size_t len = 0;
    while (msg[len]) len++;
    write(1, msg, len);
}

// Core 0 writes here, Core 1 reads
static volatile uint64_t shared_data[64] __attribute__((aligned(64)));
static volatile uint64_t sync_flag __attribute__((aligned(64)));

// Ping-pong data
static volatile uint64_t pong_data[64] __attribute__((aligned(64)));
static volatile uint64_t pong_sync1 __attribute__((aligned(64)));
static volatile uint64_t pong_sync2 __attribute__((aligned(64)));

// Result tracking
static volatile int core0_ok = 1;
static volatile int core1_ok = 1;

// ---- Tests ----

static bool test_write_readback(volatile uint64_t* data, int num_lines,
                                 uint64_t pattern) {
    for (int i = 0; i < num_lines; i++) {
        data[i * 8] = pattern ^ static_cast<uint64_t>(i);
    }
    for (int i = 0; i < num_lines; i++) {
        uint64_t expected = pattern ^ static_cast<uint64_t>(i);
        if (data[i * 8] != expected) {
            raw_write("FAIL: write_readback mismatch\n");
            return false;
        }
    }
    return true;
}

static bool test_stride_pattern(volatile uint64_t* data, int num_lines,
                                 int stride) {
    for (int i = 0; i < num_lines; i += stride) {
        data[i * 8] = static_cast<uint64_t>(i) * 0xBBBBBBBB;
    }
    for (int i = 0; i < num_lines; i += stride) {
        uint64_t expected = static_cast<uint64_t>(i) * 0xBBBBBBBB;
        if (data[i * 8] != expected) {
            raw_write("FAIL: stride pattern mismatch\n");
            return false;
        }
    }
    return true;
}

static bool test_reverse_access(volatile uint64_t* data, int num_lines) {
    for (int i = 0; i < num_lines; i++) {
        data[i * 8] = static_cast<uint64_t>(i) * 0xCCCCCCCC;
    }
    for (int i = num_lines - 1; i >= 0; i--) {
        uint64_t expected = static_cast<uint64_t>(i) * 0xCCCCCCCC;
        if (data[i * 8] != expected) {
            raw_write("FAIL: reverse access mismatch\n");
            return false;
        }
    }
    return true;
}

static void busy_wait(int iters) {
    for (volatile int i = 0; i < iters; i++) {
        __asm__ volatile("isb" ::: "memory");
    }
}

// ---- Core 0 thread ----
static void* core0_func(void*) {
    raw_write("core0: started\n");

    constexpr int NUM_LINES = 128;
    bool ok = true;

    // Tests 1-3: independent per-core tests
    ok = test_write_readback(data_a, NUM_LINES, 0xDEAD0000) && ok;
    if (ok) raw_write("PASS: core0_write_readback\n");

    ok = test_stride_pattern(data_a, NUM_LINES, 4) && ok;
    if (ok) raw_write("PASS: core0_stride\n");

    ok = test_reverse_access(data_a, NUM_LINES) && ok;
    if (ok) raw_write("PASS: core0_reverse\n");

    // Test 4: Core 0 writes shared data
    constexpr uint64_t MAGIC = 0x123456789ABCDEF0ULL;
    for (int i = 0; i < 64; i++) {
        shared_data[i] = MAGIC + static_cast<uint64_t>(i);
    }
    __asm__ volatile("dmb sy" ::: "memory");
    sync_flag = 1;
    __asm__ volatile("dmb sy" ::: "memory");
    raw_write("PASS: core0_shared_write\n");

    // Test 5: Ping-pong - Phase 1: Core 0 writes
    constexpr uint64_t MAGIC_A = 0xAAAAAAAAAAAAAAAAULL;
    constexpr uint64_t MAGIC_B = 0xBBBBBBBBBBBBBBBBULL;
    for (int i = 0; i < 64; i++) {
        pong_data[i] = MAGIC_A + static_cast<uint64_t>(i);
    }
    __asm__ volatile("dmb sy" ::: "memory");
    pong_sync1 = 1;
    __asm__ volatile("dmb sy" ::: "memory");

    // Wait for Core 1 to finish with timeout
    bool got_pong = false;
    for (int tries = 0; tries < 10000000; tries++) {
        if (pong_sync2 != 0) { got_pong = true; break; }
        busy_wait(10);
    }
    if (!got_pong) {
        raw_write("FAIL: core0 timeout waiting for pong_sync2\n");
        ok = false;
    } else {
        __asm__ volatile("dmb sy" ::: "memory");
        // Phase 2: verify Core 1's data
        bool pong_ok = true;
        for (int i = 0; i < 64; i++) {
            if (pong_data[i] != MAGIC_B + static_cast<uint64_t>(i)) {
                raw_write("FAIL: pong_core0_readback mismatch\n");
                pong_ok = false;
                break;
            }
        }
        if (pong_ok) raw_write("PASS: core0_pong_readback\n");
        ok = ok && pong_ok;
    }

    core0_ok = ok ? 1 : 0;
    __asm__ volatile("dmb sy" ::: "memory");
    return nullptr;
}

// ---- Core 1 thread ----
static void* core1_func(void*) {
    raw_write("core1: started\n");

    constexpr int NUM_LINES = 128;
    bool ok = true;

    // Tests 1-3: independent per-core tests
    ok = test_write_readback(data_b, NUM_LINES, 0xBEEF0000) && ok;
    if (ok) raw_write("PASS: core1_write_readback\n");

    ok = test_stride_pattern(data_b, NUM_LINES, 4) && ok;
    if (ok) raw_write("PASS: core1_stride\n");

    ok = test_reverse_access(data_b, NUM_LINES) && ok;
    if (ok) raw_write("PASS: core1_reverse\n");

    // Test 4: Core 1 waits for shared data
    raw_write("core1: waiting for sync_flag\n");
    bool got_flag = false;
    for (int tries = 0; tries < 10000000; tries++) {
        if (sync_flag != 0) { got_flag = true; break; }
        busy_wait(10);
    }
    if (!got_flag) {
        raw_write("FAIL: core1 timeout waiting for sync_flag\n");
        ok = false;
    } else {
        raw_write("core1: flag received\n");
        __asm__ volatile("dmb sy" ::: "memory");
        constexpr uint64_t MAGIC = 0x123456789ABCDEF0ULL;
        bool shared_ok = true;
        for (int i = 0; i < 64; i++) {
            if (shared_data[i] != MAGIC + static_cast<uint64_t>(i)) {
                raw_write("FAIL: shared_read mismatch\n");
                shared_ok = false;
                break;
            }
        }
        if (shared_ok) raw_write("PASS: core1_shared_read\n");
        ok = ok && shared_ok;
    }

    // Test 5: Ping-pong - wait for Core 0's write
    bool got_pong1 = false;
    for (int tries = 0; tries < 10000000; tries++) {
        if (pong_sync1 != 0) { got_pong1 = true; break; }
        busy_wait(10);
    }
    if (!got_pong1) {
        raw_write("FAIL: core1 timeout waiting for pong_sync1\n");
        ok = false;
    } else {
        __asm__ volatile("dmb sy" ::: "memory");
        constexpr uint64_t MAGIC_A = 0xAAAAAAAAAAAAAAAAULL;
        constexpr uint64_t MAGIC_B = 0xBBBBBBBBBBBBBBBBULL;

        // Phase 1: verify Core 0's data
        bool pong_ok = true;
        for (int i = 0; i < 64; i++) {
            if (pong_data[i] != MAGIC_A + static_cast<uint64_t>(i)) {
                raw_write("FAIL: pong_core1_read mismatch\n");
                pong_ok = false;
                break;
            }
        }

        // Phase 2: Core 1 writes new pattern
        if (pong_ok) {
            for (int i = 0; i < 64; i++) {
                pong_data[i] = MAGIC_B + static_cast<uint64_t>(i);
            }
            __asm__ volatile("dmb sy" ::: "memory");
            pong_sync2 = 1;
            __asm__ volatile("dmb sy" ::: "memory");
            raw_write("PASS: core1_pong_write\n");
        }
        ok = ok && pong_ok;
    }

    core1_ok = ok ? 1 : 0;
    __asm__ volatile("dmb sy" ::: "memory");
    return nullptr;
}

int main() {
    pthread_t t1;
    pthread_create(&t1, nullptr, core1_func, nullptr);

    // Core 0 runs on the main thread
    core0_func(nullptr);

    // Wait for Core 1 to finish
    pthread_join(t1, nullptr);

    bool ok = (core0_ok == 1) && (core1_ok == 1);
    if (ok) {
        raw_write("ALL TESTS PASSED\n");
    } else {
        raw_write("SOME TESTS FAILED\n");
    }

    _exit(ok ? 0 : 1);
}
