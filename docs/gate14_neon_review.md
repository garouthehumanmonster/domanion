# Principal Graphics Architect Review — DOMINION 2026 | Gate 14 of 30

**Model:** Gemini 4 Neon (`Thinking Level: High`)  
**Target Stack:** Windows 11 | NVIDIA RTX 3050 Laptop (Ampere GA107, 6 GB GDDR6, 96-bit bus ~144–168 GB/s) | OpenGL 4.6 Core AZDO | Static VRAM Ledger Cap: 2,304 MiB  
**Status:** CONDITIONAL PASS (Fix 4 architectural blockers prior to Gate 14 lock)

---

## 1. Format & Hardware Viability: R11G11B10F Compute Dual-Filter Bloom

- **OpenGL 4.6 Core Support:** Yes—100% Core, no extensions required. `GL_NV_image_formats` is an OpenGL ES extension and is irrelevant on desktop OpenGL 4.6 Core. `GL_R11F_G11F_B10F` (`layout(r11f_g11f_b10f)`) has been a mandatory core image load/store format since OpenGL 4.2 (`GL_ARB_shader_image_load_store`, OpenGL 4.6 Spec Table 8.26).
- **Ampere Hardware Viability:** GA107 natively executes `R11G11B10F` surface stores (`SUST`) in hardware. Because the RTX 3050 6 GB Laptop variant is constrained by a 96-bit memory bus, `R11G11B10F` (32 bpp) cuts bloom memory traffic by 50% versus `RGBA16F` (64 bpp).
- **Compute vs. FBO Architectural Verdict:** Keep Compute + `R11G11B10F`, with three strict implementation rules:
  1. **Use `glTextureView` per mip (or `GL_TEXTURE_BASE_LEVEL` / `GL_TEXTURE_MAX_LEVEL`):** Dual-filter bloom reads mip `N-1` via hardware-bilinear `sampler2D` while writing mip `N` via `writeonly image2D`. Create lightweight immutable `glTextureView` handles per mip level at init (0 MiB VRAM overhead) to avoid OpenGL driver validation hazards.
  2. **Upsample Accumulation without Ping-Pong:** Each thread at pixel `p` in mip `N` reads mip `N+1` via bilinear `sampler2D` (9-tap tent / 8-tap Kawase) and reads its own texel from mip `N` via `imageLoad(uMipN, p)`, adds them, and writes back with `imageStore(uMipN, p, ...)`.
  3. **Precision & Sign-Bit Guardrails:** `R11G11B10F` is unsigned (5-bit exponent, 6/6/5-bit mantissa; max ~65,024).
     - Sanitize the Prefilter pass (Mip 0 -> Mip 1): strip `NaN` / `Inf`, clamp to `[0.0, 65000.0]`, and apply a Karis Average on the first 13-tap downsample to eliminate sub-pixel HDR firefly flickering.
     - Note the 5-bit blue mantissa: over >6 mip levels, repeated quantization can tint dim blue glows green/yellow. Cap bloom chain depth at 5–6 mips (stopping around ~30×17 at 1080p) and use a `float` intermediate register during all filter taps before the final `imageStore`.

---

## 2. Soft Particles Depth Reconstruction (Reverse-Z D32F `[1, 0]`)

### Prerequisite Verification
Reverse-Z in OpenGL requires `glClipControl(GL_LOWER_LEFT, GL_ZERO_TO_ONE)`. Under `GL_ZERO_TO_ONE` and default `glDepthRange(0.0, 1.0)`, both `texelFetch(uSceneDepth, ivec2(gl_FragCoord.xy), 0).r` and `gl_FragCoord.z` are directly in `[1.0, 0.0]`—do NOT apply `2*z - 1`.

### Exact Eye-Space Depth Linearization Formulas
Assuming standard right-handed view space (positive eye-space distance $z_e$):
- **Finite Far Plane ($n, f$):**
  $$z_e = \frac{n \cdot f}{d \cdot (f - n) + n}$$
  Boundary check: $d = 1 \implies z_e = n$; $d = 0 \implies z_e = f$ (guard `if (d <= 0.0) return f;`).
- **Infinite Far Plane:** $z_e = \frac{n}{\max(d, 1e-7)}$.

### Soft Particle Fade
```glsl
float sceneZ = texelFetch(uSceneDepthCopy, ivec2(gl_FragCoord.xy), 0).r;
float sceneDist = linearizeReverseZ(sceneZ, uNear, uFar);
float partDist = linearizeReverseZ(gl_FragCoord.z, uNear, uFar);
float fade = clamp((sceneDist - partDist) * uInvSoftRange, 0.0, 1.0);
fade = fade * fade * (3.0 - 2.0 * fade);
```

### Critical Feedback-Loop & Hi-Z Warning
Sampling the currently bound `GL_DEPTH_ATTACHMENT` inside `vfx_draw.frag`—even with `glDepthMask(GL_FALSE)`—can force NVIDIA depth buffer decompression and disable Hi-Z / Early-Z culling for high-overdraw particles.
- **Mandatory AZDO Solution:** Allocate a dedicated read-only `D32F` depth copy in the static VRAM ledger (`7.91 MiB` at 1080p; `14.06 MiB` at 1440p). Execute `glCopyImageSubData` after the opaque depth pass, keep primary `D32F` attached to the VFX FBO with `glDepthMask(GL_FALSE)` and `glDepthFunc(GL_GREATER)` (or `GL_GEQUAL`), preserving hardware Hi-Z, and sample the `D32F` copy in `vfx_draw.frag`.

---

## 3. ACES Stephen Hill Analytical Fit & Triangular Dither

### Column-Major GLSL Verification
Stephen Hill's HLSL reference matrices are row-major and must be transposed into GLSL column-major `mat3(...)`:

```glsl
// sRGB/Rec.709 (D65) -> AP1 (D60 + RRT_SAT_MAT baked) — COLUMN-MAJOR
const mat3 ACESInputMat = mat3(
    vec3(0.59719, 0.07600, 0.02840), // col 0
    vec3(0.35458, 0.90834, 0.13383), // col 1
    vec3(0.04823, 0.01566, 0.83777)  // col 2
);

// AP1 -> sRGB/Rec.709 (D65 + ODT_SAT_MAT baked) — COLUMN-MAJOR
const mat3 ACESOutputMat = mat3(
    vec3( 1.60475, -0.10208, -0.00327), // col 0
    vec3(-0.53108,  1.10813, -0.07276), // col 1
    vec3(-0.07367, -0.00605,  1.07602)  // col 2
);

vec3 RRTAndODTFit(vec3 v) {
    vec3 a = v * (v + 0.0245786) - 0.000090537;
    vec3 b = v * (0.983729 * v + 0.4329510) + 0.238081;
    return a / b;
}

vec3 ACESFitted(vec3 color) {
    color = max(color, 0.0);
    color = ACESInputMat * color;
    color = RRTAndODTFit(color);
    color = ACESOutputMat * color;
    return clamp(color, 0.0, 1.0); // MANDATORY: RRTAndODTFit(0) = -0.00038028 -> NaN in pow()!
}
```

- At `v = 0.0`, `RRTAndODTFit(0) = -0.00038028`. Always clamp `[0.0, 1.0]` before IEC sRGB OETF.
- Hill's fit maps 18% mid-grey (`0.18` linear) to `~0.1056` linear (`~0.357` sRGB); calibrate exposure bias accordingly (`+0.5` to `+0.7` EV vs Reinhard).

### Triangular Dither (TPDF) Verification
- Apply dithering **after** linear -> exact IEC 61966-2-1 sRGB OETF, with `GL_FRAMEBUFFER_SRGB` disabled.
- Sample a 64×64 tileable RG8 Blue Noise texture: `vec2 bn = texelFetch(uBlueNoise, ivec2(gl_FragCoord.xy) & 63, 0).rg; float tpdf = (bn.r + bn.g - 1.0) / 255.0; return clamp(srgb + tpdf, 0.0, 1.0);`.

---

## 4. Particle System Memory & Dispatch Audit

1. **Ledger Accounting & `std430` Layout:** `65536 * 48 B = 3.00 MiB` exact (3,145,728 bytes). Enforce `static_assert(sizeof(Particle) == 48 && alignof(Particle) == 16)`. Pack using `vec4` / `uvec4`.
2. **Non-Contiguous Pool Compaction:** Keep the 3.00 MiB `ParticleSSBO` as the fixed-slot pool and add a 256 KiB (`0.25 MiB`) `AliveIndexSSBO` (`uint aliveIndices[65536]`).
   - Clear indirect count before compute simulate.
   - Simulate pass updates slots and appends live culled `particleID`s into `AliveIndexSSBO`.
   - Execute `glMemoryBarrier(GL_COMMAND_BARRIER_BIT | GL_SHADER_STORAGE_BARRIER_BIT)` before indirect draw.
3. **Subgroup Ballot Compaction + Indexed Quads:**
```glsl
uvec4 ballot = subgroupBallot(isAlive);
uint warpCount = subgroupBallotBitCount(ballot);
uint warpBase = 0;
if (subgroupElect()) {
    warpBase = atomicAdd(drawCmd.count, warpCount * 6u);
}
uint writeIdx = (subgroupBroadcastFirst(warpBase) / 6u) + subgroupBallotExclusiveBitCount(ballot);
if (isAlive) aliveIndices[writeIdx] = id;
```
   Use a static pre-built quad IBO (65,536 quads = `1.50 MiB` uint32 IBO, `count = aliveCount * 6`, `instanceCount = 1`, `particleID = aliveIndices[gl_VertexID >> 2]`). Guard `#extension GL_KHR_shader_subgroup_ballot : enable` with scalar `atomicAdd` fallback. Restrict Gate 14 particles strictly to additive/emissive HDR VFX (`GL_ONE, GL_ONE`) unless depth-sorted separately.

---

## 5. Gate 14 Architectural Rating

| Dimension | Score | Rationale |
|---|---|---|
| Mathematical Rigor | **8.0 / 10** | Strong Stephen Hill ACES, TPDF dithering, Dual-Filter Kawase, Reverse-Z; docked for `RRTAndODTFit(0)` clamp, MiB vs MB, and skybox guard. |
| GPU Performance (RTX 3050) | **8.5 / 10** | `R11G11B10F` compute bloom and GPU indirect particles fit 96-bit Ampere bus; docked for raw atomic contention, 6-vertex instancing, and bound depth sampling hazard. |
| Edge Cases & Robustness | **6.5 / 10** | Sparse ring compaction contradiction, column-major matrix trap, `D32F` decompression hazard, and HDR NaN/firefly bloom explosion risks must be closed. |
| **Overall Composite** | **7.7 / 10** | **Promoted to 9.5 / 10 once the 4 fixes above are applied.** |

---

## 6. Five Critical Verification Tests (Zero-Regression Suite)

1. **HDR NaN / Inf / Firefly Quarantine Test:** Inject synthetic `NaN`, `+Inf`, `-5.0`, and `100000.0` spikes; assert zero `NaN`/`Inf` in swapchain readback and zero strobing.
2. **Reverse-Z Soft Particle Linearity & Hi-Z Integrity Test:** Intersect billboards cleanly up to skybox (`d = 0.0`); verify zero GL debug errors, linear reconstruction accuracy, and no Hi-Z stalls.
3. **ACES Colorimetric Reference & 8-Bit Banding Audit:** Compare 16-bit HDR ramp & swatches against CPU reference (`RRTAndODTFit`, column-major matrices, IEC sRGB OETF) and verify dark linear gradient (`0.0..0.02`) shows zero Mach banding with TPDF noise.
4. **Particle Ring-Buffer Wrap-Around Stress Test:** Spawn 10,000 particles/frame for 1,000 frames (>150 wraps) at 65,536 capacity; assert exact `AliveIndexSSBO` count and zero out-of-bounds accesses.
5. **Static VRAM Ledger & Frame-Budget Compliance Test:** Confirm 0 bytes per-frame GPU allocation, exact Gate 14 ledger accounting, and combined Gate 14 GPU time within RTX 3050 budget.
