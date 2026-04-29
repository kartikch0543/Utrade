#include "graph.h"

#include <algorithm>
#include <queue>
#include <sstream>
#include <stdexcept>

namespace scheduler {

Graph::Graph(const std::vector<Task>& tasks) {
    adjacency_list_.clear();
    indegree_by_task_.clear();

    for (const auto& task : tasks) {
        adjacency_list_.emplace(task.id, std::vector<std::string>{});
        indegree_by_task_.emplace(task.id, 0U);
        duration_by_task_.emplace(task.id, task.duration);
        dependencies_by_task_.emplace(task.id, task.dependencies);
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

std::chrono::milliseconds Graph::critical_path_length() const {
    const auto ordered = topological_order();
    if (ordered.size() != indegree_by_task_.size()) {
        throw std::runtime_error("Cannot compute critical path for cyclic graph");
    }

    std::unordered_map<std::string, std::chrono::milliseconds> longest_path;
    for (const auto& task_id : ordered) {
        std::chrono::milliseconds best_predecessor(0);
        const auto dependency_it = dependencies_by_task_.find(task_id);
        if (dependency_it != dependencies_by_task_.end()) {
            for (const auto& dependency_id : dependency_it->second) {
                const auto predecessor_it = longest_path.find(dependency_id);
                if (predecessor_it != longest_path.end() && predecessor_it->second > best_predecessor) {
                    best_predecessor = predecessor_it->second;
                }
            }
        }

        longest_path[task_id] = best_predecessor + duration_by_task_.at(task_id);
    }

    std::chrono::milliseconds result(0);
    for (const auto& entry : longest_path) {
        if (entry.second > result) {
            result = entry.second;
        }
    }

    return result;
}

std::string Graph::to_dot() const {
    std::vector<std::string> ids = task_ids();
    std::sort(ids.begin(), ids.end());

    std::ostringstream output;
    output << "digraph scheduler {\n";
    output << "  rankdir=LR;\n";

    for (const auto& id : ids) {
        output << "  \"" << id << "\";\n";
    }

    for (const auto& id : ids) {
        const auto dependency_it = dependencies_by_task_.find(id);
        if (dependency_it == dependencies_by_task_.end() || dependency_it->second.empty()) {
            continue;
        }

        std::vector<std::string> dependencies = dependency_it->second;
        std::sort(dependencies.begin(), dependencies.end());
        for (const auto& dependency_id : dependencies) {
            output << "  \"" << dependency_id << "\" -> \"" << id << "\";\n";
        }
    }

    output << "}\n";
    return output.str();
}

}  // namespace scheduler
