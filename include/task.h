#pragma once

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

class Task {
public:
    Task();

    std::string id;
    std::string name;
    int priority;
    std::vector<std::string> dependencies;
    int duration_ms;
    TaskState state;
};

}  // namespace scheduler
