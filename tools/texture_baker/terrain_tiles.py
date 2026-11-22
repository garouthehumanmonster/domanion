"""terrain_tiles.py - Procedural terrain tile generation (8 layers)."""

import os
import sys
import numpy as np

sys.path.insert(0, os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

from asset_common.noise import grid, pfbm
from asset_common.hashing import fnv1a64
from trim_paint import height_to_normal

TERRAIN_LAYERS = [
    # id, name, c1, c2, rough, norm_strength
    (0, "seabed", (120, 110, 90), (95, 88, 72), 0.90, 0.5),
    (1, "beach", (222, 206, 160), (196, 180, 135), 0.90, 0.8),
    (2, "grass", (88, 138, 52), (62, 106, 38), 0.85, 0.8),
    (3, "forest_floor", (58, 72, 38), (40, 50, 28), 0.88, 0.8),
    (4, "arid", (196, 164, 104), (170, 138, 84), 0.92, 0.8),
    (5, "tundra", (176, 182, 170), (140, 146, 138), 0.80, 0.8),
    (6, "rock", (122, 116, 108), (88, 84, 78), 0.85, 1.0),
    (7, "cliff", (92, 84, 76), (62, 56, 50), 0.85, 1.2),
]


def paint_terrain_layer(idx: int):
    """Paint a single 512x512 terrain layer: returns (albedo, normal_xy, rh)."""
    assert 0 <= idx < 8
    t_id, name, c1_spec, c2_spec, rough_base, norm_str = TERRAIN_LAYERS[idx]
    seed = fnv1a64(f"terrain_{name}") & 0xFFFFFFFF

    gx4, gy4 = grid(512, 4, 4)
    gx8, gy8 = grid(512, 8, 8)
    gx16, gy16 = grid(512, 16, 16)
    gx32, gy32 = grid(512, 32, 32)

    n_base = pfbm(gx4, gy4, 4, 4, seed, 5)
    detail = pfbm(gx32, gy32, 32, 32, seed + 1, 3)

    c1 = np.array(c1_spec, dtype=np.float32) / 255.0
    c2 = np.array(c2_spec, dtype=np.float32) / 255.0

    albedo = (c1[None, None, :] * (1.0 - n_base[:, :, None]) + c2[None, None, :] * n_base[:, :, None]) * (
        1.0 + 0.12 * (detail[:, :, None] - 0.5)
    )

    # Feature-specific height and albedo modulation
    if name == "seabed":
        # Ripples: sin along x wrapped at period 8
        wave = np.sin(gx8 * (2.0 * np.pi)) * 0.35 + 0.5
        n_sub = pfbm(gx16, gy16, 16, 16, seed + 2, 3)
        raw_h = 0.6 * wave + 0.4 * n_sub

    elif name == "beach":
        # Fine sand grain + subtle dunes
        dune = pfbm(gx4, gy4, 4, 4, seed + 2, 3)
        sand = pfbm(gx32, gy32, 32, 32, seed + 3, 2)
        raw_h = 0.5 * dune + 0.5 * sand

    elif name == "grass":
        # Blades / clumps
        gx_clump, gy_clump = grid(512, 16, 8)
        clumps = pfbm(gx_clump, gy_clump, 16, 8, seed + 2, 4)
        raw_h = clumps

    elif name == "forest_floor":
        # Leaf litter blotches (90, 66, 40)
        blotches = pfbm(gx16, gy16, 16, 16, seed + 2, 3)
        litter_col = np.array([90, 66, 40], dtype=np.float32) / 255.0
        mask = np.clip((blotches - 0.55) / 0.2, 0.0, 1.0)[:, :, None]
        albedo = albedo * (1.0 - mask) + litter_col[None, None, :] * mask
        raw_h = blotches

    elif name == "arid":
        # Cracked-earth polygons (ridged low-frequency)
        cracks = 1.0 - np.abs(2.0 * pfbm(gx8, gy8, 8, 8, seed + 2, 4) - 1.0)
        albedo = albedo * (0.8 + 0.2 * cracks[:, :, None])
        raw_h = cracks

    elif name == "tundra":
        # Snow patches (235, 240, 245) where pfbm > 0.6
        snow_noise = pfbm(gx8, gy8, 8, 8, seed + 2, 4)
        snow_col = np.array([235, 240, 245], dtype=np.float32) / 255.0
        snow_mask = np.clip((snow_noise - 0.6) / 0.15, 0.0, 1.0)[:, :, None]
        albedo = albedo * (1.0 - snow_mask) + snow_col[None, None, :] * snow_mask
        raw_h = snow_noise

    elif name == "rock":
        # Strata + cracks
        strata = np.sin(gy16 * (2.0 * np.pi)) * 0.25 + 0.5
        cracks = 1.0 - np.abs(2.0 * pfbm(gx16, gy16, 16, 16, seed + 2, 4) - 1.0)
        raw_h = 0.5 * strata + 0.5 * cracks

    elif name == "cliff":
        # Strong horizontal strata
        strata = np.sin(gy32 * (2.0 * np.pi)) * 0.4 + 0.5
        n_rock = pfbm(gx8, gy16, 8, 16, seed + 2, 3)
        raw_h = 0.6 * strata + 0.4 * n_rock
    else:
        raw_h = n_base

    # Normalize height so that 1st-99th percentile spans >= 0.5 of [0, 1]
    q01, q99 = np.quantile(raw_h.astype(np.float64), [0.01, 0.99])
    span = q99 - q01
    if span < 0.52:
        # Stretch around median / midpoint
        mid = (q01 + q99) * 0.5
        target_span = 0.56
        norm_h = (raw_h - mid) * (target_span / max(1e-6, span)) + 0.5
    else:
        norm_h = (raw_h - q01) / max(1e-6, span) * 0.6 + 0.2
    norm_h = np.clip(norm_h, 0.0, 1.0)

    # Compute normal
    normal = height_to_normal(norm_h, norm_str)
    normal_xy = np.rint(np.clip(normal[..., :2] * 0.5 + 0.5, 0.0, 1.0) * 255.0).astype(np.uint8)

    # Ensure reconstructed nz >= 0.6 - 1e-6 after uint8 quantization
    for _ in range(3):
        nx = normal_xy[..., 0].astype(float) / 255.0 * 2.0 - 1.0
        ny = normal_xy[..., 1].astype(float) / 255.0 * 2.0 - 1.0
        bad = (nx * nx + ny * ny) > 0.64
        if not np.any(bad):
            break
        normal_xy[..., 0] = np.where(bad & (normal_xy[..., 0] > 128), normal_xy[..., 0] - 1, normal_xy[..., 0])
        normal_xy[..., 0] = np.where(bad & (normal_xy[..., 0] < 128), normal_xy[..., 0] + 1, normal_xy[..., 0])
        normal_xy[..., 1] = np.where(bad & (normal_xy[..., 1] > 128), normal_xy[..., 1] - 1, normal_xy[..., 1])
        normal_xy[..., 1] = np.where(bad & (normal_xy[..., 1] < 128), normal_xy[..., 1] + 1, normal_xy[..., 1])


    # Roughness
    rough = np.clip(rough_base + 0.05 * (detail - 0.5), 0.0, 1.0)
    rough_byte = np.rint(rough * 255.0).astype(np.uint8)
    height_byte = np.rint(norm_h * 255.0).astype(np.uint8)

    albedo_byte = np.rint(np.clip(albedo, 0.0, 1.0) * 255.0).astype(np.uint8)

    rh_byte = np.zeros((512, 512, 3), dtype=np.uint8)
    rh_byte[..., 0] = height_byte
    rh_byte[..., 1] = rough_byte
    rh_byte[..., 2] = 0

    return albedo_byte, normal_xy, rh_byte


def build_terrain():
    """Build all 8 terrain layers: returns (albedo, normal_xy, rh) stacked along axis 0."""
    albedos = []
    normals = []
    rhs = []

    for i in range(8):
        alb, nxy, rh = paint_terrain_layer(i)
        albedos.append(alb)
        normals.append(nxy)
        rhs.append(rh)

    return (
        np.stack(albedos, axis=0),
        np.stack(normals, axis=0),
        np.stack(rhs, axis=0),
    )
