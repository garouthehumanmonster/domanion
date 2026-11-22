"""patterns.py - Procedural pattern generators for trim sheet tiles."""

import numpy as np
from asset_common.noise import grid, pfbm, smoothstep, hash2


def pat_plain(c1, c2, s, rough_base, metal_base):
    return {}


def pat_worn(c1, c2, s, rough_base, metal_base):
    gx, gy = grid(256, 2, 96)
    scratches = smoothstep(0.62, 0.70, pfbm(gx, gy, 2, 96, s + 10, 3))
    rx, ry = grid(256, 6, 6)
    rust = smoothstep(0.55, 0.65, pfbm(rx, ry, 6, 6, s + 20, 3))
    return {
        "albedo_add": scratches[:, :, None] * 0.25,
        "rough_add": -scratches * 0.1,
        "rust_mask": rust,
    }


def pat_paint(c1, c2, s, rough_base, metal_base):
    px, py = grid(256, 24, 24)
    chips = smoothstep(0.65, 0.75, pfbm(px, py, 24, 24, s + 5, 3))
    return {
        "chips": chips,
    }


def pat_camo(palette, s):
    cx, cy = grid(256, 4, 4)
    n = pfbm(cx, cy, 4, 4, s + 3, 3)
    p = np.zeros((256, 256, 3), dtype=np.float32)
    b0 = n < 0.35
    b1 = (n >= 0.35) & (n < 0.50)
    b2 = (n >= 0.50) & (n < 0.65)
    b3 = n >= 0.65
    p[b0] = palette[0]
    p[b1] = palette[1]
    p[b2] = palette[2]
    p[b3] = palette[3]
    return {"albedo_base": p}


def pat_cloth(c1, c2, s, rough_base, metal_base):
    i = np.arange(256, dtype=np.float32)
    X, Y = np.meshgrid(i, i)
    weave = 0.5 + 0.25 * np.sin(2.0 * np.pi * X / 4.0) * np.sin(2.0 * np.pi * Y / 4.0)
    return {
        "height_override": weave,
        "albedo_mul": (0.9 + 0.2 * weave)[:, :, None],
    }


def pat_leather(c1, c2, s, rough_base, metal_base):
    lx, ly = grid(256, 10, 10)
    n = pfbm(lx, ly, 10, 10, s + 7, 3)
    cracks = 1.0 - np.abs(2.0 * n - 1.0)
    return {
        "height_override": cracks * 0.8,
        "albedo_mul": (0.6 + 0.4 * cracks)[:, :, None],
    }


def pat_planks(c1, c2, s, rough_base, metal_base):
    y = np.arange(256, dtype=np.int32)
    row_idx = y // 32
    h_tint = ((hash2(row_idx, np.zeros_like(row_idx), s) % 100).astype(np.float32) / 500.0) - 0.1
    tint_map = np.tile(h_tint[:, None, None], (1, 256, 3))
    gx, gy = grid(256, 3, 48)
    grain = pfbm(gx, gy, 3, 48, s + 9, 3)
    gap = (y % 32 < 2)
    gap_map = np.tile(gap[:, None], (1, 256))
    return {
        "albedo_add": tint_map + (grain[:, :, None] - 0.5) * 0.1,
        "gap_mask": gap_map,
    }


def pat_logs(c1, c2, s, rough_base, metal_base):
    gx, gy = grid(256, 48, 3)
    n = pfbm(gx, gy, 48, 3, s + 12, 3)
    ridges = 1.0 - np.abs(2.0 * n - 1.0)
    return {
        "height_override": ridges,
        "albedo_mul": (0.7 + 0.3 * ridges)[:, :, None],
    }


def pat_blocks(c1, c2, s, rough_base, metal_base):
    y = np.arange(256, dtype=np.int32)
    x = np.arange(256, dtype=np.int32)
    X, Y = np.meshgrid(x, y)
    row_idx = Y // 64
    x_offset = np.where(row_idx % 2 == 1, 48, 0)
    shifted_X = (X + x_offset) % 256
    col_idx = shifted_X // 96
    groove = (Y % 64 < 3) | (shifted_X % 96 < 3)
    h_tint = ((hash2(row_idx * 10 + col_idx, np.zeros_like(row_idx), s) % 100).astype(np.float32) / 600.0) - 0.08
    return {
        "height_override": np.where(groove, 0.05, 0.65),
        "albedo_mul": np.where(groove[:, :, None], 0.6, 1.0 + h_tint[:, :, None]),
    }


def pat_brick(c1, c2, s, rough_base, metal_base):
    y = np.arange(256, dtype=np.int32)
    x = np.arange(256, dtype=np.int32)
    X, Y = np.meshgrid(x, y)
    row_idx = Y // 16
    x_offset = np.where(row_idx % 2 == 1, 16, 0)
    shifted_X = (X + x_offset) % 256
    mortar = (Y % 16 < 2) | (shifted_X % 32 < 2)
    mortar_col = np.array([170, 165, 155], dtype=np.float32) / 255.0
    return {
        "height_override": np.where(mortar, 0.1, 0.6),
        "mortar_mask": mortar,
        "mortar_color": mortar_col,
    }


def pat_panels(c1, c2, s, rough_base, metal_base):
    y = np.arange(256, dtype=np.int32)
    x = np.arange(256, dtype=np.int32)
    X, Y = np.meshgrid(x, y)
    seam = (X % 128 < 2) | (Y % 128 < 2)
    return {
        "height_override": np.where(seam, 0.2, 0.6),
        "albedo_mul": np.where(seam[:, :, None], 0.7, 1.0),
    }


def pat_window(c1, c2, s, rough_base, metal_base):
    y = np.arange(256, dtype=np.float32) / 255.0
    y_grid = np.tile(y[:, None, None], (1, 256, 1))
    albedo = c1[None, None, :] * (1.0 - y_grid) + c2[None, None, :] * y_grid
    X, Y = np.meshgrid(np.arange(256), np.arange(256))
    frame = (X % 64 < 4) | (Y % 64 < 4)
    streak = (np.abs((X + Y) - 256) < 8)
    rough = np.where(frame, 0.5, np.where(streak, 0.02, 0.05))
    frame_col = np.array([30, 30, 32], dtype=np.float32) / 255.0
    albedo[frame] = frame_col
    return {
        "albedo_override": albedo,
        "height_override": np.where(frame, 0.8, 0.3),
        "rough_override": rough,
    }


def pat_scales(c1, c2, s, rough_base, metal_base):
    y = np.arange(256, dtype=np.int32)
    x = np.arange(256, dtype=np.int32)
    X, Y = np.meshgrid(x, y)
    row = Y // 16
    x_off = np.where(row % 2 == 1, 12, 0)
    ramp = (Y % 16).astype(np.float32) / 16.0
    return {"height_override": ramp * 0.7 + 0.1}


def pat_seams(c1, c2, s, rough_base, metal_base):
    x = np.arange(256, dtype=np.int32)
    X, _ = np.meshgrid(x, x)
    seam = (X % 32 < 6)
    edge = (X % 32 == 0) | (X % 32 == 5)
    return {
        "height_override": np.where(seam, 0.7, 0.4),
        "albedo_add": np.where(edge[:, :, None], 0.2, 0.0),
    }


def pat_tread(c1, c2, s, rough_base, metal_base):
    y = np.arange(256, dtype=np.int32)
    _, Y = np.meshgrid(y, y)
    cleat = (Y % 16 < 8)
    return {
        "height_override": np.where(cleat, 0.7, 0.3),
        "albedo_mul": np.where(cleat[:, :, None], 1.1, 0.8),
    }


def pat_hazard(c1, c2, s, rough_base, metal_base):
    i = np.arange(256, dtype=np.int32)
    X, Y = np.meshgrid(i, i)
    parity = ((X + Y) // 32) % 2
    albedo = np.where(parity[:, :, None] == 0, c1, c2)
    boundary = (((X + Y) % 32 == 0) | ((X + Y) % 32 == 31))
    albedo[boundary] = np.array([10, 10, 10], dtype=np.float32) / 255.0
    return {
        "albedo_override": albedo,
        "height_override": np.full((256, 256), 0.5, dtype=np.float32),
    }


def pat_glow(c1, c2, s, rough_base, metal_base):
    i = (np.arange(256, dtype=np.float32) - 127.5) / 128.0
    X, Y = np.meshgrid(i, i)
    r = np.clip(np.sqrt(X * X + Y * Y), 0.0, 1.0)
    albedo = c1 * (1.0 - r[:, :, None]) + c2 * r[:, :, None]
    return {
        "albedo_override": albedo,
        "height_override": np.full((256, 256), 0.5, dtype=np.float32),
        "rough_override": np.full((256, 256), 0.3, dtype=np.float32),
        "emis_override": np.ones((256, 256), dtype=np.float32),
    }


def pat_solar(c1, c2, s, rough_base, metal_base):
    x = np.arange(256, dtype=np.int32)
    X, Y = np.meshgrid(x, x)
    grid_gap = (X % 32 < 2) | (Y % 32 < 2)
    gap_col = np.array([170, 175, 180], dtype=np.float32) / 255.0
    albedo = np.where(grid_gap[:, :, None], gap_col, c1)
    metal = np.where(grid_gap, 0.5, metal_base)
    return {
        "albedo_override": albedo,
        "metal_override": metal,
        "height_override": np.where(grid_gap, 0.4, 0.5),
    }


def pat_leaves(c1, c2, s, rough_base, metal_base):
    lx, ly = grid(256, 12, 12)
    blotches = pfbm(lx, ly, 12, 12, s + 15, 3)
    vx, vy = grid(256, 20, 20)
    veins = 1.0 - np.abs(2.0 * pfbm(vx, vy, 20, 20, s + 16, 3) - 1.0)
    return {
        "height_override": blotches * 0.7 + veins * 0.3,
        "albedo_mul": (0.8 + 0.2 * blotches - 0.2 * veins)[:, :, None],
    }


def pat_bark(c1, c2, s, rough_base, metal_base):
    gx, gy = grid(256, 4, 40)
    n = pfbm(gx, gy, 4, 40, s + 17, 4)
    grooves = 1.0 - np.abs(2.0 * n - 1.0)
    return {
        "height_override": grooves,
        "albedo_mul": (0.5 + 0.5 * grooves)[:, :, None],
    }


def pat_streaks(c1, c2, s, rough_base, metal_base):
    gx, gy = grid(256, 64, 4)
    blades = pfbm(gx, gy, 64, 4, s + 18, 3)
    y_grad = np.arange(256, dtype=np.float32)[:, None] / 255.0
    return {
        "height_override": blades,
        "albedo_add": (blades[:, :, None] * 0.2 - y_grad[:, :, None] * 0.1),
    }


def pat_crystal(c1, c2, s, rough_base, metal_base):
    cx, cy = grid(256, 5, 5)
    n = pfbm(cx, cy, 5, 5, s + 19, 3)
    facets = np.floor(n * 6.0) / 6.0
    sx, sy = grid(256, 40, 40)
    sparkle = smoothstep(0.85, 0.95, pfbm(sx, sy, 40, 40, s + 20, 2)) * 0.25
    return {
        "height_override": facets,
        "emis_override": sparkle,
    }
