"""trim_paint.py - Trim sheet tile painting and 2048x2048 texture sheet assembly."""

import numpy as np

import sys, os
sys.path.insert(0, os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

from asset_common.hashing import fnv1a64
from asset_common.noise import grid, pfbm
import params
import patterns
from trim_table import TRIM_SPECS


class TileMaps:
    """Collection of physical maps for a single 256x256 trim tile."""

    def __init__(self, albedo, height, rough, metal, ao, emis, normal):
        self.albedo = albedo   # float32 [256, 256, 3] sRGB in 0..1
        self.height = height   # float32 [256, 256] 0..1
        self.rough = rough     # float32 [256, 256] 0..1
        self.metal = metal     # float32 [256, 256] 0..1
        self.ao = ao           # float32 [256, 256] 0..1
        self.emis = emis       # float32 [256, 256] 0..1
        self.normal = normal   # float32 [256, 256, 3] unit normal


def height_to_normal(h, strength=1.0):
    """Compute tangent-space unit normal from height map using wrapped central differences."""
    h = np.asarray(h, dtype=np.float32)
    gx = params.NORMAL_SCALE * float(strength) * (np.roll(h, -1, axis=1) - np.roll(h, 1, axis=1)) * 0.5
    gy = params.NORMAL_SCALE * float(strength) * (np.roll(h, -1, axis=0) - np.roll(h, 1, axis=0)) * 0.5

    g_len = np.sqrt(gx * gx + gy * gy)
    scale = np.where(g_len > 1.3333, 1.3333 / np.maximum(1e-8, g_len), 1.0)
    gx = gx * scale
    gy = gy * scale

    nx = -gx
    ny = -gy
    nz = np.ones_like(gx)

    n_len = np.sqrt(nx * nx + ny * ny + nz * nz)
    inv_len = 1.0 / np.maximum(1e-8, n_len)
    return np.stack([nx * inv_len, ny * inv_len, nz * inv_len], axis=-1)


def paint_trim(spec):
    """Paint all physical maps for a single trim spec tuple."""
    trim_id, name, pat_name, c1_spec, c2_spec, rough_base, metal_base, norm_str, emis_base = spec
    s = fnv1a64(name) & 0xFFFFFFFF

    gx1, gy1 = grid(256, 8, 8)
    n1 = pfbm(gx1, gy1, 8, 8, s, 4)
    gx2, gy2 = grid(256, 32, 32)
    n2 = pfbm(gx2, gy2, 32, 32, s + 1, 3)

    if pat_name == "camo":
        # Palette is passed in c1_spec
        pal = np.array(c1_spec, dtype=np.float32) / 255.0
        pat_fn = getattr(patterns, f"pat_{pat_name}")
        overrides = pat_fn(pal, s)
        albedo = overrides["albedo_base"] * (1.0 + 0.12 * (n2[:, :, None] - 0.5))
    else:
        c1 = np.array(c1_spec, dtype=np.float32) / 255.0
        c2 = np.array(c2_spec, dtype=np.float32) / 255.0
        albedo = (c1[None, None, :] * (1.0 - n1[:, :, None]) + c2[None, None, :] * n1[:, :, None]) * (
            1.0 + 0.12 * (n2[:, :, None] - 0.5)
        )
        pat_fn = getattr(patterns, f"pat_{pat_name}")
        overrides = pat_fn(c1, c2, s, rough_base, metal_base)

    height = 0.5 * n2
    rough = np.clip(rough_base + 0.15 * (n2 - 0.5), 0.0, 1.0)
    metal = np.full((256, 256), metal_base, dtype=np.float32)
    emis = np.full((256, 256), emis_base, dtype=np.float32)

    # Apply overrides
    if "albedo_override" in overrides:
        albedo = overrides["albedo_override"]
    if "albedo_mul" in overrides:
        albedo = albedo * overrides["albedo_mul"]
    if "albedo_add" in overrides:
        albedo = albedo + overrides["albedo_add"]
    if "chips" in overrides:
        chips = overrides["chips"]
        steel_col = np.array([150, 150, 150], dtype=np.float32) / 255.0
        albedo = np.where(chips[:, :, None] > 0.0, steel_col, albedo)
        height = height - chips * 0.15
    if "rust_mask" in overrides:
        rust = overrides["rust_mask"]
        c2 = np.array(c2_spec, dtype=np.float32) / 255.0
        albedo = albedo * (1.0 - rust[:, :, None]) + c2 * rust[:, :, None]
        metal = metal * (1.0 - 0.3 * rust)
    if "gap_mask" in overrides:
        gap = overrides["gap_mask"]
        height = np.where(gap, 0.0, height)
        albedo = np.where(gap[:, :, None], albedo * 0.4, albedo)
    if "mortar_mask" in overrides:
        mortar = overrides["mortar_mask"]
        albedo = np.where(mortar[:, :, None], overrides["mortar_color"], albedo)
    if "height_override" in overrides:
        height = overrides["height_override"]
    if "rough_override" in overrides:
        rough = overrides["rough_override"]
    if "rough_add" in overrides:
        rough = np.clip(rough + overrides["rough_add"], 0.0, 1.0)
    if "metal_override" in overrides:
        metal = overrides["metal_override"]
    if "emis_override" in overrides:
        emis = overrides["emis_override"]

    albedo = np.clip(albedo, 0.0, 1.0)
    height = np.clip(height, 0.0, 1.0)
    rough = np.clip(rough, 0.0, 1.0)
    metal = np.clip(metal, 0.0, 1.0)
    emis = np.clip(emis, 0.0, 1.0)

    cavity = np.clip((0.5 - height) * 2.0, 0.0, 1.0)
    ao = 1.0 - 0.35 * cavity

    normal = height_to_normal(height, norm_str)

    return TileMaps(albedo, height, rough, metal, ao, emis, normal)


def assemble_sheet():
    """Assemble 2048x2048 trim sheet textures."""
    sheet_size = params.SHEET_SIZE
    tile_size = params.TILE_SIZE

    albedo_img = np.full((sheet_size, sheet_size, 3), 128, dtype=np.uint8)
    normal_xy_img = np.full((sheet_size, sheet_size, 2), 128, dtype=np.uint8)
    orm_img = np.zeros((sheet_size, sheet_size, 3), dtype=np.uint8)
    orm_img[..., 0] = 255  # AO
    orm_img[..., 1] = 128  # Rough
    orm_img[..., 2] = 0    # Metal
    emis_img = np.zeros((sheet_size, sheet_size), dtype=np.uint8)

    for spec in TRIM_SPECS:
        trim_id = spec[0]
        col = trim_id % 8
        row = trim_id // 8
        x0 = col * tile_size
        y0 = row * tile_size

        tm = paint_trim(spec)

        albedo_byte = np.rint(tm.albedo * 255.0).astype(np.uint8)
        norm_xy_byte = np.rint(np.clip(tm.normal[..., :2] * 0.5 + 0.5, 0.0, 1.0) * 255.0).astype(np.uint8)
        ao_byte = np.rint(tm.ao * 255.0).astype(np.uint8)
        rough_byte = np.rint(tm.rough * 255.0).astype(np.uint8)
        metal_byte = np.rint(tm.metal * 255.0).astype(np.uint8)
        emis_byte = np.rint(tm.emis * 255.0).astype(np.uint8)

        albedo_img[y0 : y0 + tile_size, x0 : x0 + tile_size] = albedo_byte
        normal_xy_img[y0 : y0 + tile_size, x0 : x0 + tile_size] = norm_xy_byte
        orm_img[y0 : y0 + tile_size, x0 : x0 + tile_size, 0] = ao_byte
        orm_img[y0 : y0 + tile_size, x0 : x0 + tile_size, 1] = rough_byte
        orm_img[y0 : y0 + tile_size, x0 : x0 + tile_size, 2] = metal_byte
        emis_img[y0 : y0 + tile_size, x0 : x0 + tile_size] = emis_byte

    return {
        "albedo": albedo_img,
        "normal_xy": normal_xy_img,
        "orm": orm_img,
        "emissive": emis_img,
    }
