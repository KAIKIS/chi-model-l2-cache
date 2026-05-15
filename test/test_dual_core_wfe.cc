#include <cstdint>
#include <cstring>
#include <unistd.h>
#include <pthread.h>

// Test data: padding ensures L1 capacity pressure (32KB L1).
static volatile uint64_t padding[4096] __attribute__((aligned(64)));
static volatile uint64_t data_a[2048] __attribute__((aligned(64)));
static volatile uint64_t data_b[2048] __attribute__((aligned(64)));

static void raw_write(const char* msg) {
    size_t len = 0;
    while (msg[len]) len++;
    write(1, msg, len);
}

// Yield the CPU briefly — enough iterations to let the other core make
// progress in gem5's event-driven scheduling, but not so many that we
// trigger the 50M-tick deadlock detector.
static void yield_cpu() {
    for (volatile int i = 0; i < 100; i++) {
        __asm__ volatile("nop" ::: "memory");
    }
}

// Shared data: Core 0 writes, Core 1 reads
static volatile uint64_t shared_data[64] __attribute__((aligned(64)));
static volatile uint64_t sync_flag  __attribute__((aligned(64)));

// Ping-pong data
static volatile uint64_t pong_data[64] __attribute__((aligned(64)));
static volatile uint64_t pong_sync1 __attribute__((aligned(64)));
static volatile uint64_t pong_sync2 __attribute__((aligned(64)));

static volatile int core0_ok = 1;
static volatile int core1_ok = 1;

// ---- Tests ----
static bool test_write_readback(volatile uint64_t* data, int n, uint64_t pat) {
    for (int i = 0; i < n; i++) data[i * 8] = pat ^ (uint64_t)i;
    for (int i = 0; i < n; i++)
        if (data[i * 8] != (pat ^ (uint64_t)i)) { raw_write("FAIL: w/r\n"); return false; }
    return true;
}
static bool test_stride_pattern(volatile uint64_t* data, int n, int stride) {
    for (int i = 0; i < n; i += stride) data[i * 8] = (uint64_t)i * 0xBBBBBBBB;
    for (int i = 0; i < n; i += stride)
        if (data[i * 8] != (uint64_t)i * 0xBBBBBBBB) { raw_write("FAIL: stride\n"); return false; }
    return true;
}
static bool test_reverse_access(volatile uint64_t* data, int n) {
    for (int i = 0; i < n; i++) data[i * 8] = (uint64_t)i * 0xCCCCCCCC;
    for (int i = n - 1; i >= 0; i--)
        if (data[i * 8] != (uint64_t)i * 0xCCCCCCCC) { raw_write("FAIL: reverse\n"); return false; }
    return true;
}

// ---- Core 0 ----
static void* core0_func(void*) {
    raw_write("core0: started\n");
    constexpr int NL = 128;
    bool ok = true;

    ok = test_write_readback(data_a, NL, 0xDEAD0000) && ok;
    if (ok) raw_write("PASS: core0_write_readback\n");
    ok = test_stride_pattern(data_a, NL, 4) && ok;
    if (ok) raw_write("PASS: core0_stride\n");
    ok = test_reverse_access(data_a, NL) && ok;
    if (ok) raw_write("PASS: core0_reverse\n");

    // Test 4: shared write
    constexpr uint64_t MAGIC = 0x123456789ABCDEF0ULL;
    for (int i = 0; i < 64; i++) shared_data[i] = MAGIC + (uint64_t)i;
    __asm__ volatile("dmb sy" ::: "memory");
    sync_flag = 1;
    __asm__ volatile("dmb sy" ::: "memory");
    raw_write("PASS: core0_shared_write\n");

    // Test 5: ping-pong Phase 1
    constexpr uint64_t MA = 0xAAAAAAAAAAAAAAAAULL;
    constexpr uint64_t MB = 0xBBBBBBBBBBBBBBBBULL;
    for (int i = 0; i < 64; i++) pong_data[i] = MA + (uint64_t)i;
    __asm__ volatile("dmb sy" ::: "memory");
    pong_sync1 = 1;
    __asm__ volatile("dmb sy" ::: "memory");

    // Wait for Core 1's pong reply
    while (pong_sync2 == 0) yield_cpu();
    __asm__ volatile("dmb sy" ::: "memory");
    bool pong_ok = true;
    for (int i = 0; i < 64; i++)
        if (pong_data[i] != MB + (uint64_t)i) { raw_write("FAIL: pong_c0\n"); pong_ok = false; break; }
    if (pong_ok) raw_write("PASS: core0_pong_readback\n");
    ok = ok && pong_ok;

    core0_ok = ok ? 1 : 0;
    return nullptr;
}

// ---- Core 1 ----
static void* core1_func(void*) {
    raw_write("core1: started\n");
    constexpr int NL = 128;
    bool ok = true;

    ok = test_write_readback(data_b, NL, 0xBEEF0000) && ok;
    if (ok) raw_write("PASS: core1_write_readback\n");
    ok = test_stride_pattern(data_b, NL, 4) && ok;
    if (ok) raw_write("PASS: core1_stride\n");
    ok = test_reverse_access(data_b, NL) && ok;
    if (ok) raw_write("PASS: core1_reverse\n");

    // Test 4: wait for Core 0's shared data
    while (sync_flag == 0) yield_cpu();
    __asm__ volatile("dmb sy" ::: "memory");
    constexpr uint64_t MAGIC = 0x123456789ABCDEF0ULL;
    bool shared_ok = true;
    for (int i = 0; i < 64; i++)
        if (shared_data[i] != MAGIC + (uint64_t)i) { raw_write("FAIL: shared_read\n"); shared_ok = false; break; }
    if (shared_ok) raw_write("PASS: core1_shared_read\n");
    ok = ok && shared_ok;

    // Test 5: ping-pong
    while (pong_sync1 == 0) yield_cpu();
    __asm__ volatile("dmb sy" ::: "memory");
    constexpr uint64_t MA = 0xAAAAAAAAAAAAAAAAULL;
    constexpr uint64_t MB = 0xBBBBBBBBBBBBBBBBULL;
    bool pong_ok = true;
    for (int i = 0; i < 64; i++)
        if (pong_data[i] != MA + (uint64_t)i) { raw_write("FAIL: pong_c1_read\n"); pong_ok = false; break; }
    if (pong_ok) {
        for (int i = 0; i < 64; i++) pong_data[i] = MB + (uint64_t)i;
        __asm__ volatile("dmb sy" ::: "memory");
        pong_sync2 = 1;
        __asm__ volatile("dmb sy" ::: "memory");
        raw_write("PASS: core1_pong_write\n");
    }
    ok = ok && pong_ok;

    core1_ok = ok ? 1 : 0;
    return nullptr;
}

int main() {
    pthread_t t1;
    pthread_create(&t1, nullptr, core1_func, nullptr);
    core0_func(nullptr);
    pthread_join(t1, nullptr);
    if (core0_ok && core1_ok) raw_write("ALL TESTS PASSED\n");
    else raw_write("SOME TESTS FAILED\n");
    _exit((core0_ok && core1_ok) ? 0 : 1);
}
