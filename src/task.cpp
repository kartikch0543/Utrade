#include "task.h"

#include <stdexcept>

namespace scheduler {

std::string to_string(TaskState state) {
    switch (state) {
        case TaskState::Pending:
            return "pending";
        case TaskState::Running:
            return "running";
        case TaskState::Completed:
            return "completed";
        case TaskState::Failed:
            return "failed";
        case TaskState::Cancelled:
            return "cancelled";
    }

    throw std::logic_error("Unhandled task state");
}

Task::Task(std::string task_id,
           std::string task_name,
           int task_priority,
           std::vector<std::string> task_dependencies,
           std::chrono::milliseconds task_duration)
    : id(std::move(task_id)),
      name(std::move(task_name)),
      priority(task_priority),
      dependencies(std::move(task_dependencies)),
      duration(task_duration),
      state(TaskState::Pending),
      retry_count(0),
      fail_probability(0.0),
      has_started(false),
      has_finished(false) {}

bool Task::is_terminal() const {
    return state == TaskState::Completed ||
           state == TaskState::Failed ||
           state == TaskState::Cancelled;
}

}
