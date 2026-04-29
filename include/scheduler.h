#pragma once

#include "task.h"

#include <chrono>
#include <condition_variable>
#include <cstddef>
#include <mutex>
#include <queue>
#include <string>
#include <unordered_map>
#include <vector>

namespace scheduler {

struct SchedulerOptions {
    std::string config_path;
    std::size_t worker_count;
};

class Scheduler {
public:
    explicit Scheduler(SchedulerOptions options);

    int run();
    void load_tasks_from_file();
    const std::vector<Task>& tasks() const;

private:
    struct ReadyTask {
        int priority;
        std::size_t insertion_order;
        std::string task_id;
    };

    struct ReadyTaskCompare {
        bool operator()(const ReadyTask& lhs, const ReadyTask& rhs) const;
    };

    void initialize_scheduler_state();
    void enqueue_ready_task(const std::string& task_id);
    std::string pop_next_ready_task();
    void mark_task_completed(const std::string& task_id);
    void dispatch_ready_tasks(class WorkerPool& pool);
    void execute_task(const std::string& task_id, int attempt_number);
    bool all_tasks_terminal() const;
    bool should_fail(const Task& task, int attempt_number) const;
    void cancel_dependents(const std::string& task_id);

    SchedulerOptions options_;
    std::vector<Task> tasks_;
    std::unordered_map<std::string, Task*> tasks_by_id_;
    std::unordered_map<std::string, std::vector<std::string>> adjacency_list_;
    std::unordered_map<std::string, std::size_t> remaining_dependencies_;
    std::unordered_map<std::string, int> attempts_by_task_;
    std::priority_queue<ReadyTask, std::vector<ReadyTask>, ReadyTaskCompare> ready_queue_;
    std::size_t enqueue_sequence_;
    std::size_t running_tasks_;
    std::size_t completed_tasks_;
    std::size_t failed_tasks_;
    std::size_t cancelled_tasks_;
    std::chrono::steady_clock::time_point run_started_at_;
    mutable std::mutex mutex_;
    std::condition_variable state_changed_;
};

}  // namespace scheduler
