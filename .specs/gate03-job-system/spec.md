# GATE 03 Specification: Work-Stealing Multi-Core Job DAG, Cache-Line Isolated Queues, Per-Thread Scratch Arenas & Nanosecond Profiler

## 1. Scope & Architecture
- **Objective**: Deterministic multi-core execution engine for 32,768-entity simulation ticks across 1..16 threads.
- **Components**:
  - `src/core/job_system.hpp`, `src/core/job_system.cpp`: Cache-line isolated work-stealing job queues, Win32 `_beginthreadex`, `SRWLOCK`, `CONDITION_VARIABLE`, `JobCounter`, `Job`, `parallel_for_deterministic`, `parallel_reduce_deterministic`.
  - `src/core/task_graph.hpp`, `src/core/task_graph.cpp`: Fixed-capacity 32-node tick DAG, Kahn topological sort compile, atomic dependency tracking, root node dispatch.
  - `src/core/profiler.hpp`, `src/core/profiler.cpp`: Lock-free per-worker telemetry streams (512 events/worker), 64-frame rolling stats, zero allocations.
  - `tests/test_gate03_jobs.cpp`: 7 automated tests proving single-worker fallback, work-stealing + queue overflow resilience, 1w vs 8w 40-tick bit-exact determinism, non-commutative reduction invariance, 7-stage DAG ordering, cycle rejection, and telemetry verification.

## 2. Invariants & Determinism Laws
- **Law A (Chunk Identity)**: `parallel_for_deterministic` passes `(begin, end, chunk_id, scratch)` to callbacks, never thread ID.
- **Law B (Ascending Reduction)**: Per-chunk partials written in parallel, folded sequentially `chunk_id = 0 .. chunk_count - 1`.
- **Law C (LIFO ArenaTempScope)**: Every job execution wraps `scratch` with `ArenaTempScope`.
- **Zero Heap Allocations**: No `malloc`, `new`, `std::vector`, `std::function`.
- **Win32 Direct**: Native `_beginthreadex`, `SRWLOCK`, `CONDITION_VARIABLE` via `<windows.h>` / `kernel32`.

## 3. Structural Definitions
- `JobCounter`: 64 bytes (`RON_CACHE_ALIGN`, `std::atomic<i32> pending`).
- `Job`: 64 bytes (`JobEntryFn`, `JobCounter*`, `u8 payload[48]`).
- `WorkStealingDeque`: 1024-entry ring, owner LIFO at bottom, thieves FIFO at top via `TryAcquireSRWLockExclusive`.
- `TaskGraph`: 32 nodes max, 16 dependents/node, stack-allocated dependency resolution.
- `Profiler`: Per-worker stream, no cross-thread locks or atomics on capture, aggregated at `end_frame()`.
