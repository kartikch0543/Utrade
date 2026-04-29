#include "scheduler.h"

#include "nlohmann/json.hpp"

#include <fstream>
#include <sstream>
#include <stdexcept>
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

        for (const auto& dependency : task_json.at("dependencies")) {
            if (!dependency.is_string() || dependency.get<std::string>().empty()) {
                throw_config_error(file_path, "task '" + id + "' has an invalid dependency entry");
            }
            dependencies.push_back(dependency.get<std::string>());
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

}  // namespace

Scheduler::Scheduler(SchedulerOptions options)
    : options_(std::move(options)) {}

int Scheduler::run() {
    load_tasks_from_file();
    return 0;
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

}  // namespace scheduler
