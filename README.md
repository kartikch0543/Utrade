# Concurrent Task Scheduler

Production-style concurrent task scheduler engine implemented in modern C++17 with CMake. It executes a directed acyclic graph of tasks using a priority-aware scheduler and a worker pool, while producing deterministic logs and a post-run execution summary.

## Design Decisions

### Why a priority queue?

The scheduler keeps all dependency-satisfied tasks in a priority queue so the highest-priority runnable work is always dispatched first. When priorities tie, the implementation falls back to insertion order and task ID, which keeps scheduling behavior deterministic and debuggable.

### Why Kahn's algorithm?

Kahn's algorithm is a natural fit for build-style DAG validation because it uses the same in-degree bookkeeping that the scheduler later relies on at runtime. That lets the code fail fast on cycles and reuse the graph representation for execution.

## Data Structures

### Graph representation

- `Graph` stores an adjacency list from each task to its dependents.
- It also stores an in-degree map for dependency counts, per-task durations for critical-path computation, and reverse dependency lists for DOT export and path analysis.

### Thread-safe work queue

- `WorkerPool` owns a queue of `std::function<void()>` jobs protected by `std::mutex` and coordinated with `std::condition_variable`.
- Workers sleep when no work is available and wake only when new jobs arrive or shutdown begins.

### Scheduler state

- `Scheduler` keeps an in-memory task table, remaining dependency counts, a deterministic ready queue, per-task attempt counters, and aggregate execution counters.
- Runtime state updates are guarded by a mutex so task completion, retries, and downstream cancellation stay consistent under concurrency.

## How to Run

### Build with CMake

```bash
cmake -S . -B build
cmake --build build
```

### Run the scheduler

```bash
./build/scheduler --config configs/sample_tasks.json --workers 4
```

### Export the graph to DOT

```bash
./build/scheduler --config configs/sample_tasks.json --workers 4 --dot build/tasks.dot
```

You can then render the DOT file with Graphviz:

```bash
dot -Tpng build/tasks.dot -o build/tasks.png
```

## Input Format

The scheduler expects a JSON object with a top-level `tasks` array. Each task supports:

- `id`: unique string identifier
- `name`: human-readable task name
- `priority`: higher values run first when dependencies are satisfied
- `dependencies`: list of task IDs that must complete before this task starts
- `duration`: simulated runtime in milliseconds
- `retry_count`: optional retry budget for failures
- `fail_probability`: optional value in `[0.0, 1.0]` for deterministic simulated failure

Example:

```json
{
  "tasks": [
    {
      "id": "compile",
      "name": "Compile project",
      "priority": 10,
      "dependencies": ["fetch"],
      "duration": 500,
      "retry_count": 1,
      "fail_probability": 0.1
    }
  ]
}
```

## Edge Cases Handled

- Circular dependencies, including deep cycles
- Self-dependency
- Missing dependency IDs
- Duplicate task IDs
- Empty files
- Invalid JSON
- Zero-task configurations
- Same-priority tasks
- `--workers 1`
- `--workers` greater than the number of tasks

## Trade-offs

- Execution is simulated with `sleep_for`, so there is no external process execution or persistence layer.
- Failure simulation is deterministic rather than truly random, which improves debuggability at the cost of realism.
- The scheduler cancels pending downstream work after an unrecoverable failure, but it does not preempt tasks that are already running.

## Future Improvements

- Add unit and integration tests under `tests/`
- Support real command execution instead of simulated durations
- Persist execution metadata for resumable runs
- Add resource constraints beyond worker-count concurrency
- Emit structured logs as JSON for machine ingestion

## Verification Notes

The project is intended to build with a modern CMake toolchain and standard library that supports `std::thread`. In the current local environment, `cmake` is not installed on `PATH`, and the bundled MinGW 6.3 runtime lacks usable standard threading support, so full local build verification requires a newer toolchain.
