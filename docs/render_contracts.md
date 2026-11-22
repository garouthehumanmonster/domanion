# DOMINION 2026 — Rendering Foundation Contracts

## 1. Reverse-Z, Clip Control & Depth Format
| Property | Value | Rationale |
|---|---|---|
| Depth Buffer Format | `TexFmt::D32F` (`GL_DEPTH_COMPONENT32F`) | 32-bit floating point depth precision. |
| Clip Control | `glClipControl(GL_LOWER_LEFT, GL_ZERO_TO_ONE)` | Maps NDC Z to [0, 1] instead of [-1, 1]. |
| Depth Comparison | `glDepthFunc(GL_GREATER)` | Reverse-Z: 1.0 = near plane, 0.0 = far plane. |
| Clear Depth | `0.0f` | Explicit clear via `glClearNamedFramebufferfv`. |
| Precision Distribution | Uniform across distance in 1/z | Eliminates Z-fighting across massive 4096m RTS terrain. |

## 2. Context & Window Configuration
| Setting | Specification | Notes |
|---|---|---|
| API Version | OpenGL 4.6 Core Profile | Fixed requirement, no fallback. |
| Window Manager | SDL3 (static build) | Low overhead, zero dynlib deployment deps. |
| Debug Context | Enabled in Debug (`RON_DEBUG=1`), disabled in Release | Synchronous debug output hooked to engine logger. |
| VSync Control | Adaptive (-1), VSync on (1), Immediate (0) | Configurable via CLI `--vsync`. |
| VAO Binding | Single global empty VAO created at boot | Core profile requires a VAO bound for `glDrawArrays`. |
| Seamless Cubemaps | `GL_TEXTURE_CUBE_MAP_SEAMLESS` enabled | Seamless filtering across cube faces. |
| Framebuffer sRGB | `GL_FRAMEBUFFER_SRGB` disabled | Manual exact sRGB encoding in tonemapping pass. |

## 3. VRAM Ledger Categories & Static Caps (Hard Budget: 2,304 MiB)
| Category | Cap (MiB) | Description |
|---|---|---|
| `TrimTextures` | 64 | Trim sheet atlases and decals. |
| `TerrainTextures` | 64 | Terrain macro and detail tile textures. |
| `VfxAtlas` | 32 | Particle and VFX flipbook textures. |
| `UiAtlas` | 16 | MSDF glyphs and UI icons. |
| `ModelGeometry` | 128 | Static mesh and kitbash vertex/index buffers. |
| `VatTextures` | 32 | Vertex Animation Texture positional/normal data. |
| `EntityBuffers` | 192 | Instance transform matrices and visible entity SSBOs. |
| `TerrainMaps` | 192 | CDLOD heightmaps, splatmaps, normal maps. |
| `Ocean` | 64 | FFT wave displacement and spectrum maps. |
| `ShadowMaps` | 320 | Cascaded shadow map texture arrays. |
| `HdrTargets` | 512 | Offscreen HDR color render targets (RGBA16F). |
| `PostFx` | 256 | Intermediate post-processing and bloom ping-pong targets. |
| `DepthTargets` | 128 | Depth render targets (D32F). |
| `Staging` | 64 | Persistent mapped uniform/staging rings. |
| `Reserve` | 240 | Emergency allocation headroom. |
| **TOTAL** | **2304** | Hard engine-wide boot-time budget. |

*Size Calculation Rule*: Every allocation must be registered via `VramLedger::reserve()` before `freeze()`. Runtime allocations post-startup are strictly prohibited.

## 4. Pipeline Bindings (gl_bindings.hpp)
| Symbol | Type | Value | Target |
|---|---|---|---|
| `BIND_UBO_FRAME` | UBO | 0 | `FrameUniforms` struct block |
| `BIND_SSBO_INSTANCES` | SSBO | 0 | Entity instance transforms |
| `BIND_SSBO_VISIBLE` | SSBO | 1 | Entity visibility lists |
| `BIND_SSBO_INDIRECT` | SSBO | 2 | Indirect draw commands |
| `BIND_SSBO_COUNTERS` | SSBO | 3 | Atomic counters and draw metrics |
| `BIND_SSBO_TEST_IN` | SSBO | 6 | Unit test read buffer |
| `BIND_SSBO_TEST_OUT` | SSBO | 7 | Unit test write buffer |
| `TEX_HDR` | Texture | 0 | Scene HDR color input to tonemapper |
| `RON_WORLD_SIZE` | Constant | 4096.0 | World extent in meters |
| `RON_REVERSE_Z` | Constant | 1 | Reverse-Z active flag |

## 5. FrameUniforms Layout (std140, 336 Bytes)
| Member | Type | Offset (Bytes) | Size (Bytes) | Description |
|---|---|---|---|---|
| `u_view` | `mat4` | 0 | 64 | Column-major view matrix |
| `u_proj` | `mat4` | 64 | 64 | Column-major projection matrix |
| `u_view_proj` | `mat4` | 128 | 64 | Combined view-projection matrix |
| `u_inv_view_proj` | `mat4` | 192 | 64 | Inverse view-projection matrix |
| `u_cam_pos` | `vec4` | 256 | 16 | Camera world position (xyz), w unused |
| `u_screen` | `vec4` | 272 | 16 | x=w, y=h, z=1/w, w=1/h |
| `u_time` | `vec4` | 288 | 16 | x=seconds, y=sim_tick, z=interp_alpha, w unused |
| `u_sun_dir` | `vec4` | 304 | 16 | Sun direction vector (xyz), w=intensity |
| `u_sun_color` | `vec4` | 320 | 16 | Sun RGB radiance, w unused |

## 6. PersistentRing Semantics
- **Slot Count**: Exactly 3 slots (triple buffering).
- **Stride Alignment**: `slot_stride = round_up(bytes_per_frame, max(ssbo_align, ubo_align, 256))`.
- **Mapping Flags**: `GL_MAP_WRITE_BIT | GL_MAP_PERSISTENT_BIT | GL_MAP_COHERENT_BIT`.
- **Synchronization**: `glFenceSync(GL_SYNC_GPU_COMMANDS_COMPLETE, 0)` on `end_frame()`; `glClientWaitSync(GL_SYNC_FLUSH_COMMANDS_BIT, 1s)` on `begin_frame()` when cycling into previous fence.

## 7. Shader Preprocessor & Program Library
- **Rules**:
  1. Root file line 1 must match `#version 460 core`.
  2. Preprocessor injects all `BINDING_DEFINES`, followed by user defines, followed by `#line 2 0`.
  3. Include lines (`#include "..."`) resolved relative to shader root directory.
  4. Implicit pragma-once: each unique file included at most once.
  5. Include nesting depth limit: max 8 levels.
- **Programs (`ProgramId`)**:
  - `Tonemap`: `fullscreen.vert` + `tonemap.frag` (Reinhard tonemapping + exact sRGB).
  - `TonemapPassthrough`: `fullscreen.vert` + `tonemap.frag` with `TONEMAP_PASSTHROUGH=1`.
  - `SelfTestRing`: `selftest_ring.comp` compute pipeline test.
  - `SelfTestFrame`: `selftest_frame.comp` uniform block round-trip test.
  - `DepthProbe`: `depth_probe.vert` + `depth_probe.frag` for depth testing.
  - `Pattern`: `fullscreen.vert` + `pattern.frag` for procedural test scene.

## 8. Test Suite (`ron_render_tests`)
1. `Gl_Context46`: Validates core profile 4.6 context, debug flags, capabilities, and dimensions.
2. `Gl_DebugCallbackWired`: Validates synchronous debug callback and deliberate GL error handling.
3. `Vram_Budget`: Verifies hard budget cap, category constraints, and freeze enforcement.
4. `Vram_TexBytes`: Validates texture mip sizing calculations across all texture formats.
5. `Vram_RealTex26`: Confirms bit-exact byte layout against Gate 08 `textures_2026.tex26`.
6. `Gpu_ObjectsLedger`: Validates RAII GPU buffer and texture lifecycle within VRAM ledger.
7. `Shader_Preproc`: Checks preprocessor define injection, recursive includes, pragma-once, line markers, and error reporting.
8. `Shader_Programs`: Verifies compilation, linking, and fallback safety of all shaders.
9. `Frame_UboLayout`: Validates bit-exact std140 layout and memory barrier execution of `FrameUniforms`.
10. `Ring_Persistent`: Validates triple-buffered coherent persistent memory ring and GPU synchronization.
11. `Depth_ReverseZ`: Validates reverse-Z depth testing (0.5 vs 0.25 vs 0.75) and D32F precision.
12. `Present_Tonemap`: Validates HDR Reinhard tonemapping and exact sRGB transfer function.
13. `GpuTimer_Basic`: Validates nanosecond-resolution timestamp query rings across multiple draw passes.

## 9. GPU Preference Export
All graphical executables (`ron_view`, etc.) MUST call `RON_EXPORT_GPU_PREFERENCE()` at file scope to export `NvOptimusEnablement = 0x00000001` and `AmdPowerXpressRequestHighPerformance = 1` for automatic discrete GPU selection on dual-GPU laptops.

## 10. Height & Sample Convention
- **Height Dequantization Formula**: `h = -128.0f + 640.0f * (float(code) / 65535.0f)` meters.
- **Sea Level**: Exactly `0.0f` meters.
- **Coordinate Space**: World horizontal coordinates `X, Z in [0.0m, 4096.0m)`. Elevation `Y in [-128.0m, +512.0m]`.
- **Texel Centring (Render vs Sim)**:
  - Render texture samples are centred at `((i + 0.5) * cell_size, (j + 0.5) * cell_size)`.
  - UV coordinates map `[0.0, 1.0]` across `[0.0m, 4096.0m]`.
  - Simulation height grids evaluate at discrete cell indices without the half-texel offset.

## 11. RTS Camera Model
- **Distance Range**: `CAM_DIST_MIN = 30.0f` m, `CAM_DIST_MAX = 1500.0f` m.
- **Dynamic Pitch**: Linearly blended with distance: `CAM_PITCH_NEAR_DEG = 28.0f` degrees (steep near) to `CAM_PITCH_FAR_DEG = 62.0f` degrees (flatter overview far).
- **Z-Planes**: `CAM_NEAR = 1.0f` m, `CAM_FAR = 16000.0f` m.
- **Field of View**: `CAM_FOV_Y_DEG = 45.0f` degrees.
- **Orientation**: Yaw `0.0f` looks North (`-Z`), positive yaw rotates clockwise (East is `+X`).
- **Ground Tracking**: Target follows ground elevation via bilinear height sampling.
- **Reverse-Z Projection**:
  - `m[10] = -n / (f - n)`, `m[14] = (n * f) / (f - n)`.
  - Depth buffer maps near plane to `1.0f` and far plane to `0.0f`.

## 12. CDLOD Terrain System Specification
- **LOD Levels**: 8 levels (`L = 0` leaf 32m to `L = 7` root 4096m). Total quadtree nodes = 21,845.
- **LOD Ranges**: `range[L] = 96.0f * (1 << L)` meters (`[96m, 192m, 384m, 768m, 1536m, 3072m, 6144m, 12288m]`).
- **Grid Mesh**: Single shared skirted grid mesh of dimension 19x19 vertices (648 triangles, 1944 indices).
- **Skirt Depth**: 10.0 meters downward along node perimeter to prevent cracks between mismatched LODs.
- **Morphing Function**: Continuous morph parameter `k = clamp((dist - (range - morph_range)) / morph_range, 0.0, 1.0)` smoothly shifts odd grid vertices to parent positions to eliminate LOD popping.
- **Selection**: Hierarchical frustum culling + distance sphere selection implemented in `terrain_quadtree.cpp`.

## 13. Terrain & Ocean Shader Bindings & Uniforms
### Shader Bindings
| Symbol | Type | Value | Target |
|---|---|---|---|
| `BIND_UBO_TERRAIN` | UBO | 1 | `TerrainUbo` struct block |
| `BIND_SSBO_TERRAIN_NODES` | SSBO | 4 | Selected `GpuTerrainNode` ring buffer |
| `TEX_TERRAIN_HEIGHT` | Texture 2D | 1 | R16 elevation texture (4096m extent) |
| `TEX_TERRAIN_BIOME` | Texture 2D | 2 | R8 biome index texture |
| `TEX_TERRAIN_ALBEDO` | Texture 2DArray | 3 | 8-layer BC1 albedo array (10 mips) |
| `TEX_TERRAIN_NORMAL` | Texture 2DArray | 4 | 8-layer BC5 normal array (10 mips) |
| `TEX_TERRAIN_RH` | Texture 2DArray | 5 | 8-layer BC5 roughness/height array (10 mips) |
| `TEX_OCEAN_DEPTH` | Texture 2D | 6 | 512x512 R8 ocean depth map |

### TerrainUbo Layout (std140, 192 Bytes)
| Member | Type | Offset (Bytes) | Size (Bytes) | Description |
|---|---|---|---|---|
| `lod_range[8]` | `vec4[2]` | 0 | 32 | LOD transition ranges for levels 0..7 |
| `skirt_depth` | `float` | 32 | 4 | Skirt extension distance (10.0m) |
| `rock_layer` | `float` | 36 | 4 | Rock texture layer index (6.0) |
| `tex_n` | `float` | 40 | 4 | Height texture dimension (2048.0) |
| `inv_n` | `float` | 44 | 4 | Reciprocal of height dimension (1/2048) |
| `biome_w` | `float` | 48 | 4 | Biome map width (512.0) |
| `biome_h` | `float` | 52 | 4 | Biome map height (512.0) |
| `ocean_depth_max` | `float` | 56 | 4 | Maximum ocean depth scale (120.0m) |
| `pad` | `float` | 60 | 4 | Explicit std140 alignment padding |
| `biome_layer[32]` | `uvec4[8]` | 64 | 128 | Biome ID to texture layer lookup table |

## 14. Ocean Depth-Map Rule
- **Resolution**: 512x512 R8 texture.
- **Computation**: 2-pass 8-neighbor Chamfer distance transform computed from coastlines into underwater cells.
- **Reconstruction Rule**: In shader, `depth = max(depth_fine, depth_coarse)` ensures consistent depth estimation across shallow and deep ocean regions.
- **Rendering**: Flat `Y = 0.0f` plane rendered after terrain with depth writes disabled (`glDepthMask(GL_FALSE)`).

## 15. Biome to Layer Mapping
| Biome ID | Biome Name | Layer Index | Texture Layer |
|---|---|---|---|
| 0 | Ocean | 0 | `seabed` |
| 1 | Beach | 1 | `beach` |
| 2 | Grass | 2 | `grass` |
| 3 | Forest | 3 | `forest_floor` |
| 4 | Arid | 4 | `arid` |
| 5 | Tundra | 5 | `tundra` |
| 6 | Rock | 6 | `rock` |
| - | Cliff (Slope > 0.7) | 7 | `cliff` |
| 7..31 | Reserved | 0 | `seabed` |

## 16. Render Test Suite (Tests 14..27)
14. `Camera_Smoothing`: Verifies exponential smoothing of camera position and look target.
15. `Camera_GroundTracking`: Verifies camera tracks terrain elevation changes smoothly.
16. `Camera_FrustumExtract`: Verifies extraction of 6 frustum planes from view-projection matrix.
17. `Camera_RayPickGround`: Verifies screen ray intersection with flat ground plane at Y=0.
18. `Terrain_RealData`: Loads `continent_2026.thtr` and verifies dimensions, land fraction, and elevation bounds.
19. `Terrain_Quadtree`: Validates 8-level quadtree construction and node bounding boxes.
20. `Terrain_OceanMap`: Validates 512x512 ocean depth Chamfer distance generation.
21. `Cdlod_Cover`: Verifies CDLOD node selection covers the view frustum without gaps.
22. `Cdlod_Invariants`: Verifies CDLOD LOD hierarchy rules (no T-junctions, max 1 LOD step delta).
23. `Terrain_Upload`: Validates GPU texture creation and upload of height, biome, and layer arrays.
24. `Terrain_DepthMatch`: Validates screen-center ray pick hit distance matches reverse-Z unprojected depth.
25. `Terrain_NoCracks`: Verifies zero rendering cracks or depth leaks across multi-LOD terrain views.
26. `Ocean_Colour`: Validates deep vs shallow ocean water color gradient and sun glint calculations.
27. `Ocean_DepthRule`: Validates ocean depth sampling rule `max(depth_fine, depth_coarse)`.



## 17. World Overlays Specification (Gate 11)
### Shader Bindings
| Symbol | Type | Value | Target |
|---|---|---|---|
| `BIND_UBO_OVERLAY` | UBO | 2 | `OverlayUbo` struct block (320 B, std140) |
| `BIND_UBO_MINIMAP` | UBO | 3 | `MinimapUbo` struct block (48 B, std140) |
| `BIND_SSBO_SPLATS` | SSBO | 5 | Crater splat buffer (64 entries, 2048 B) |
| `BIND_SSBO_BLIPS` | SSBO | 8 | Minimap blip buffer (4096 entries, 65536 B) |
| `TEX_TERRITORY_SDF` | Texture 2D | 7 | 1024x1024 RG16F (signed dist m, owner float) |
| `TEX_TERRITORY_OWNER` | Texture 2D | 8 | 512x512 R8 (owner id 0..15, NEAREST) |
| `TEX_CRATER` | Texture 2D | 9 | 2048x2048 RGBA8 (R=offset, G=scorch, LINEAR) |
| `TEX_FOG` | Texture 2D | 10 | 512x512 RG16F (R=visible, G=explored, LINEAR) |
| `TEX_MINIMAP_BASE` | Texture 2D | 11 | 512x512 RGBA8 (hillshaded base texture) |
| `TEX_FOG_TVIS` | Texture 2D | 12 | 512x512 R8 (target visibility mask, NEAREST) |
| `TEX_FOG_TEXP` | Texture 2D | 13 | 512x512 R8 (target explored mask, NEAREST) |
| `IMG_JFA_IN` | Image | 0 | JFA read image (RG16F) |
| `IMG_JFA_OUT` | Image | 1 | JFA write image (RG16F) |
| `IMG_SDF` | Image | 2 | Territory SDF write image (RG16F) |
| `IMG_CRATER` | Image | 3 | Crater splat write image (RGBA8) |
| `IMG_FOG_SMOOTH` | Image | 4 | Fog smooth write image (RG16F) |

### OverlayUbo Layout (std140, 320 Bytes)
| Member | Type | Offset (Bytes) | Size (Bytes) | Description |
|---|---|---|---|---|
| `owner_color[16]` | `vec4[16]` | 0 | 256 | RGBA color palette for owners 0..15 |
| `p0` | `vec4` | 256 | 16 | x=territory_on, y=fill_alpha, z=line_w0_m, w=line_w1_m |
| `p1` | `vec4` | 272 | 16 | x=fog_on, y=fog_unexplored, z=fog_explored_dim, w=crater_on |
| `p2` | `vec4` | 288 | 16 | x=world_size (4096), y=inv_world (1/4096), z=reserved, w=time_s |
| `p3` | `vec4` | 304 | 16 | x=local_player, yzw=reserved |

### Depth Pass Contract Note
Craters are cosmetic and are NOT evaluated in depth-only passes until Gate 13.

## 18. Crater Formulas & Encoding
- **Spatial Coverage**: 2048x2048 RGBA8 texture (`2m` texels over `4096m` world).
- **Height Encoding**: Code 128 represents `0.0m` elevation offset.
  - Quantization: `code = clamp(round(128.0f + delta_h / 0.0625f), 0, 255)`.
  - Dequantization: `delta_h = (float(code) - 128.0f) * 0.0625f` meters. Step = `1/16 = 0.0625m`.
- **Profiles for Splat (radius R, depth d, scorch s)**:
  - Inside bowl (`u = r / R <= 1.0`): `h(u) = -d * (1.0 - u^2)^2`.
  - Lip rim (`1.0 < u <= 1.35`): `rim(u) = +0.25 * d * sin(pi * (u - 1.0) / 0.35)^2`.
  - Scorch falloff: `scorch(u) = s * (1.0 - smoothstep(0.7, 1.3, u))`.

## 19. Jump Flooding Algorithm (JFA) Signed Distance Field
- **Grid Resolution**: 1024x1024 grid (`4m` texels) over `4096m` world.
- **Seed Units**: Stored in half-texel units `2p + 1 + dir` where `p` is cell index `[0, 511]` and `dir in {-1, +1}` represents boundary edge direction. Unseeded texels store `(-1.0, -1.0)`.
- **Step Sizes**: Passes with step sizes `k = 512, 256, 128, 64, 32, 16, 8, 4, 2, 1`.
- **Tail Passes**: `EXTRA_PASSES = 2` tail passes (`k = 2, 1`) to eliminate diagonal propagation errors.
- **Sign Rule**: `dist = +d` outside territory (unclaimed or foreign), `dist = -d` inside territory. Clamped to `[-2000.0m, +2000.0m]`.

## 20. Fog of War Model
- **Grid Resolution**: 512x512 cells (`8m` per cell).
- **Visibility & Exploration**:
  - `visible`: Set to `255` within vision circles (`reveal_circle(x, z, r)`), `0` elsewhere.
  - `explored`: Accumulates visibility over time (`explored |= visible`).
- **Temporal Smoothing**:
  - Exponential approach rates: Visible approaches target at `10.0 / s`; Explored approaches target at `3.0 / s`.
  - Formula: `val += (target - val) * (1.0 - exp(-rate * dt))`.
- **GPU Texture**: 512x512 RG16F (R = visible `[0.0, 1.0]`, G = explored `[0.0, 1.0]`).

## 21. Entity Renderer Specification (Gate 12)
### Pipeline Bindings
| Symbol | Type | Value | Target |
|---|---|---|---|
| `BIND_UBO_ENTITY` | UBO | 4 | `EntityUbo` struct block (160 B, std140) |
| `BIND_SSBO_INSTANCES` | SSBO | 0 | `EntityInstance` array (64 B each, std430, triple-buffered ring) |
| `BIND_SSBO_VISIBLE` | SSBO | 1 | Visible instance indices array (uint each) |
| `BIND_SSBO_INDIRECT` | SSBO | 2 | `DrawElementsIndirectCommand` array (20 B each, tightly packed) |
| `BIND_SSBO_COUNTERS` | SSBO | 3 | Atomic counters buffer (uint, 16 header + 384 bucket counts) |
| `BIND_SSBO_MODELS` | SSBO | 9 | `ModelGpu` array (80 B each, std430) |
| `BIND_SSBO_BUCKETS` | SSBO | 10 | Rank buffer (`uvec2` per instance: bucket, rank within bucket) |
| `TEX_VAT` | Texture 2D | 14 | 48x(44*M) RGBA32F Vertex Animation Texture (3 texels per part, 16 parts) |
| `TEX_TRIM_ALBEDO` | Texture 2D | 15 | BC7 RGBA trim sheet albedo |
| `TEX_TRIM_NORMAL` | Texture 2D | 16 | BC5 RG trim sheet normal map |
| `TEX_TRIM_ORM` | Texture 2D | 17 | BC7 RGBA trim sheet Occlusion-Roughness-Metallic |
| `TEX_TRIM_EMISSIVE` | Texture 2D | 18 | BC7 RGBA trim sheet emissive map |

### Compute Culling & Multi-Draw Indirect
1. **Compute Cull Count (`EntityCullCount`)**:
   - Alpha-interpolates instance transform: `pos = mix(pos_prev, pos_curr, alpha)`, `yaw = yaw_prev + wrap_pi(yaw_curr - yaw_prev) * alpha`.
   - Bounding sphere transform: `centre_world = pos + R_y(yaw) * (sphere.xyz * scale)`, `radius_w = sphere.w * scale`.
   - Frustum cull: tested against 6 normalized planes extracted from `view_proj` under Reverse-Z ZERO_TO_ONE (`dot(n, centre_world) + d < -radius_w`).
   - Size cull & LOD determination: `dist = length(centre_world - cam_pos)`, `px = radius_w * proj_scale_px / max(dist, 0.001)`.
     - `px >= 48.0` (`LOD0_PX`): LOD 0.
     - `px >= 16.0` (`LOD1_PX`): LOD 1.
     - `px >= 1.5` (`CULL_PX`): LOD 2.
     - `px < 1.5`: size-culled.
   - Atomic bucket count: increments `counters[16 + bucket]` and records `(bucket, rank)` in rank SSBO.
2. **Compute Cull Scan (`EntityCullScan`)**:
   - Single workgroup prefix scan over `384` buckets (`128` models x 3 LODs).
   - Generates compact `DrawElementsIndirectCommand` structs into `BIND_SSBO_INDIRECT`.
3. **Compute Cull Scatter (`EntityCullScatter`)**:
   - Scatters visible instance indices into `BIND_SSBO_VISIBLE` at offset `first_instance + rank`.
4. **Draw Execution**:
   - Single `glMultiDrawElementsIndirect(GL_TRIANGLES, GL_UNSIGNED_INT, nullptr, bucket_count, 0)`.
   - Drawn after terrain and before ocean plane.

