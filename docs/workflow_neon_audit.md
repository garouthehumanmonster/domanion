# Principal Systems Architecture Review — DOMINION 2026 Workflow Audit

**Model:** Gemini 4 Neon (`Thinking Level: High`)  
**Scope:** End-to-End 30-Gate Multi-Agent Workflow (Neon + Coordinator + Flash), Transitioning Phase III (AZDO Renderer) -> Phase IV–VII (Simulation, Gameplay, AI, Release)

---

## 1. End-to-End Workflow Ratings (1–10)

| Dimension | Score | Principal Architect Assessment |
|---|---|---|
| Scalability across 30+ Gates | **8.5 / 10** | Strict phase boundaries, small-batch groups (A/B/C), and `contract_notes.md` prevent context collapse (~15k LOC). Docked (-1.5) because from Gate 15 onward state coupling scales superlinearly (`Nav <-> Economy <-> Combat <-> Snapshot Bridge <-> UI`) and needs phase-level soak tests plus a live architectural manifest. |
| Token Efficiency & Division of Labor | **9.5 / 10** | Strong cognitive tiering (proofs/audits in Neon High, bounded specs in Coordinator, small implementation groups in Flash, terse failure reporting). Docked (-0.5) because Neon -> Coordinator -> Flash handoff is prose rather than typed schema. |
| Failure Resilience & Guardrails | **8.0 / 10** | Reading real signatures first (`contract_notes.md`), banning test weakening, and requiring Debug + Release builds eliminate ~85% of LLM hallucinations. Docked (-2.0) for potential hidden heap allocs in `src/sim`, float/double bleed, non-deterministic tie-breaking, or signed integer overflow UB passing Debug and failing under GCC 15.2 `-O3`. |
| Final Engine Performance (60–144 FPS RTX 3050 6 GB) | **8.5 / 10** | AZDO + compute culling + single MDI + Reverse-Z + frozen 2,304 MiB VRAM cap fits Ampere's 96-bit GDDR6 bus (~144–168 GB/s). Docked (-1.5) for dynamic per-frame SSBO uploads in Phase IV–VI, fog updates, UI overdraw, and main-thread sim spikes >3–4 ms hurting 144 Hz (6.94 ms/frame) 1% lows. |

---

## 2. Top 3 Hidden Vulnerabilities & Friction Points

### Vulnerability 1: The "Telephone Game" Spec Drift + C++23 Error Truncation
- **Failure Mode:** Neon audits Gate NN without seeing exact live header signatures, `fx32` overloads, or live `VramLedger` totals; Flash hits minor mismatches and improvises casts or packing.
- **Friction Multiplier:** Reporting only the last 30 lines of build output works for runtime test failures, but hides GCC 15.2 C++23 template/concept/linker root causes located 80–150 lines above trailing instantiation noise.

### Vulnerability 2: Silent Constitutional Decay Inside `src/sim`
- **Failure Mode:** Phases IV–V (Flowfields, A*, ORCA steering, ballistics, economy graphs) invite reflexive `std::vector`, `std::unordered_map`, non-deterministic sorts, `<cmath>`, or un-widened `fx32 * fx32`.
- **Why Lethal:** Signed integer overflow UB in C++23 passes Debug `-O0` and diverges under GCC 15.2 `-O3` in Release replays.

### Vulnerability 3: Sim-to-Render Coupling & Frame-Pacing Cliff
- **Direct Coupling:** Allowing `src/render` to include `src/sim` internals risks cache-locality destruction and determinism leaks.
- **Frame-Pacing Spikes & Stalls:** Synchronous slow sim ticks or non-triple-buffered persistent SSBO writes cause 1% low micro-stutters or NVIDIA pipeline stalls.

---

## 3. Optimizing the Neon <-> Coordinator <-> Flash Interface

1. **Feed Ground Truth to Neon (`tools/gen_manifest.py`):** Generate compact `docs/ARCH_MANIFEST.md` (~3–5K tokens) with exact `src/core` APIs, `SimWorld`/bridge struct layouts, `VramLedger` totals, and CMake target graph before every Gate design.
2. **Enforce Standardized 6-Block Gate Blueprint Contract (Neon -> Coordinator):** Invariants & Math Spec -> Exact C++23 Contracts -> Hostile Edge Cases -> Golden Numerical Vectors -> Group Partitioning (`A -> D`) -> Explicit Negative Constraints.
3. **Authoritative Runner (`tools/gate_check.py`):** Executes in order: `Static Sim Lint -> Debug Build -> Debug Tests -> Release Build -> Release Tests -> Debug/Release Hash Parity`, emitting a deterministic 25-line Failure Capsule (first `error:` + context, or failing test + `file:line`).

---

## 4. Architectural Precautions: Transitioning Phase III -> Phase IV

1. **Audit Q16.16 (`fx32`) Overflow Ceilings Immediately:** Widen squared distances, dot/cross products, and areas to `int64_t` / `fx64` before `isqrt` back to `fx32`; zero `<cmath>` in `src/sim`; compile `src/sim` with `-fno-strict-aliasing -fwrapv`.
2. **Erect Compile-Time & Runtime Firewalls Around `src/sim`:** Add `-Werror=double-promotion -Werror=float-conversion` on `sim_kernel` + static regex check in `tools/gate_check.py` banning `float`, `double`, `<cmath>`, `std::unordered_map`, `std::map`, `std::set`, `std::vector` dynamic allocation, `rand()`, `std::chrono`; wrap `operator new`/`operator new[]` in `ron_tests` to abort if dynamic allocations occur during `SimWorld::step_tick()`.
3. **Pre-Allocate All Pathfinding & Spatial Structures (SoA + Generation Counters):** Allocate fixed flowfield LRU pools (32–64 slots), O(1)-reset generation-stamped spatial hash grids, and hot cache-aligned nav SoA arrays separate from cold economy metadata.

> **Capture Note:** Original AI Studio capture truncated cleanly after Precautions Item 3 (`separate from cold economy/metadata st`). Items 4–6 were re-requested in the subsequent grounded repository review.
