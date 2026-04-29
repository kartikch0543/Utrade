#pragma once

#include <condition_variable>
#include <cstddef>
#include <functional>
#include <mutex>
#include <queue>
#include <thread>
#include <vector>

namespace scheduler {

class WorkerPool {
public:
    explicit WorkerPool(std::size_t worker_count);
    ~WorkerPool();

    WorkerPool(const WorkerPool&) = delete;
    WorkerPool& operator=(const WorkerPool&) = delete;

    void submit(std::function<void()> job);
    void shutdown();
    std::size_t size() const;

private:
    void worker_loop();

    std::vector<std::thread> workers_;
    std::queue<std::function<void()>> jobs_;
    mutable std::mutex mutex_;
    std::condition_variable condition_;
    bool stopping_;
};

}
