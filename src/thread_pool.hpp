#pragma once
#include <algorithm>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <thread>
#include <type_traits>
#include <vector>

// Persistent fork-join pool for data-parallel loops. Workers sleep between jobs,
// so a parallel section costs a wake-up instead of creating threads, and tasks are
// handed out dynamically so uneven work still balances across cores.
// Jobs are submitted from one thread at a time; tasks must not throw.
class ThreadPool {
public:
    // `threads` includes the calling thread; 0 means std::thread::hardware_concurrency().
    explicit ThreadPool(unsigned threads = 0);
    ~ThreadPool();
    ThreadPool(const ThreadPool&) = delete;
    ThreadPool& operator=(const ThreadPool&) = delete;

    unsigned threadCount() const noexcept { return static_cast<unsigned>(workers_.size()) + 1; }

    // Calls task(i) for every i in [0, taskCount); the caller takes part and the call
    // returns once all tasks have finished.
    template <class Task>
    void run(unsigned taskCount, Task&& task) {
        if (taskCount == 0) return;
        if (taskCount == 1 || workers_.empty()) {
            for (unsigned i = 0; i < taskCount; ++i) task(i);
            return;
        }
        using Fn = std::remove_reference_t<Task>;
        dispatch(taskCount, std::addressof(task), [](const void* fn, unsigned i) {
            (*static_cast<Fn*>(const_cast<void*>(fn)))(i);
        });
    }

    // Splits [0, count) into chunks of `grain` items and calls body(begin, end) on each.
    template <class Body>
    void parallelFor(std::size_t count, std::size_t grain, Body&& body) {
        grain = std::max<std::size_t>(grain, 1);
        const auto chunks = static_cast<unsigned>((count + grain - 1) / grain);
        run(chunks, [&](unsigned chunk) {
            const std::size_t begin = std::size_t{chunk} * grain;
            body(begin, std::min(begin + grain, count));
        });
    }

private:
    using Invoke = void (*)(const void* task, unsigned index);

    void dispatch(unsigned taskCount, const void* task, Invoke invoke);
    void drain();
    void workerLoop();

    std::vector<std::thread> workers_;

    // Current job; written by dispatch() before the generation bump publishes it.
    const void* task_ = nullptr;
    Invoke invoke_ = nullptr;
    unsigned taskCount_ = 0;

    std::atomic<unsigned> nextTask_{0};
    std::atomic<unsigned> busyWorkers_{0};
    std::atomic<std::uint32_t> generation_{0};
    std::atomic<bool> stopping_{false};
};
