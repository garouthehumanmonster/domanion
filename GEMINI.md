# GEMINI.md — Master Engine Constitution: DOMINION 2026

## 1. Target Hardware & Toolchain
- **OS**: Windows 11 64-bit (`_WIN32`, `_WIN64`, `WIN32_LEAN_AND_MEAN`, `NOMINMAX`).
- **GPU**: NVIDIA GeForce RTX 3050 Laptop GPU (6,144 MiB GDDR6). GUI binaries MUST export `NvOptimusEnablement = 0x00000001` and `AmdPowerXpressRequestHighPerformance = 1`.
- **Compiler**: MinGW-W64 `g++` 15.2.0 (`-std=c++23 -march=x86-64-v2`), `cmake`, `ninja`.
- **Pipeline**: Python 3.13 (`python`), Node.js (`node`).

## 2. Global Mathematical & Memory Laws (NEVER VIOLATE)
1. **Strict Sim/Render Split**:
   - `src/core/` and `src/sim/` compile with `-fno-exceptions -fno-rtti`.
   - `float` and `double` are **BANNED** in `src/sim/`. Use `fx32` (`int32_t` Q16.16), `fx64` (`int64_t` Q32.32), and `Angle16` (`uint16_t` Binary Angular Measurement where `0..65535` maps `[0, 2π)` via exact unsigned modular wraparound).
   - World horizontal coordinates (`X`, `Z`) are strictly non-negative `[0.0m, 4096.0m)` so bitshift grid indexing never hits negative signed-division rounding bugs. Elevation `Y` is `[-128.0m, +512.0m]`.
2. **Deterministic Memory & Hashing**:
   - Zero `malloc`, `new`, `std::vector`, `std::unordered_map`, or heap allocations inside simulation ticks or render frames.
   - All simulation structs MUST be `trivially_copyable` with explicit zeroed padding (`std::has_unique_object_representations_v<T>` where integer-only) and allocated from zero-initialized `MemoryArena` blocks (`64-byte` cache-line aligned default) so `xxh64` full-arena state hashes are 100% bit-reproducible.
3. **Command Bus Exclusivity**:
   - Neither UI input nor AI ever mutates `SimWorld` directly; all actions emit POD `SimCommand` structs into the sorted deterministic tick queue (`20 Hz` = `50 ms/tick`).
4. **OpenGL 4.6 AZDO & VRAM Cap**:
   - Direct State Access (`glCreate*`, `glNamed*`, `glTextureStorage*`) + Persistent Mapped SSBOs + Compute Culling + `glMultiDrawElementsIndirect`.
   - Hard boot-time static VRAM budget: `<= 2,304 MiB`. Zero runtime GPU buffer/texture allocations after startup.
5. **Verification Mandate**:
   - Every Gate MUST compile with `0` errors and pass all automated tests in `ron_tests.exe` with exit code `0` before completion. Build + run tests after every file group, not only at the end. If a spec is provably wrong, record it in docs/spec_deviations.md instead of weakening tests.

tools/asset_common/ holds Python helpers shared by all asset bakers. Bakers never import from each other. Bake outputs live in assets/compiled and are git-ignored; assets/source holds committed inputs.

Texture bakes: tools/texture_baker writes assets/compiled/textures/textures_2026.tex26 (BC1/BC3/BC4/BC5/RGBA8, level-major then layer). Contracts are in docs/asset_contracts.md. Never change a BC format formula in only one language; Python and C++ must stay bit-identical.

Rendering: OpenGL 4.6 core via glad2 + SDL3 (static). Reverse-Z always (glClipControl LOWER_LEFT/ZERO_TO_ONE, D32F, GL_GREATER, clear 0.0). GL calls only in src/render and tests/render; DSA only; no glGetError outside tests. Every GPU allocation goes through VramLedger before it freezes after init (2304 MiB cap); no runtime GPU allocation, no resolution change without restart. Binding indices exist only in src/render/gl_bindings.hpp and are injected into shaders. GUI executables must invoke RON_EXPORT_GPU_PREFERENCE(). Contracts: docs/render_contracts.md.

Terrain: heights are -128 + 640*code/65535 m, sea level exactly 0 m; height texels are centred at (i+0.5)*cell. CDLOD: 8 levels, 32 m leaves, range[L] = 96*2^L m, one 19x19 skirted grid mesh; node selection lives in terrain_quadtree.cpp and its tests must keep passing. Camera: yaw 0 looks north (-Z), reverse-Z, dist 30..1500 m. Ocean is a flat y=0 plane drawn after terrain with depth write off. Only terrain_source.cpp and entity_source.cpp may include theatre_data.hpp/texture_data.hpp/model_data.hpp from src/render.


World overlays: territory owner 512² (8 m cells) -> JFA SDF 1024² (seed points in half-texel units, RG16F) -> borders. Craters are cosmetic RGBA8 2048²: R=(code-128)*0.0625 m offset, G=scorch; the sim never reads them. Fog is 512² RG16F (x visible, y explored) fed by FogMap. Overlay shader code lives behind RON_OVERLAYS and is only compiled via the TerrainOvl/OceanOvl programs. Overlay data is render-side only.

Entities: render-side EntityInstance (64 B) -> compute cull (frustum + projected-size, LOD by pixel radius, bucket = model*3+lod) -> prefix scan builds DrawElementsIndirect commands -> scatter writes the visible list -> ONE glMultiDrawElementsIndirect. Vertex shader reads visible[gl_BaseInstance+gl_InstanceID]; VAT frames give per-part 3x4 matrices. Interpolation prev->curr uses alpha in EntityUbo. Entities are drawn after terrain and BEFORE the ocean. The sim never reads entity render data. Contracts are in docs/render_contracts.md and docs/gate12_contract_notes.md.

