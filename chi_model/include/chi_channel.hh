#pragma once

#include <queue>
#include <mutex>
#include <condition_variable>
#include <optional>

namespace chi {

template<typename T>
class Channel {
public:
    void push(T item) {
        std::lock_guard<std::mutex> lock(mutex_);
        queue_.push(std::move(item));
        cv_.notify_one();
    }

    T pop() {
        std::unique_lock<std::mutex> lock(mutex_);
        cv_.wait(lock, [this] { return !queue_.empty(); });
        T item = std::move(queue_.front());
        queue_.pop();
        return item;
    }

    std::optional<T> tryPop() {
        std::lock_guard<std::mutex> lock(mutex_);
        if (queue_.empty()) return std::nullopt;
        T item = std::move(queue_.front());
        queue_.pop();
        return item;
    }

    bool empty() const {
        std::lock_guard<std::mutex> lock(mutex_);
        return queue_.empty();
    }

    size_t size() const {
        std::lock_guard<std::mutex> lock(mutex_);
        return queue_.size();
    }

private:
    std::queue<T> queue_;
    mutable std::mutex mutex_;
    std::condition_variable cv_;
};

// A bidirectional channel: external push/pop go through separate internal
// queues so that a processing loop can read inbound items and write outbound
// items without re-consuming its own output.
template<typename T>
class BidirectionalChannel {
public:
    // External push: places an item into the inbound queue.
    void push(T item) {
        inCh_.push(std::move(item));
    }

    // External pop: takes an item from the outbound queue (blocks).
    T pop() {
        return outCh_.pop();
    }

    // External tryPop: non-blocking pop from the outbound queue.
    std::optional<T> tryPop() {
        return outCh_.tryPop();
    }

    // Internal: non-blocking pop from the inbound queue (used by process loop).
    std::optional<T> tryPopIn() {
        return inCh_.tryPop();
    }

    // Internal: push to the outbound queue (used by process loop).
    void pushOut(T item) {
        outCh_.push(std::move(item));
    }

    bool empty() const {
        return inCh_.empty();
    }

    size_t size() const {
        return inCh_.size();
    }

private:
    Channel<T> inCh_;
    Channel<T> outCh_;
};

} // namespace chi
