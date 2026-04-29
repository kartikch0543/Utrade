#include "graph.h"

#include <queue>
#include <stdexcept>

namespace scheduler {

Graph::Graph(const std::vector<Task>& tasks) {
    adjacency_list_.clear();
    indegree_by_task_.clear();

    for (const auto& task : tasks) {
        adjacency_list_.emplace(task.id, std::vector<std::string>{});
        indegree_by_task_.emplace(task.id, 0U);
    }

    for (const auto& task : tasks) {
        for (const auto& dependency_id : task.dependencies) {
            if (dependency_id == task.id) {
                throw std::runtime_error("Task '" + task.id + "' cannot depend on itself");
            }

            auto dependency_it = adjacency_list_.find(dependency_id);
            if (dependency_it == adjacency_list_.end()) {
                throw std::runtime_error("Cannot build graph: missing dependency '" + dependency_id + "'");
            }

            dependency_it->second.push_back(task.id);
            ++indegree_by_task_.at(task.id);
        }
    }
}

const std::unordered_map<std::string, std::vector<std::string>>& Graph::adjacency_list() const {
    return adjacency_list_;
}

const std::unordered_map<std::string, std::size_t>& Graph::indegree_by_task() const {
    return indegree_by_task_;
}

std::vector<std::string> Graph::task_ids() const {
    std::vector<std::string> ids;
    ids.reserve(indegree_by_task_.size());

    for (const auto& entry : indegree_by_task_) {
        ids.push_back(entry.first);
    }

    return ids;
}

std::vector<std::string> Graph::topological_order() const {
    std::queue<std::string> ready;
    std::unordered_map<std::string, std::size_t> indegree = indegree_by_task_;

    for (const auto& entry : indegree) {
        if (entry.second == 0U) {
            ready.push(entry.first);
        }
    }

    std::vector<std::string> ordered;
    ordered.reserve(indegree.size());

    while (!ready.empty()) {
        const auto task_id = ready.front();
        ready.pop();
        ordered.push_back(task_id);

        const auto adjacency_it = adjacency_list_.find(task_id);
        if (adjacency_it == adjacency_list_.end()) {
            continue;
        }

        for (const auto& dependent_id : adjacency_it->second) {
            auto& dependent_indegree = indegree.at(dependent_id);
            --dependent_indegree;
            if (dependent_indegree == 0U) {
                ready.push(dependent_id);
            }
        }
    }

    return ordered;
}

void Graph::validate_acyclic() const {
    const auto ordered = topological_order();
    if (ordered.size() != indegree_by_task_.size()) {
        throw std::runtime_error("Dependency graph contains a cycle");
    }
}

}  // namespace scheduler
