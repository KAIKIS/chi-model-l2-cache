#include <cstdint>
#include <atomic>
#include <unistd.h>

// Shared data between cores (volatile to prevent optimization)
static volatile uint64_t shared_data[1024 * 128] __attribute__((aligned(64)));

// Synchronization flags (atomic for cross-core visibility)
static std::atomic<uint64_t> sync_flags[16] = {};
static std::atomic<uint64_t> core_id_counter = {0};

// Raw syscall write for output (avoids libc printf which may hang gem5)
static void raw_write(const char* msg) {
    size_t len = 0;
    while (msg[len]) len++;
    write(1, msg, len);
}

// Spin barrier: waits until both cores reach this point
static void barrier(int barrier_num) {
    sync_flags[barrier_num].fetch_add(1, std::memory_order_release);
    while (sync_flags[barrier_num].load(std::memory_order_acquire) < 2) {
        // spin
    }
}

// Get this core's ID (0 or 1)
static int get_core_id() {
    return core_id_counter.fetch_add(1, std::memory_order_relaxed);
}

// Test 1: Shared read — Core 0 writes, Core 1 reads
static bool test_shared_read(int my_id) {
    constexpr int NUM_LINES = 64;  // 64 cache lines = 4KB

    if (my_id == 0) {
        for (int i = 0; i < NUM_LINES; i++) {
            shared_data[i * 8] = static_cast<uint64_t>(i) * 0xAAAAAAAA;
        }
        barrier(0);
    } else {
        barrier(0);
        uint64_t checksum = 0;
        for (int i = 0; i < NUM_LINES; i++) {
            checksum += shared_data[i * 8];
        }
        uint64_t expected = 0;
        for (int i = 0; i < NUM_LINES; i++) {
            expected += static_cast<uint64_t>(i) * 0xAAAAAAAA;
        }
        if (checksum != expected) {
            raw_write("FAIL: shared_read checksum mismatch\n");
            return false;
        }
        raw_write("PASS: shared_read\n");
    }
    return true;
}

// Test 2: Write conflict — Core 0 writes, Core 1 overwrites, Core 0 reads
static bool test_write_conflict(int my_id) {
    if (my_id == 0) {
        shared_data[0] = 0xDEAD0000;
        barrier(1);
        barrier(2);
        uint64_t val = shared_data[0];
        if (val != 0xBEEF0000) {
            raw_write("FAIL: write_conflict expected 0xBEEF0000\n");
            return false;
        }
        raw_write("PASS: write_conflict\n");
    } else {
        barrier(1);
        shared_data[0] = 0xBEEF0000;
        barrier(2);
    }
    return true;
}

// Test 3: Ping-pong — alternating writes
static bool test_pingpong(int my_id) {
    constexpr int ROUNDS = 10;

    if (my_id == 0) {
        for (int i = 0; i < ROUNDS; i++) {
            shared_data[0] = static_cast<uint64_t>(i * 2);
            barrier(3 + i * 2);
            barrier(4 + i * 2);
            uint64_t val = shared_data[0];
            if (val != static_cast<uint64_t>(i * 2 + 1)) {
                raw_write("FAIL: pingpong\n");
                return false;
            }
        }
        raw_write("PASS: pingpong\n");
    } else {
        for (int i = 0; i < ROUNDS; i++) {
            barrier(3 + i * 2);
            uint64_t val = shared_data[0];
            if (val != static_cast<uint64_t>(i * 2)) {
                raw_write("FAIL: pingpong\n");
                return false;
            }
            shared_data[0] = static_cast<uint64_t>(i * 2 + 1);
            barrier(4 + i * 2);
        }
    }
    return true;
}

int main() {
    int my_id = get_core_id();

    bool ok = true;
    ok = test_shared_read(my_id) && ok;
    ok = test_write_conflict(my_id) && ok;
    ok = test_pingpong(my_id) && ok;

    if (my_id == 1) {
        if (ok) {
            raw_write("ALL TESTS PASSED\n");
        } else {
            raw_write("SOME TESTS FAILED\n");
        }
    }

    _exit(ok ? 0 : 1);
}
