#pragma once

#include "types.hpp"
#include "result.hpp"
#include "arena.hpp"
#include "job_system.hpp"

namespace ron {

class Profiler;

inline constexpr u32 MAX_TASK_NODES = 32;
inline constexpr u32 MAX_TASK_DEPENDENTS = 16;

using TaskId = u16;
inline constexpr TaskId INVALID_TASK_ID = 0xFFFF;

using TaskNodeFn = void(*)(void* user_ctx, JobSystem& js, MemoryArena& scratch) noexcept;

class TaskGraph {
public:
    TaskGraph() noexcept = default;

    void reset() noexcept;
    Result<TaskId> add_node(const char* debug_name, void* user_ctx, TaskNodeFn fn) noexcept;
    Result<void> add_dependency(TaskId prerequisite, TaskId dependent) noexcept;
    Result<void> compile() noexcept;
    void execute(JobSystem& js, Profiler* profiler = nullptr) noexcept;

    u32 node_count() const noexcept { return node_count_; }
    bool is_compiled() const noexcept { return compiled_; }

private:
    struct TaskNode {
        const char* debug_name{"unnamed"};
        void* user_ctx{nullptr};
        TaskNodeFn fn{nullptr};
        TaskId dependents[MAX_TASK_DEPENDENTS]{};
        u16 dependent_count{0};
        u16 in_degree{0};
    };

    TaskNode nodes_[MAX_TASK_NODES]{};
    u16 base_in_degree_[MAX_TASK_NODES]{};
    TaskId root_nodes_[MAX_TASK_NODES]{};
    u32 node_count_{0};
    u32 root_count_{0};
    bool compiled_{false};
};

} // namespace ron
