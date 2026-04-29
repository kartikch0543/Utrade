#pragma once

#include "task.h"

#include <cstddef>
#include <chrono>
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
    std::vector<std::string> topological_order() const;
    void validate_acyclic() const;
    std::chrono::milliseconds critical_path_length() const;

private:
    std::unordered_map<std::string, std::vector<std::string>> adjacency_list_;
    std::unordered_map<std::string, std::size_t> indegree_by_task_;
    std::unordered_map<std::string, std::chrono::milliseconds> duration_by_task_;
    std::unordered_map<std::string, std::vector<std::string>> dependencies_by_task_;
};

}  // namespace scheduler
