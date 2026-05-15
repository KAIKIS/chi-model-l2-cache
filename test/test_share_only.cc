// Minimal true-sharing-only test. Single cache line shared between cores.

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

static volatile uint64_t shared __attribute__((aligned(64)));
static volatile uint64_t flag   __attribute__((aligned(64)));

static void* c0(void*) {
    raw_write("c0: write\n");
    shared = 0xCAFE;
    __asm__ volatile("dmb sy" ::: "memory");
    flag = 1;
    __asm__ volatile("dmb sy" ::: "memory");
    futex_wake(&flag);
    return nullptr;
}

static void* c1(void*) {
    raw_write("c1: wait\n");
    while (flag == 0) futex_wait(&flag, 0);
    __asm__ volatile("dmb sy" ::: "memory");
    if (shared == 0xCAFE) raw_write("PASS\n");
    else raw_write("FAIL\n");
    return nullptr;
}

int main() {
    pthread_t t;
    pthread_create(&t, nullptr, c1, nullptr);
    c0(nullptr);
    for (volatile int i = 0; i < 500000; i++) __asm__ volatile("nop");
    _exit(0);
}
