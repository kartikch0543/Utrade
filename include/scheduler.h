#pragma once

#include "task.h"

#include <cstddef>
#include <queue>
#include <string>
#include <unordered_map>
#include <vector>

namespace scheduler {

struct SchedulerOptions {
    std::string config_path;
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

    SchedulerOptions options_;
    std::vector<Task> tasks_;
    std::unordered_map<std::string, Task*> tasks_by_id_;
    std::unordered_map<std::string, std::vector<std::string>> adjacency_list_;
    std::unordered_map<std::string, std::size_t> remaining_dependencies_;
    std::priority_queue<ReadyTask, std::vector<ReadyTask>, ReadyTaskCompare> ready_queue_;
    std::size_t enqueue_sequence_;
};

}  // namespace scheduler
