#pragma once

#include "task.h"

#include <string>
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
    SchedulerOptions options_;
    std::vector<Task> tasks_;
};

}  // namespace scheduler
