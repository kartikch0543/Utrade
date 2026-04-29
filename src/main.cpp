#include "scheduler.h"

#include <exception>
#include <iostream>

namespace {

void print_usage() {
    std::cerr << "Usage: scheduler --config <path-to-json>\n";
}

}  // namespace

int main(int argc, char** argv) {
    std::string config_path;

    for (int index = 1; index < argc; ++index) {
        const std::string argument = argv[index];
        if (argument == "--config" && index + 1 < argc) {
            config_path = argv[++index];
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
        scheduler::Scheduler scheduler({config_path});
        return scheduler.run();
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
