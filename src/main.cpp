#include "scheduler.h"

#include <cstdlib>
#include <exception>
#include <iostream>
#include <limits>

namespace {

void print_usage() {
    std::cerr << "Usage: scheduler --config <path-to-json> [--workers <count>] [--dot <path-to-dot>]\n";
}

std::size_t parse_worker_count(const std::string& value) {
    char* end = nullptr;
    const auto parsed = std::strtoul(value.c_str(), &end, 10);
    if (end == value.c_str() || *end != '\0') {
        throw std::runtime_error("Invalid worker count: " + value);
    }

    if (parsed == 0UL) {
        throw std::runtime_error("Worker count must be greater than zero");
    }

    if (parsed > static_cast<unsigned long>(std::numeric_limits<std::size_t>::max())) {
        throw std::runtime_error("Worker count is too large: " + value);
    }

    return static_cast<std::size_t>(parsed);
}

}  // namespace

int main(int argc, char** argv) {
    std::string config_path;
    std::string dot_output_path;
    std::size_t worker_count = 0U;

    for (int index = 1; index < argc; ++index) {
        const std::string argument = argv[index];
        if (argument == "--config" && index + 1 < argc) {
            config_path = argv[++index];
            continue;
        }
        if (argument == "--workers" && index + 1 < argc) {
            worker_count = parse_worker_count(argv[++index]);
            continue;
        }
        if (argument == "--dot" && index + 1 < argc) {
            dot_output_path = argv[++index];
            continue;
        }

        print_usage();
        return 1;
    }

    if (config_path.empty()) {
        print_usage();
        return 1;
    }

    try {
        if (worker_count == 0U) {
            worker_count = 1U;
        }

        scheduler::Scheduler scheduler({config_path, dot_output_path, worker_count});
        return scheduler.run();
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
