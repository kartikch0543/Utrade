#include "scheduler.h"

#include "graph.h"
#include "nlohmann/json.hpp"
#include "worker_pool.h"

#include <algorithm>
#include <chrono>
#include <functional>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <limits>
#include <sstream>
#include <stdexcept>
#include <thread>
#include <unordered_set>

namespace scheduler {

namespace {

using json = nlohmann::json;

[[noreturn]] void throw_config_error(const std::string& file_path, const std::string& message) {
    throw std::runtime_error("Configuration error in '" + file_path + "': " + message);
}

std::string require_string(const std::string& file_path, const json& task_json, const char* field_name) {
    if (!task_json.contains(field_name) || !task_json.at(field_name).is_string()) {
        throw_config_error(file_path, "task is missing string field '" + std::string(field_name) + "'");
    }

    const auto value = task_json.at(field_name).get<std::string>();
    if (value.empty()) {
        throw_config_error(file_path, "task field '" + std::string(field_name) + "' must not be empty");
    }

    return value;
}

int require_int(const std::string& file_path, const json& task_json, const char* field_name) {
    if (!task_json.contains(field_name) || !task_json.at(field_name).is_number_integer()) {
        throw_config_error(file_path, "task is missing integer field '" + std::string(field_name) + "'");
    }

    return task_json.at(field_name).get<int>();
}

Task parse_task(const std::string& file_path, const json& task_json) {
    const auto id = require_string(file_path, task_json, "id");
    const auto name = require_string(file_path, task_json, "name");
    const auto priority = require_int(file_path, task_json, "priority");
    const auto duration_ms = require_int(file_path, task_json, "duration");

    if (duration_ms < 0) {
        throw_config_error(file_path, "task '" + id + "' has negative duration");
    }

    std::vector<std::string> dependencies;
    if (task_json.contains("dependencies")) {
        if (!task_json.at("dependencies").is_array()) {
            throw_config_error(file_path, "task '" + id + "' has non-array dependencies");
        }

        std::unordered_set<std::string> seen_dependencies;
        for (const auto& dependency : task_json.at("dependencies")) {
            if (!dependency.is_string() || dependency.get<std::string>().empty()) {
                throw_config_error(file_path, "task '" + id + "' has an invalid dependency entry");
            }

            const auto dependency_id = dependency.get<std::string>();
            if (!seen_dependencies.insert(dependency_id).second) {
                throw_config_error(file_path,
                                   "task '" + id + "' declares duplicate dependency '" + dependency_id + "'");
            }

            dependencies.push_back(dependency_id);
        }
    }

    Task task(id, name, priority, std::move(dependencies), std::chrono::milliseconds(duration_ms));

    if (task_json.contains("retry_count")) {
        if (!task_json.at("retry_count").is_number_integer() || task_json.at("retry_count").get<int>() < 0) {
            throw_config_error(file_path, "task '" + id + "' has an invalid retry_count");
        }
        task.retry_count = task_json.at("retry_count").get<int>();
    }

    if (task_json.contains("fail_probability")) {
        if (!task_json.at("fail_probability").is_number()) {
            throw_config_error(file_path, "task '" + id + "' has a non-numeric fail_probability");
        }

        task.fail_probability = task_json.at("fail_probability").get<double>();
        if (task.fail_probability < 0.0 || task.fail_probability > 1.0) {
            throw_config_error(file_path, "task '" + id + "' has fail_probability outside [0.0, 1.0]");
        }
    }

    return task;
}

}

Scheduler::Scheduler(SchedulerOptions options)
    : options_(std::move(options)),
      enqueue_sequence_(0U),
      running_tasks_(0U),
      completed_tasks_(0U),
      failed_tasks_(0U),
      cancelled_tasks_(0U) {}

int Scheduler::run() {
    load_tasks_from_file();
    Graph graph(tasks_);
    graph.validate_acyclic();
    if (!options_.dot_output_path.empty()) {
        std::ofstream dot_output(options_.dot_output_path);
        if (!dot_output.is_open()) {
            throw std::runtime_error("Unable to open DOT output path: " + options_.dot_output_path);
        }
        dot_output << graph.to_dot();
    }
    initialize_scheduler_state();

    if (options_.worker_count == 0U) {
        throw std::runtime_error("Worker count must be greater than zero");
    }

    run_started_at_ = std::chrono::steady_clock::now();
    WorkerPool pool(options_.worker_count);

    {
        std::unique_lock<std::mutex> lock(mutex_);
        dispatch_ready_tasks(pool);

        while (!all_tasks_terminal()) {
            state_changed_.wait(lock, [this]() {
                return all_tasks_terminal() || (!ready_queue_.empty() && has_worker_capacity());
            });
            dispatch_ready_tasks(pool);
        }
    }

    pool.shutdown();
    print_summary(graph, std::chrono::steady_clock::now());
    return failed_tasks_ == 0U ? 0 : 1;
}

void Scheduler::load_tasks_from_file() {
    if (options_.config_path.empty()) {
        throw std::runtime_error("Configuration path must not be empty");
    }

    std::ifstream input(options_.config_path);
    if (!input.is_open()) {
        throw std::runtime_error("Unable to open config file: " + options_.config_path);
    }

    std::ostringstream buffer;
    buffer << input.rdbuf();

    if (buffer.str().empty()) {
        throw std::runtime_error("Config file is empty: " + options_.config_path);
    }

    json root;
    try {
        root = json::parse(buffer.str());
    } catch (const json::parse_error& error) {
        throw std::runtime_error("Invalid JSON in '" + options_.config_path + "': " + std::string(error.what()));
    }

    if (!root.is_object() || !root.contains("tasks") || !root.at("tasks").is_array()) {
        throw std::runtime_error("Config root must be an object containing a 'tasks' array");
    }

    const auto& task_array = root.at("tasks");
    tasks_.clear();
    tasks_.reserve(task_array.size());

    std::unordered_set<std::string> task_ids;
    for (const auto& task_json : task_array) {
        if (!task_json.is_object()) {
            throw_config_error(options_.config_path, "each task entry must be a JSON object");
        }

        Task task = parse_task(options_.config_path, task_json);
        if (!task_ids.insert(task.id).second) {
            throw_config_error(options_.config_path, "duplicate task id '" + task.id + "'");
        }
        tasks_.push_back(std::move(task));
    }

    for (const auto& task : tasks_) {
        for (const auto& dependency_id : task.dependencies) {
            if (task_ids.find(dependency_id) == task_ids.end()) {
                throw_config_error(options_.config_path,
                                   "task '" + task.id + "' references missing dependency '" + dependency_id + "'");
            }
        }
    }
}

const std::vector<Task>& Scheduler::tasks() const {
    return tasks_;
}

bool Scheduler::ReadyTaskCompare::operator()(const ReadyTask& lhs, const ReadyTask& rhs) const {
    if (lhs.priority != rhs.priority) {
        return lhs.priority < rhs.priority;
    }

    if (lhs.insertion_order != rhs.insertion_order) {
        return lhs.insertion_order > rhs.insertion_order;
    }

    return lhs.task_id > rhs.task_id;
}

void Scheduler::initialize_scheduler_state() {
    Graph graph(tasks_);

    tasks_by_id_.clear();
    tasks_by_id_.reserve(tasks_.size());
    adjacency_list_ = graph.adjacency_list();
    remaining_dependencies_ = graph.indegree_by_task();
    attempts_by_task_.clear();
    attempts_by_task_.reserve(tasks_.size());
    ready_queue_ = std::priority_queue<ReadyTask, std::vector<ReadyTask>, ReadyTaskCompare>();
    enqueue_sequence_ = 0U;
    running_tasks_ = 0U;
    completed_tasks_ = 0U;
    failed_tasks_ = 0U;
    cancelled_tasks_ = 0U;

    for (auto& task : tasks_) {
        task.state = TaskState::Pending;
        task.has_started = false;
        task.has_finished = false;
        tasks_by_id_[task.id] = &task;
        attempts_by_task_[task.id] = 0;
    }

    for (const auto& task : tasks_) {
        const auto dependency_count_it = remaining_dependencies_.find(task.id);
        if (dependency_count_it != remaining_dependencies_.end() && dependency_count_it->second == 0U) {
            enqueue_ready_task(task.id);
        }
    }
}

void Scheduler::enqueue_ready_task(const std::string& task_id) {
    const auto task_it = tasks_by_id_.find(task_id);
    if (task_it == tasks_by_id_.end()) {
        throw std::runtime_error("Cannot enqueue unknown task '" + task_id + "'");
    }

    ready_queue_.push(ReadyTask{task_it->second->priority, enqueue_sequence_++, task_id});
}

std::string Scheduler::pop_next_ready_task() {
    if (ready_queue_.empty()) {
        throw std::runtime_error("Ready queue is empty");
    }

    const auto ready_task = ready_queue_.top();
    ready_queue_.pop();
    return ready_task.task_id;
}

void Scheduler::mark_task_completed(const std::string& task_id) {
    const auto task_it = tasks_by_id_.find(task_id);
    if (task_it == tasks_by_id_.end()) {
        throw std::runtime_error("Cannot complete unknown task '" + task_id + "'");
    }

    task_it->second->state = TaskState::Completed;

    const auto adjacency_it = adjacency_list_.find(task_id);
    if (adjacency_it == adjacency_list_.end()) {
        return;
    }

    for (const auto& dependent_id : adjacency_it->second) {
        auto& dependency_count = remaining_dependencies_.at(dependent_id);
        if (dependency_count == 0U) {
            throw std::runtime_error("Dependency count underflow for task '" + dependent_id + "'");
        }

        --dependency_count;
        if (dependency_count == 0U) {
            enqueue_ready_task(dependent_id);
        }
    }
}

void Scheduler::dispatch_ready_tasks(WorkerPool& pool) {
    std::vector<std::pair<std::string, int>> to_launch;

    while (!ready_queue_.empty() && has_worker_capacity()) {
        const auto task_id = pop_next_ready_task();
        const auto task_it = tasks_by_id_.find(task_id);
        if (task_it == tasks_by_id_.end()) {
            throw std::runtime_error("Cannot dispatch unknown task '" + task_id + "'");
        }

        Task& task = *task_it->second;
        if (task.state != TaskState::Pending) {
            continue;
        }

        const int attempt_number = ++attempts_by_task_.at(task_id);
        task.state = TaskState::Running;
        ++running_tasks_;
        to_launch.emplace_back(task_id, attempt_number);
    }

    for (const auto& launch : to_launch) {
        pool.submit([this, launch]() {
            execute_task(launch.first, launch.second);
        });
    }
}

void Scheduler::execute_task(const std::string& task_id, int attempt_number) {
    std::chrono::milliseconds duration(0);
    {
        std::lock_guard<std::mutex> lock(mutex_);
        Task& task = *tasks_by_id_.at(task_id);
        task.has_started = true;
        task.started_at = std::chrono::steady_clock::now();
        duration = task.duration;
        log_event(task, "STARTED", std::this_thread::get_id(), attempt_number);
    }

    std::this_thread::sleep_for(duration);

    bool success = false;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        success = !should_fail(*tasks_by_id_.at(task_id), attempt_number);
    }

    {
        std::lock_guard<std::mutex> lock(mutex_);
        Task& locked_task = *tasks_by_id_.at(task_id);
        locked_task.has_finished = true;
        locked_task.finished_at = std::chrono::steady_clock::now();
        --running_tasks_;

        if (success) {
            locked_task.state = TaskState::Completed;
            ++completed_tasks_;
            log_event(locked_task, "COMPLETED", std::this_thread::get_id(), attempt_number);
            mark_task_completed(task_id);
        } else if (attempt_number <= locked_task.retry_count) {
            log_event(locked_task, "RETRYING", std::this_thread::get_id(), attempt_number, "retry scheduled");
            locked_task.state = TaskState::Pending;
            locked_task.has_finished = false;
            enqueue_ready_task(task_id);
        } else {
            locked_task.state = TaskState::Failed;
            ++failed_tasks_;
            log_event(locked_task, "FAILED", std::this_thread::get_id(), attempt_number);
            cancel_dependents(task_id);
        }
    }

    state_changed_.notify_all();
}

bool Scheduler::all_tasks_terminal() const {
    return completed_tasks_ + failed_tasks_ + cancelled_tasks_ == tasks_.size() && running_tasks_ == 0U;
}

bool Scheduler::should_fail(const Task& task, int attempt_number) const {
    if (task.fail_probability <= 0.0) {
        return false;
    }

    const auto fingerprint =
        static_cast<unsigned long long>(std::hash<std::string>{}(task.id + ":" + std::to_string(attempt_number)));
    const double normalized = static_cast<double>(fingerprint % 10000ULL) / 10000.0;
    return normalized < task.fail_probability;
}

bool Scheduler::has_worker_capacity() const {
    return running_tasks_ < options_.worker_count;
}

void Scheduler::cancel_dependents(const std::string& task_id) {
    const auto adjacency_it = adjacency_list_.find(task_id);
    if (adjacency_it == adjacency_list_.end()) {
        return;
    }

    for (const auto& dependent_id : adjacency_it->second) {
        Task& dependent = *tasks_by_id_.at(dependent_id);
        if (dependent.state == TaskState::Completed ||
            dependent.state == TaskState::Failed ||
            dependent.state == TaskState::Cancelled ||
            dependent.state == TaskState::Running) {
            continue;
        }

        dependent.state = TaskState::Cancelled;
        dependent.has_finished = true;
        dependent.finished_at = std::chrono::steady_clock::now();
        ++cancelled_tasks_;
        log_event(dependent, "CANCELLED", std::this_thread::get_id(), attempts_by_task_.at(dependent_id),
                  "blocked by failed dependency");
        cancel_dependents(dependent_id);
    }
}

void Scheduler::log_event(const Task& task,
                          const char* event,
                          std::thread::id thread_id,
                          int attempt_number,
                          const std::string& detail) const {
    const auto now = std::chrono::steady_clock::now();
    const auto elapsed_ms =
        std::chrono::duration_cast<std::chrono::milliseconds>(now - run_started_at_).count();

    std::ostringstream line;
    line << '[' << std::setw(6) << elapsed_ms << " ms] "
         << "Task " << task.id << " (" << task.name << ") " << event
         << " (priority=" << task.priority
         << ", thread=" << thread_id
         << ", attempt=" << attempt_number << ')';

    if (!detail.empty()) {
        line << " - " << detail;
    }

    std::lock_guard<std::mutex> lock(log_mutex_);
    std::cout << line.str() << '\n';
}

void Scheduler::print_summary(const Graph& graph,
                              std::chrono::steady_clock::time_point run_finished_at) const {
    std::vector<const Task*> ordered_tasks;
    ordered_tasks.reserve(tasks_.size());
    for (const auto& task : tasks_) {
        ordered_tasks.push_back(&task);
    }

    std::sort(ordered_tasks.begin(), ordered_tasks.end(), [](const Task* lhs, const Task* rhs) {
        return lhs->id < rhs->id;
    });

    const auto total_runtime_ms =
        std::chrono::duration_cast<std::chrono::milliseconds>(run_finished_at - run_started_at_).count();
    const auto critical_path_ms = graph.critical_path_length().count();

    std::ostringstream report;
    report << "Execution Summary\n";
    report << "=================\n";
    report << "Total execution time: " << total_runtime_ms << " ms\n";
    report << "Critical path length: " << critical_path_ms << " ms\n";
    report << "Completed: " << completed_tasks_
           << ", Failed: " << failed_tasks_
           << ", Cancelled: " << cancelled_tasks_ << "\n\n";
    report << "Per-task timings:\n";

    for (const auto* task : ordered_tasks) {
        report << "  - " << task->id
               << " (" << task->name << ')'
               << " [" << to_string(task->state) << ']'
               << " attempts=" << attempts_by_task_.at(task->id);

        if (task->has_started && task->has_finished) {
            const auto task_runtime_ms =
                std::chrono::duration_cast<std::chrono::milliseconds>(task->finished_at - task->started_at).count();
            report << " duration=" << task_runtime_ms << " ms";
        } else {
            report << " duration=n/a";
        }

        report << '\n';
    }

    std::lock_guard<std::mutex> lock(log_mutex_);
    std::cout << report.str();
}

}
