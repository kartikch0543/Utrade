#include "task.h"

namespace scheduler {

Task::Task()
    : priority(0),
      duration_ms(0),
      state(TaskState::Pending) {}

}  // namespace scheduler
