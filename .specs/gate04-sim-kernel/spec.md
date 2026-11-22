# GATE 04 Specification: Generational Dense SoA Entity Kernel, Pointer-Free ASLR-Invariant SimState, Sorted Command Bus, 20 Hz TaskGraph Tick Loop & LZ4 Snapshot Save/Replay Engine

## 1. Scope & Architecture
- **Objective**: Implement `ron_sim` library containing ECS kernel for 32,768 entities, deterministic command queue, 20 Hz tick graph loop, and instant LZ4 snapshot save/load & replay engine.
- **Constraints**:
  - `-fno-exceptions -fno-rtti`
  - Zero `float` / `double` instructions in `src/sim/`. Fixed-point `fx32`, `fx64`, `Angle16` only.
  - Pointer-Free, ASLR-Invariant `SimState` POD: `match_arena.alloc<SimState>(1)`.
  - Zero Stale Tail Garbage: dense swap-and-pop explicitly clears `last` slot before decrementing `active_count`.
  - 100% bit-exact determinism across 1 worker and 8 workers.

## 2. Components
1. `src/sim/entity_handle.hpp`:
   - `MAX_ENTITIES = 32768`, `INVALID_DENSE_IDX = 0xFFFF'FFFFu`, `MAX_PLAYERS = 8`, `GAIA_PLAYER_ID = 8`, `SIM_TICKS_PER_SECOND = 20`.
   - `integrate_dt_20hz(fx32 rate_per_sec)`: odd-symmetric `(mag + 10) / 20`.
   - `EntityHandle`: 32-bit (20-bit slot + 12-bit generation). `NULL_ENTITY = EntityHandle::null()`.
   - `MovementDomain`, `OrderType`, `StanceType`, `EntityFlags`.
2. `src/sim/entity_soa.hpp` / `.cpp`:
   - `SpawnParams`.
   - `EntityStore`: Dense SoA + sparse generational table, `init()`, `is_alive()`, `resolve_dense()`, `spawn()`, `mark_for_destroy()`, `destroy_at_dense()`, `sweep_pending_destroys()`, `interpolate_pos()`, `interpolate_heading()`.
3. `src/sim/command_bus.hpp` / `.cpp`:
   - `SimCommand`: 32 bytes exact, trivially copyable, unique object representations.
   - `CommandBus`: pending buffer (8192 max), replay buffer (131072 max), `enqueue()`, `extract_sorted_for_tick()`.
   - Replay serialization: `ReplayHeader` (32B, magic `'RP26'`), `serialize_replay_lz4()`, `load_replay_lz4()`.
4. `src/sim/sim_world.hpp` / `.cpp`:
   - `SimState`: pointer-free root POD (magic `'SM26'`), tick counter, match seed, PRNG, totals, `EntityStore`.
   - `SimWorld`: 4-stage `TaskGraph`:
     - Stage 0 ("Sim_BeginTick"): copy pos/heading, tick down cooldowns, tick APS reload, tick anim.
     - Stage 1 ("Sim_CommandIngest"): deterministic sorted command application.
     - Stage 2 ("Sim_Kinematics"): orders movement, heading turn, position clamp.
     - Stage 3 ("Sim_TargetCleanupAndSweep"): target validity check, destroy sweep, advance tick & RNG.
   - `step_tick()`, `compute_state_hash()`, `save_snapshot_lz4()`, `load_snapshot_lz4()`.
5. `tests/test_gate04_sim_kernel.cpp`:
   - 7 automated tests covering generational ABA, 32,768 churn & zero tail garbage, command bus deterministic sort, 1w vs 8w 16,384 entity bit-exactness, mid-match snapshot save/load fork equality, replay desync verification, and render interpolation / < 1.5 ms performance budget.
