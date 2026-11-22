# Asset Contracts: Dominion 2026 Texture System

## 1. Container Specification (.tex26)
- **Endianness**: Little-endian, 64-byte aligned offsets throughout.
- **Header (64 B)**: Magic `0x36325854` ('TX26'), version `1`, `total_bytes`, `texture_count` (9), `content_xxh64` (seed 0 over bytes [64, total_bytes)), `glyph_offset`, `glyph_count` (95), padding to 64 bytes.
- **Texture Table (64 B records)**: `name_hash` (FNV-1a-64), `format` (0=BC1, 1=BC3, 2=BC4, 3=BC5, 4=RGBA8), `width`, `height`, `layers`, `mips`, `data_offset` (64-aligned), `data_bytes`, `flags` (bit 0: sRGB, bit 1: premul alpha), `probe_a[4]` (mip 0, (37, 91)), `probe_b[4]` (last mip, (0, 0)).
- **Glyph Table (16 B records)**: 95 glyph records `<4H 2h 2H` (cell_x, cell_y, cell_w, cell_h, offset_x_q, offset_y_q, advance_q, reserved).

## 2. Textures & LOD Clamping
| ID | Name | Format | Dimensions | Layers | Mips | Flags | Role |
|---|---|---|---|---|---|---|---|
| 0 | `trim_albedo` | BC1 | 2048x2048 | 1 | 12 | 1 (sRGB) | 8x8 trim sheet albedo |
| 1 | `trim_normal` | BC5 | 2048x2048 | 1 | 12 | 0 | 8x8 trim sheet tangent normals (XY) |
| 2 | `trim_orm` | BC1 | 2048x2048 | 1 | 12 | 0 | R=AO, G=Roughness, B=Metallic |
| 3 | `trim_emissive` | BC4 | 2048x2048 | 1 | 12 | 0 | Single-channel emissive intensity |
| 4 | `terrain_albedo` | BC1 | 512x512 | 8 | 10 | 1 (sRGB) | 8 biome terrain albedos |
| 5 | `terrain_normal` | BC5 | 512x512 | 8 | 10 | 0 | 8 biome terrain normals (XY) |
| 6 | `terrain_rh` | BC1 | 512x512 | 8 | 10 | 0 | R=Height, G=Roughness, B=0 |
| 7 | `vfx_atlas` | BC3 | 2048x2048 | 1 | 4 | 2 (Premul) | 12 flipbooks, 16 frames (128x128) |
| 8 | `msdf_atlas` | RGBA8 | 1024x512 | 1 | 1 | 0 | RGB=MSDF channels, A=True SD |

> **Critical LOD Clamp Rule**: The renderer MUST clamp trim sheet texture sampling LOD to `<= 5`. Beyond mip 5, individual 256x256 trim tiles bleed into neighbouring tiles across UV boundaries.

## 3. Tangent Normal Convention
- Tangent space $u$ increases to the right; $v$ increases DOWN the texture.
- Stored $n_{xy} = (-\partial h/\partial u, -\partial h/\partial v)$ normalized such that $|g| \le 1.3333$ and $n_z \ge 0.6$.
- Reconstruction on GPU: $n_z = \sqrt{\max(0, 1 - n_x^2 - n_y^2)}$.
- Encoded byte: `rint((n * 0.5 + 0.5) * 255)`.

## 4. Atlas Layouts
- **Font & Icon Atlas (`msdf_atlas`, 1024x512 RGBA8)**:
  - Glyphs (ASCII 32..126): 48x48 px cells in 12 columns at $x \in [0, 576), y \in [0, 384)$.
  - Icons (32 icons): 64x64 px cells in 16 columns at $x \in [0, 1024), y \in [384, 512)$.
  - Distance encoding: $v = \text{clip}(0.5 + \text{sd\_px} / (2 \cdot 4.0), 0, 1) \times 255$, positive inside.
- **VFX Atlas (`vfx_atlas`, 2048x2048 BC3 Premultiplied)**:
  - 12 effects, each 16 frames in a 4x4 grid of 128x128 px frames within a 512x512 cell.
  - Cell $e$ at $((e\%4) \cdot 512, (e//4) \cdot 512)$. Frame $f$ at $((f\%4) \cdot 128, (f//4) \cdot 128)$ within cell.
  - Cells 12..15 reserved/empty. Outermost 2 px on every 128x128 frame forced to zero.

## 5. Block Compression Codec Formulas
- **BC1**: 8 B per 4x4 block. $c_0, c_1$ in RGB565. If $c_0 > c_1$: 4-color mode $(c_0, c_1, (2c_0+c_1)/3, (c_0+2c_1)/3)$.
- **BC4**: 8 B per 4x4 block. $a_0, a_1$ 8-bit endpoints. If $a_0 > a_1$: 8-value ramp $(a_0, a_1, (6a_0+a_1)/7, \dots)$.
- **BC5**: 16 B per 4x4 block. Channel 0 (X) BC4 block followed by Channel 1 (Y) BC4 block.
- **BC3**: 16 B per 4x4 block. Alpha BC4 block followed by Color BC1 block (always 4-color mode).
