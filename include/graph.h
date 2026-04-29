#pragma once

#include "task.h"

#include <cstddef>
#include <string>
#include <unordered_map>
#include <vector>

namespace scheduler {

class Graph {
public:
    Graph() = default;
    explicit Graph(const std::vector<Task>& tasks);

    const std::unordered_map<std::string, std::vector<std::string>>& adjacency_list() const;
    const std::unordered_map<std::string, std::size_t>& indegree_by_task() const;
    std::vector<std::string> task_ids() const;

private:
    std::unordered_map<std::string, std::vector<std::string>> adjacency_list_;
    std::unordered_map<std::string, std::size_t> indegree_by_task_;
};

}  // namespace scheduler
