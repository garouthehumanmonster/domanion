#include "task_graph.hpp"
#include "profiler.hpp"

namespace ron {

void TaskGraph::reset() noexcept {
    node_count_ = 0;
    root_count_ = 0;
    compiled_ = false;
    for (u32 i = 0; i < MAX_TASK_NODES; ++i) {
        nodes_[i] = TaskNode{};
        base_in_degree_[i] = 0;
        root_nodes_[i] = 0;
    }
}

Result<TaskId> TaskGraph::add_node(const char* debug_name, void* user_ctx, TaskNodeFn fn) noexcept {
    if (fn == nullptr) {
        return Result<TaskId>::err(ErrorCode::InvalidArgument);
    }
    if (node_count_ >= MAX_TASK_NODES) {
        return Result<TaskId>::err(ErrorCode::OutOfMemory);
    }

    const TaskId id = static_cast<TaskId>(node_count_++);
    TaskNode& n = nodes_[id];
    n.debug_name = (debug_name != nullptr) ? debug_name : "unnamed";
    n.user_ctx = user_ctx;
    n.fn = fn;
    n.dependent_count = 0;
    n.in_degree = 0;
    compiled_ = false;

    return Result<TaskId>::ok(id);
}

Result<void> TaskGraph::add_dependency(TaskId prerequisite, TaskId dependent) noexcept {
    if (prerequisite >= node_count_ || dependent >= node_count_ || prerequisite == dependent) {
        return Result<void>::err(ErrorCode::InvalidArgument);
    }

    TaskNode& from = nodes_[prerequisite];
    for (u32 i = 0; i < from.dependent_count; ++i) {
        if (from.dependents[i] == dependent) {
            return Result<void>::ok(); // Ignore duplicate edge
        }
    }

    if (from.dependent_count >= MAX_TASK_DEPENDENTS) {
        return Result<void>::err(ErrorCode::OutOfMemory);
    }

    from.dependents[from.dependent_count++] = dependent;
    nodes_[dependent].in_degree++;
    compiled_ = false;

    return Result<void>::ok();
}

Result<void> TaskGraph::compile() noexcept {
    u16 in_deg[MAX_TASK_NODES]{};
    TaskId queue[MAX_TASK_NODES]{};
    u32 head = 0;
    u32 tail = 0;
    root_count_ = 0;

    for (u32 i = 0; i < node_count_; ++i) {
        in_deg[i] = nodes_[i].in_degree;
        base_in_degree_[i] = nodes_[i].in_degree;
        if (in_deg[i] == 0) {
            queue[tail++] = static_cast<TaskId>(i);
            root_nodes_[root_count_++] = static_cast<TaskId>(i);
        }
    }

    u32 visited_count = 0;
    while (head < tail) {
        const TaskId u = queue[head++];
        visited_count++;

        const TaskNode& node = nodes_[u];
        for (u32 d = 0; d < node.dependent_count; ++d) {
            const TaskId v = node.dependents[d];
            if (--in_deg[v] == 0) {
                queue[tail++] = v;
            }
        }
    }

    if (visited_count != node_count_) {
        compiled_ = false;
        return Result<void>::err(ErrorCode::InvalidArgument); // Cycle detected
    }

    compiled_ = true;
    return Result<void>::ok();
}

void TaskGraph::execute(JobSystem& js, Profiler* profiler) noexcept {
    RON_ALWAYS_ASSERT(compiled_, "TaskGraph::compile() must succeed before execute()");
    if (node_count_ == 0) return;

    struct Context {
        TaskGraph* graph;
        JobSystem* js;
        Profiler* profiler;
        JobCounter* graph_done;
        std::atomic<i32>* rem_deps;
    };

    RON_CACHE_ALIGN std::atomic<i32> rem_deps[MAX_TASK_NODES];
    for (u32 i = 0; i < node_count_; ++i) {
        rem_deps[i].store(base_in_degree_[i], std::memory_order_relaxed);
    }

    JobCounter graph_done{static_cast<i32>(node_count_)};

    Context ctx{
        .graph = this,
        .js = &js,
        .profiler = profiler,
        .graph_done = &graph_done,
        .rem_deps = rem_deps
    };

    struct NodeRunner {
        Context* ctx;
        TaskId node_id;

        void operator()() const noexcept {
            const TaskNode& node = ctx->graph->nodes_[node_id];

            MemoryArena& scratch = ctx->js->current_scratch_arena();
            {
                ArenaTempScope scope(scratch);
                u32 prof_slot = 0xFFFF'FFFF;
                if (ctx->profiler != nullptr) {
                    prof_slot = ctx->profiler->push_scope(node.debug_name, fnv1a_64(node.debug_name));
                }

                node.fn(node.user_ctx, *ctx->js, scratch);

                if (ctx->profiler != nullptr && prof_slot != 0xFFFF'FFFF) {
                    ctx->profiler->pop_scope(prof_slot);
                }
            }

            // Submits unlocked dependents
            for (u32 d = 0; d < node.dependent_count; ++d) {
                const TaskId dep = node.dependents[d];
                if (ctx->rem_deps[dep].fetch_sub(1, std::memory_order_acq_rel) == 1) {
                    ctx->js->submit(nullptr, NodeRunner{ctx, dep});
                }
            }

            // Decrements graph_done strictly AFTER submitting unlocked dependents
            ctx->js->decrement_counter(*ctx->graph_done);
        }
    };
    static_assert(sizeof(NodeRunner) <= JOB_PAYLOAD_BYTES);

    // Submit all root nodes
    for (u32 r = 0; r < root_count_; ++r) {
        js.submit(nullptr, NodeRunner{&ctx, root_nodes_[r]});
    }

    js.wait(graph_done);
}

} // namespace ron
