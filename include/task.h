#pragma once

#include <chrono>
#include <string>
#include <vector>

namespace scheduler {

enum class TaskState {
    Pending,
    Running,
    Completed,
    Failed,
    Cancelled
};

std::string to_string(TaskState state);

class Task {
public:
    Task(std::string id,
         std::string name,
         int priority,
         std::vector<std::string> dependencies,
         std::chrono::milliseconds duration);

    bool is_terminal() const;

    std::string id;
    std::string name;
    int priority;
    std::vector<std::string> dependencies;
    std::chrono::milliseconds duration;
    TaskState state;
    int retry_count;
    double fail_probability;
    bool has_started;
    bool has_finished;
    std::chrono::steady_clock::time_point started_at;
    std::chrono::steady_clock::time_point finished_at;
};

}
