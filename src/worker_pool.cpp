#include "worker_pool.h"

#include <stdexcept>

namespace scheduler {

WorkerPool::WorkerPool(std::size_t worker_count)
    : stopping_(false) {
    if (worker_count == 0U) {
        throw std::invalid_argument("Worker pool size must be greater than zero");
    }

    workers_.reserve(worker_count);
    for (std::size_t index = 0; index < worker_count; ++index) {
        workers_.emplace_back(&WorkerPool::worker_loop, this);
    }
}

WorkerPool::~WorkerPool() {
    shutdown();
}

void WorkerPool::submit(std::function<void()> job) {
    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (stopping_) {
            throw std::runtime_error("Cannot submit job to stopped worker pool");
        }

        jobs_.push(std::move(job));
    }

    condition_.notify_one();
}

void WorkerPool::shutdown() {
    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (stopping_) {
            return;
        }

        stopping_ = true;
    }

    condition_.notify_all();

    for (auto& worker : workers_) {
        if (worker.joinable()) {
            worker.join();
        }
    }
}

std::size_t WorkerPool::size() const {
    return workers_.size();
}

void WorkerPool::worker_loop() {
    while (true) {
        std::function<void()> job;

        {
            std::unique_lock<std::mutex> lock(mutex_);
            condition_.wait(lock, [this]() { return stopping_ || !jobs_.empty(); });

            if (stopping_ && jobs_.empty()) {
                return;
            }

            job = std::move(jobs_.front());
            jobs_.pop();
        }

        job();
    }
}

}
