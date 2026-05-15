// Minimal true-sharing test: one cache line, no large arrays.
// Core 0 writes a value, Core 1 reads and verifies.
// Synchronization via raw futex syscall (gem5 SE-mode compatible).

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

// A single shared cache line
static volatile uint64_t shared_val __attribute__((aligned(64)));
static volatile uint64_t flag      __attribute__((aligned(64)));

// Futex wrappers — kernel-level sync, reliable in gem5 SE mode
static void futex_wait(volatile uint64_t* addr, uint64_t expected) {
    syscall(SYS_futex, (uint64_t*)addr, FUTEX_WAIT, expected, 0, 0, 0);
}

static void futex_wake(volatile uint64_t* addr) {
    syscall(SYS_futex, (uint64_t*)addr, FUTEX_WAKE, 1, 0, 0, 0);
}

static void* core0_func(void*) {
    raw_write("core0: started\n");

    // Write to shared cache line
    shared_val = 0xDEADBEEFCAFEBABEULL;
    __asm__ volatile("dmb sy" ::: "memory");

    // Signal Core 1 via futex
    flag = 1;
    __asm__ volatile("dmb sy" ::: "memory");
    futex_wake(&flag);

    raw_write("core0: shared write done\n");
    return nullptr;
}

static void* core1_func(void*) {
    raw_write("core1: started\n");

    // Wait for Core 0's signal via futex
    while (flag == 0) {
        futex_wait(&flag, 0);
    }
    __asm__ volatile("dmb sy" ::: "memory");

    raw_write("core1: flag received\n");

    // Verify the shared data
    if (shared_val == 0xDEADBEEFCAFEBABEULL) {
        raw_write("PASS: core1 shared read\n");
    } else {
        raw_write("FAIL: core1 shared read mismatch\n");
    }

    return nullptr;
}

int main() {
    pthread_t t1;
    pthread_create(&t1, nullptr, core1_func, nullptr);
    core0_func(nullptr);
    // Sleep briefly to let Core 1 print its result, then exit.
    // Skipping pthread_join avoids gem5 futex deadlock in SE mode.
    for (volatile int i = 0; i < 100000; i++) __asm__ volatile("nop");
    raw_write("ALL DONE\n");
    _exit(0);
}
