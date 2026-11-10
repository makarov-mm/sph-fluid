#include "thread_pool.hpp"

ThreadPool::ThreadPool(unsigned threads) {
    if (threads == 0) threads = std::max(1u, std::thread::hardware_concurrency());
    workers_.reserve(threads - 1);
    for (unsigned i = 1; i < threads; ++i) workers_.emplace_back([this] { workerLoop(); });
}

ThreadPool::~ThreadPool() {
    stopping_.store(true, std::memory_order_relaxed);
    generation_.fetch_add(1, std::memory_order_release);
    generation_.notify_all();
    for (std::thread& worker : workers_) worker.join();
}

void ThreadPool::dispatch(unsigned taskCount, const void* task, Invoke invoke) {
    task_ = task;
    invoke_ = invoke;
    taskCount_ = taskCount;
    nextTask_.store(0, std::memory_order_relaxed);
    busyWorkers_.store(static_cast<unsigned>(workers_.size()), std::memory_order_relaxed);
    generation_.fetch_add(1, std::memory_order_release);
    generation_.notify_all();

    drain();

    // Every worker checks in, even one that found no task left, so the job fields
    // above are no longer read once this returns and the next dispatch may reuse them.
    for (unsigned busy; (busy = busyWorkers_.load(std::memory_order_acquire)) != 0;)
        busyWorkers_.wait(busy, std::memory_order_acquire);
}

void ThreadPool::drain() {
    for (unsigned i; (i = nextTask_.fetch_add(1, std::memory_order_relaxed)) < taskCount_;)
        invoke_(task_, i);
}

void ThreadPool::workerLoop() {
    std::uint32_t seen = 0;
    for (;;) {
        generation_.wait(seen, std::memory_order_acquire);
        seen = generation_.load(std::memory_order_acquire);
        if (stopping_.load(std::memory_order_relaxed)) return;
        drain();
        if (busyWorkers_.fetch_sub(1, std::memory_order_acq_rel) == 1) busyWorkers_.notify_one();
    }
}
