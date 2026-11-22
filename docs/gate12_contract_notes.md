# Gate 12 Contract Notes: Entity System & Model Layout

## Q1: Vertex Byte Layout (24 bytes)
- Source: `src/assets/model_data.hpp:82-95`, `tools/model_baker/mformat.py:38-46`, `tools/model_baker/octa.py:6-77`
- Stride: 24 bytes
- Layout:
  - Offset 0: `float x, y, z` (12 B, 3x float32) - Model-space position in meters.
  - Offset 12: `i16 nx, ny` (4 B, 2x int16) - Octahedral encoded unit normal vector (`snorm16`). Decoded via `ModelData::decode_oct`.
  - Offset 16: `u16 u, v` (4 B, 2x uint16) - Normalized texture coordinates (`unorm16`, divide by 65535.0f).
  - Offset 20: `u8 trim` (1 B, uint8) - Trim sheet tile index (0..31).
  - Offset 21: `u8 flags` (1 B, uint8) - Vertex flags (bit 0: team colour, bit 1: emissive, bit 2: scroll uv).
  - Offset 22: `u16 part` (2 B, uint16) - Skeletal part index (0..15) for VAT skinning.

## Q2: VAT Semantics & Texel Layout
- Source: `src/assets/model_data.hpp:22-26, 137`, `src/assets/model_data.cpp:245-271`, `tools/model_baker/vat.py:9-33`, `tools/model_baker/rig.py:67-91`
- Transform type: ABSOLUTE part transform ($3 \times 4$ affine matrix, rows 0..2) in model coordinates, transforming vertex from rest pose to animated pose.
- Texel layout: RGBA16F texture with width = 48 texels (= 16 parts $\times$ 3 rows), height = $44 \times \text{model\_count}$.
  - Column index: `col = part * 3 + r` where $r \in \{0, 1, 2\}$ is the matrix row.
  - Row index: `row = vat_row_base + clip_start[clip] + frame`.
  - Texel format: `RGBA16F`, texel value = `(m[r][0], m[r][1], m[r][2], m[r][3])`.
- Frame-0 Idle equals identity $\begin{pmatrix} 1 & 0 & 0 & 0 \\ 0 & 1 & 0 & 0 \\ 0 & 0 & 1 & 0 \end{pmatrix}$ for all 16 parts (tested in `tests/test_models.cpp:328-340`).
- Unused parts ($\ge \text{part\_count}$) store the identity transform.

## Q3: Index/Vertex Organization per LOD
- Source: `src/assets/model_data.hpp:57-60`, `tools/model_baker/bake_models.py:122-152`, `tests/test_models.cpp:223-225`
- Organization: Each LOD owns its own contiguous vertex run within the model bundle.
- Base vertex: `lod_base_vertex[lod] = rec.vtx_base[lod]`. Indices are 0-based relative to `vtx_base[lod]`.
- First index: `lod_first_index[lod] = rec.idx_first[lod]`.
- Index count: `lod_index_count[lod] = rec.idx_count[lod]`.

## Q4: Model-Local Forward Axis & Yaw Rotation ($R_y$)
- Source: `tools/model_baker/gen_ground.py:22-27`, `tools/model_baker/gen_humanoid.py:36-49`, `tools/model_baker/gen_air.py:34-48`, `tests/test_models.cpp:215-220`
- Model-local axes: Forward is $+X$ (`(1, 0, 0)`), Up is $+Y$ (`(0, 1, 0)`), Left is $+Z$ (`(0, 0, 1)`).
- World heading: $\text{heading}(\text{yaw}) = (\sin(\text{yaw}), 0, -\cos(\text{yaw}))$.
  - $\text{yaw} = 0$: North $(0, 0, -1)$.
  - $\text{yaw} = \pi/2$: East $(1, 0, 0)$.
  - $\text{yaw} = \pi$: South $(0, 0, 1)$.
  - $\text{yaw} = 3\pi/2$: West $(-1, 0, 0)$.
- $R_y(\text{yaw})$ 3x3 matrix (column-major in memory):
  $$R_y(\theta) = \begin{pmatrix} \sin\theta & 0 & \cos\theta \\ 0 & 1 & 0 \\ -\cos\theta & 0 & \sin\theta \end{pmatrix}$$
  Checking:
  $$R_y(\theta) \begin{pmatrix} 1 \\ 0 \\ 0 \end{pmatrix} = \begin{pmatrix} \sin\theta \\ 0 \\ -\cos\theta \end{pmatrix} = \text{heading}(\theta)$$
  $$R_y(\theta) \begin{pmatrix} 0 \\ 1 \\ 0 \end{pmatrix} = \begin{pmatrix} 0 \\ 1 \\ 0 \end{pmatrix} = \text{up}$$
  $$R_y(\theta) \begin{pmatrix} 0 \\ 0 \\ 1 \end{pmatrix} = \begin{pmatrix} \cos\theta \\ 0 \\ \sin\theta \end{pmatrix} = \text{left}$$
  $\det(R_y) = \sin^2\theta + \cos^2\theta = 1$.

## Q5: Triangle Winding
- Source: `tests/test_models.cpp:270-305`, `tools/model_baker/meshbuilder.py`
- Winding: COUNTER-CLOCKWISE (CCW) when viewed from outside front faces.
- Proof: In `tests/test_models.cpp`, geometric triangle normal $(v_1 - v_0) \times (v_2 - v_0)$ has positive dot product with decoded outward vertex normals for $\ge 99\%$ of triangles.
- Function: `bool entity_front_face_ccw() { return true; }`.

## Q6: Normal-Map Convention & Bitangent Sign
- Source: `docs/asset_contracts.md:24-29`, `tests/test_textures.cpp:254-269`
- Convention: $u$ increases right, $v$ increases DOWN the texture. Green channel stores $+v$ tangent-space direction ($n_y$).
- Tangent frame derivation:
  $$dp_1 = dFdx(p), \quad dp_2 = dFdy(p)$$
  $$duv_1 = dFdx(uv), \quad duv_2 = dFdy(uv)$$
  $$T = dp_1 \cdot duv_2.y - dp_2 \cdot duv_1.y$$
  $$B = dp_2 \cdot duv_1.x - dp_1 \cdot duv_2.x$$
  Bitangent sign is positive: $B = dP/dv$.

## Q7: VAT Animation Frame Rate
- Source: `tools/model_baker/params.py:52-59`
- Decision: `ANIM_FPS = 16.0f` (fallback per spec).

## Q8: sRGB Flag Status & Texture Formats for tex26 IDs 0..3
- Source: `docs/asset_contracts.md:10-20`, `src/assets/texture_data.hpp:45`
- Flags:
  - ID 0 (`trim_albedo`): Format BC1, flags = 1 (sRGB).
  - ID 1 (`trim_normal`): Format BC5, flags = 0 (Linear).
  - ID 2 (`trim_orm`): Format BC1, flags = 0 (Linear).
  - ID 3 (`trim_emissive`): Format BC4, flags = 0 (Linear).
- Decision: `TexFmt::BC1_SRGB` for `trim_albedo` (`GL_COMPRESSED_SRGB_S3TC_DXT1_EXT`), enabling hardware sRGB decode on sample. `RON_ENTITY_ALBEDO_DECODE = 0`.
- Format mappings:
  - `trim_albedo`: `TexFmt::BC1_SRGB` (`GL_COMPRESSED_SRGB_S3TC_DXT1_EXT`)
  - `trim_normal`: `TexFmt::BC5` (`GL_COMPRESSED_RG_RGTC2`)
  - `trim_orm`: `TexFmt::BC1` (`GL_COMPRESSED_RGB_S3TC_DXT1_EXT`)
  - `trim_emissive`: `TexFmt::BC4` (`GL_COMPRESSED_RED_RGTC1`)

## ModelData API Reference
- `model_count()` -> u32
- `model(i)` -> `const ModelRec&` (accessing `category`, `vtx_base`, `vtx_count`, `idx_first`, `idx_count`, `part_count`, `vat_row_base`, `bmin`, `bmax`, `radius_xz`)
- `vertices()` -> `const VertexRec*`, `vertex_count()` -> u32
- `indices()` -> `const u16*`, `index_count()` -> u32
- `vat_halfs()` -> `const u16*`, `vat_rows()` -> u32
- `vat_matrix(model_idx, clip_idx, frame_idx, part_idx, float m[12])` -> bool
- `decode_oct(i16 nx, i16 ny, float out[3])` -> static helper
- `half_to_float(u16 h)` -> static helper
