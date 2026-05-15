// Dual-core true-sharing test: private tests + shared write→read.
// Uses futex for sync, small arrays to avoid L2 capacity issues.

#include <cstdint>
#include <unistd.h>
#include <pthread.h>
#include <linux/futex.h>
#include <sys/syscall.h>

static void raw_write(const char* msg) {
    size_t len = 0;
    while (msg[len]) len++;
    write(1, msg, len);
}
static void futex_wait(volatile uint64_t* a, uint64_t v) { syscall(SYS_futex, (uint64_t*)a, FUTEX_WAIT, v, 0, 0, 0); }
static void futex_wake(volatile uint64_t* a)            { syscall(SYS_futex, (uint64_t*)a, FUTEX_WAKE, 1, 0, 0, 0); }

// Per-core private data: 8 cache lines each (512B)
static volatile uint64_t priv_a[64] __attribute__((aligned(64)));
static volatile uint64_t priv_b[64] __attribute__((aligned(64)));

// Shared data: single cache line (64B)
static volatile uint64_t shared[8] __attribute__((aligned(64)));
static volatile uint64_t flag   __attribute__((aligned(64)));

static volatile int ok0 = 1, ok1 = 1;

static bool test_wr(volatile uint64_t* d, int n, uint64_t pat) {
    for (int i = 0; i < n; i++) d[i] = pat ^ (uint64_t)i;
    for (int i = 0; i < n; i++) if (d[i] != (pat ^ (uint64_t)i)) return false;
    return true;
}
static bool test_stride(volatile uint64_t* d, int n, int s) {
    for (int i = 0; i < n; i += s) d[i] = (uint64_t)i * 0xBB;
    for (int i = 0; i < n; i += s) if (d[i] != (uint64_t)i * 0xBB) return false;
    return true;
}
static bool test_reverse(volatile uint64_t* d, int n) {
    for (int i = 0; i < n; i++) d[i] = (uint64_t)i * 0xCC;
    for (int i = n - 1; i >= 0; i--) if (d[i] != (uint64_t)i * 0xCC) return false;
    return true;
}

static void* c0(void*) {
    raw_write("core0: started\n");
    bool ok = true;
    ok = test_wr(priv_a, 32, 0xDEAD) && ok;
    if (ok) raw_write("PASS: c0_write_readback\n");
    ok = test_stride(priv_a, 32, 4) && ok;
    if (ok) raw_write("PASS: c0_stride\n");
    ok = test_reverse(priv_a, 32) && ok;
    if (ok) raw_write("PASS: c0_reverse\n");

    // True sharing: write shared data, signal Core 1
    constexpr uint64_t M = 0xFEEDFACECAFEBEEFULL;
    for (int i = 0; i < 8; i++) shared[i] = M + (uint64_t)i;
    __asm__ volatile("dmb sy" ::: "memory");
    flag = 1;
    __asm__ volatile("dmb sy" ::: "memory");
    futex_wake(&flag);
    raw_write("PASS: c0_shared_write\n");

    ok0 = ok ? 1 : 0;
    return nullptr;
}

static void* c1(void*) {
    raw_write("core1: started\n");
    bool ok = true;
    ok = test_wr(priv_b, 32, 0xBEEF) && ok;
    if (ok) raw_write("PASS: c1_write_readback\n");
    ok = test_stride(priv_b, 32, 4) && ok;
    if (ok) raw_write("PASS: c1_stride\n");
    ok = test_reverse(priv_b, 32) && ok;
    if (ok) raw_write("PASS: c1_reverse\n");

    // True sharing: wait for Core 0, then verify shared data
    while (flag == 0) futex_wait(&flag, 0);
    __asm__ volatile("dmb sy" ::: "memory");
    constexpr uint64_t M = 0xFEEDFACECAFEBEEFULL;
    bool shared_ok = true;
    for (int i = 0; i < 8; i++)
        if (shared[i] != M + (uint64_t)i) { raw_write("FAIL: shared_read\n"); shared_ok = false; break; }
    if (shared_ok) raw_write("PASS: c1_shared_read\n");
    ok = ok && shared_ok;

    ok1 = ok ? 1 : 0;
    return nullptr;
}

int main() {
    pthread_t t;
    pthread_create(&t, nullptr, c1, nullptr);
    c0(nullptr);
    for (volatile int i = 0; i < 50000; i++) __asm__ volatile("nop");
    if (ok0 && ok1) raw_write("ALL TESTS PASSED\n");
    else raw_write("SOME TESTS FAILED\n");
    _exit((ok0 && ok1) ? 0 : 1);
}
