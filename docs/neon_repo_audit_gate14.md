# Gemini 4 Neon — Live Repository Verification & Audit (Commit `cf4cdb95`)

**Reviewer:** Gemini 4 Neon (`Thinking Level: High`, Grounded against `https://github.com/garouthehumanmonster/domanion`)  
**Sources Verified:** `README/repo root`, `GEMINI.md`, `CMakeLists.txt`, `docs/render_contracts.md`, `docs/spec_deviations.md`, `docs/gate13_contract_notes.md`, `docs/gate14_neon_review.md`, `docs/workflow_neon_audit.md`, `src/core/fixed_math.hpp`, `src/sim/sim_world.hpp`, `src/sim/command_bus.hpp`, `src/render/gl_bindings.hpp`, `src/render/vram_ledger.hpp`, `src/render/lighting_system.hpp`

---

## 1. Repo & Constitution Verification

| Subsystem | Score | Principal Architect Verdict |
|---|---|---|
| Core / Sim Kernel (`fixed_math.hpp`, `sim_world.hpp`, `command_bus.hpp`) | **7.5 / 10** | Strong POD discipline (`SimCommand` & `ReplayHeader` 32 B with `has_unique_object_representations_v`), saturating `fx32` ops, zero-`<cmath>` compile-time LUTs. Docked for `u64` overflow -> `SIGFPE` in `isqrt_u128`, `INT64_MIN` negation UB in `fx64`, `turn_towards_bam` wrap bug, and missing `has_unique_object_representations_v<SimState>`. |
| AZDO Render Architecture (`gl_bindings.hpp`, `vram_ledger.hpp`, `lighting_system.hpp`, `render_contracts.md`) | **8.0 / 10** | Clean Reverse-Z `ZERO_TO_ONE`, single X-macro `RON_GL_BINDINGS`, coherent triple-buffered persistent ring, frozen `VramLedger`. Docked for `LightingSystem` Rule-of-Five double-destroy bug, monotonically growing `TEX_*` / `IMG_*` bindings nearing Ampere per-stage limits (32 samplers / 8 images), and unmonitored raw GL handles. |
| CMake & Flag Discipline (`CMakeLists.txt`) | **6.5 / 10** | Good warning hygiene and `-fno-exceptions -fno-rtti` on `ron_core`/`ron_sim`. Docked for copy-paste linker bug stripping `-static` + 16 MiB stack from `ron_view`, `ron_render` linking `ron_sim PUBLIC` via `theatre_data.cpp`, missing `-fwrapv`/`-Werror=float-conversion` on `ron_sim`, and inconsistent `-fno-strict-aliasing`. |
| Contract Integrity & Repo Hygiene (`GEMINI.md`, `docs/*`) | **6.0 / 10** | `spec_deviations.md` and `gate13_contract_notes.md` show strong engineering honesty. Docked because `docs/render_contracts.md` and `GEMINI.md` stopped at Gate 12, `gate14_neon_review.md` & `workflow_neon_audit.md` held raw browser-HUD dumps, and Gates 01–13 lived in 1 squashed commit. |

### Key Bugs Flagged
- **`src/core/fixed_math.hpp`:**
  1. `isqrt_u128(u128)` declared `constexpr` yet calls non-`constexpr` inline `asm("divq %4")` in `div128_by_64`; large inputs `>= 2^126` overflow `u64` in `(x0 + div128_by_64(n, x0)) >> 1` causing `SIGFPE`.
  2. Signed negation `-raw` / `-x_cos` UB on `INT32_MIN` / `INT64_MIN` across `fx32`, `fx64`, and `atan2_bam`.
  3. `turn_towards_bam` casts `u16 max_step_bam` to `i16`, breaking steps `>= 32768`.
  4. `fx32::to_render_f32()` unguarded in `fixed_math.hpp`; missing `#include <bit>`.
- **`src/sim/sim_world.hpp` & `src/sim/command_bus.hpp`:**
  1. `SimState` (and `EntityStore`) missing `std::has_unique_object_representations_v<...>`.
  2. `CommandBus` lives outside `SimState`, risking stale pending/history state across `load_snapshot_lz4()`.
  3. `MAX_REPLAY_COMMANDS = 131072` saturation cliff if 200-unit selections emit 200 separate `SimCommand`s.
- **`src/render/lighting_system.hpp`:**
  1. Defaulted `LightingSystem(LightingSystem&&)` / `operator=` copies raw `shadow_fbo_[4]` and `height_linear_sampler_` without zeroing source -> double-destroy / leak.
  2. `render_shadows(...)` missing `noexcept`; `destroy(VramLedger&)` ignores parameter.
- **`CMakeLists.txt`:**
  1. `ron_view` MinGW block sets link options on `ron_render_tests` by copy-paste mistake.
  2. `ron_render` links `ron_sim PUBLIC` because `theatre_data.cpp` resides in `ron_sim`.
  3. `ron_sim` lacks `-fwrapv -Werror=double-promotion -Werror=float-conversion -Werror=switch -DRON_SIM_NO_FLOAT=1`; `ron_tests` / `ron_render_tests` / `ron_view` omit `-fno-strict-aliasing`.

---

## 2. Gate 14 Integration Readiness

- **Next Free Slots in `gl_bindings.hpp`:** UBO `7..8`, SSBO `15..18`, TEX `22..25` (26/32 used), IMG `5` (6/8 used; alias `0`/`1` inside bloom compute or document post-Phase III aliasing).
- **Exact Gate 14 VRAM (`MiB`, `VramCat`):**
  - `TEX_DEPTH_COPY` (`D32F`): `7.91 MiB` @ 1080p (`VramCat::DepthTargets`)
  - Bloom `R11G11B10F` 5–6 mips: `~2.64 MiB` @ 1080p (`VramCat::PostFx`)
  - 64×64 `RG8` Blue Noise: `8 KiB` (`VramCat::PostFx`)
  - Particle Pool SSBO (`65536 * 48 B`): `3.00 MiB` + `AliveIndexSSBO`: `0.25 MiB` + Indirect/Counter `< 4 KiB` (`VramCat::EntityBuffers`)
  - Static Quad IBO (`65536 * 6 * 4 B`): `1.50 MiB` (`VramCat::ModelGeometry`)
  - VFX Atlas (`textures_2026.tex26`): `VramCat::VfxAtlas`
- **Explicit Conflicts to Close Before Gate 14 Lock:**
  1. `Present_Tonemap` (`docs/render_contracts.md` Test 12) tests Reinhard; isolate `TonemapAces` or migrate Test 12 intentionally while keeping `TONEMAP_PASSTHROUGH=1` pure.
  2. Ocean writes no depth; naval soft particles need analytical `y = 0` plane distance if fading against water rather than seabed.
  3. Audit `RON_ENTITY_ALBEDO_DECODE = 0` linearity under ACES steeper curve.
  4. Verify `#extension GL_KHR_shader_subgroup_ballot` preprocessing order with scalar `atomicAdd` fallback.
  5. Backfill Gate 13 into `docs/render_contracts.md` and `GEMINI.md`.

---

## 3. Workflow Rating & Recommended Checklist

- **Workflow Score:** **7.8 / 10**
- **Stages Ordered:**
  - **Stage A (Pre-Gate 14 Hotfixes):** Fix `CMakeLists.txt` `ron_view` link + `LightingSystem` move ops + backfill Gate 13 docs + clean Neon review docs + tag `gate-13-lock`.
  - **Stage B (Gate 14 Execution):** Verify `TexFmt::R11F_G11F_B10F` + context binding assertions + Gate 14 Neon 4 fixes + 5 verification tests + tag `phase-3-lock`.
  - **Stage C (Phase IV Firewall):** Fix `fixed_math.hpp`, harden `SimState`/`CommandBus`, move `theatre_data.cpp` -> `src/assets`, enforce `ron_sim` flags, deploy `tools/gate_check.py`, `tools/gen_manifest.py`, and `operator new` tick trap, tag `phase-4-ready`.
