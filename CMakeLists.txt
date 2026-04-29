cmake_minimum_required(VERSION 3.20)

project(scheduler VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

add_library(scheduler_lib
    src/graph.cpp
    src/scheduler.cpp
    src/task.cpp
    src/worker_pool.cpp
)

target_include_directories(scheduler_lib
    PUBLIC
        ${PROJECT_SOURCE_DIR}/include
)

add_executable(scheduler
    src/main.cpp
)

target_link_libraries(scheduler
    PRIVATE
        scheduler_lib
)

enable_testing()
