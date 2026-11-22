"""vfx_effects.py - Procedural flipbook frame renderers for 12 VFX effects."""

import os
import sys
import numpy as np

sys.path.insert(0, os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

from asset_common.noise import afbm, smoothstep
from asset_common.hashing import fnv1a64
from asset_common.rng import Rng

EFFECTS = [
    (0, "fire_explosion", "once"),
    (1, "smoke_puff", "once"),
    (2, "muzzle_flash", "once"),
    (3, "dust_cloud", "once"),
    (4, "spark_burst", "once"),
    (5, "shockwave_ring", "once"),
    (6, "fire_loop", "loop"),
    (7, "smoke_loop", "loop"),
    (8, "laser_impact", "once"),
    (9, "emp_pulse", "once"),
    (10, "debris_chunks", "once"),
    (11, "nuke_mushroom", "once"),
]

N = 128
_coords = ((np.arange(N, dtype=np.float32) + 0.5) / float(N)) * 2.0 - 1.0
GRID_X, GRID_Y = np.meshgrid(_coords, _coords)
GRID_R = np.sqrt(GRID_X * GRID_X + GRID_Y * GRID_Y)
GRID_A = np.arctan2(GRID_Y, GRID_X)
EDGE_MASK = 1.0 - smoothstep(0.80, 0.96, np.maximum(np.abs(GRID_X), np.abs(GRID_Y)))


def render_frame(effect_id: int, t: float) -> np.ndarray:
    """Render a single 128x128 premultiplied RGBA frame for the given effect and normalized time t."""
    if effect_id < 0 or effect_id >= 12:
        return np.zeros((128, 128, 4), dtype=np.float32)

    _, name, kind = EFFECTS[effect_id]
    s = fnv1a64(name) & 0xFFFFFFFF
    t = float(np.clip(t, 0.0, 1.0))

    X, Y, R, A = GRID_X, GRID_Y, GRID_R, GRID_A

    if effect_id == 0:  # fire_explosion
        fade = float((1.0 - t) ** 1.3)
        r0 = 0.12 + 0.60 * float(np.sqrt(t))
        w = afbm(X * 3.5, Y * 3.5, s, 3)
        d = R - (r0 + 0.12 * (w - 0.5))
        alpha = np.clip(1.0 - d / 0.12, 0.0, 1.0) * fade * 0.95
        c_core = np.array([1.0, 0.95, 0.6], dtype=np.float32)
        c_mid = np.array([1.0, 0.45, 0.08], dtype=np.float32)
        c_edge = np.array([0.45, 0.08, 0.02], dtype=np.float32)
        r_norm = np.clip(R / max(1e-4, r0), 0.0, 1.0)[:, :, None]
        color = np.where(r_norm < 0.4, c_core * (1.0 - r_norm / 0.4) + c_mid * (r_norm / 0.4),
                         c_mid * (1.0 - (r_norm - 0.4) / 0.6) + c_edge * ((r_norm - 0.4) / 0.6))

    elif effect_id == 1:  # smoke_puff
        fade = float((1.0 - t) ** 1.1)
        r0 = 0.12 + 0.38 * t
        w = afbm(X * 4.0, Y * 4.0, s, 3)
        d = R - (r0 + 0.10 * (w - 0.5))
        alpha = np.clip(1.0 - d / 0.12, 0.0, 1.0) * fade * 0.85
        color = np.full((128, 128, 3), [0.38, 0.38, 0.40], dtype=np.float32)


    elif effect_id == 2:  # muzzle_flash
        fade = float(max(0.0, 1.0 - t / 0.5) ** 2.0)
        spikes = np.maximum(0.0, np.cos(6.0 * A)) ** 3.0
        r_star = 0.10 + 0.45 * spikes
        alpha = np.clip(1.0 - (R - r_star) / 0.06, 0.0, 1.0) * fade
        color = np.full((128, 128, 3), [1.0, 0.95, 0.5], dtype=np.float32)

    elif effect_id == 3:  # dust_cloud
        fade = float((1.0 - t) ** 1.2)
        r_ell = np.sqrt(X * X + (Y / 0.55) ** 2.0)
        r0 = 0.15 + 0.45 * t
        w = afbm(X * 3.0, Y * 3.0, s, 3)
        alpha = np.clip(1.0 - (r_ell - r0 + 0.10 * (w - 0.5)) / 0.15, 0.0, 1.0) * fade * 0.85
        color = np.full((128, 128, 3), [0.72, 0.62, 0.48], dtype=np.float32)

    elif effect_id == 4:  # spark_burst
        fade = float((1.0 - t) ** 1.3)
        rng = Rng(s)
        # Center flash decaying in first few frames
        flash = np.clip(1.0 - R / 0.35, 0.0, 1.0) * float(max(0.0, 1.0 - t * 2.0))
        alpha = flash
        for _ in range(24):
            ang = rng.uniform() * (2.0 * np.pi)
            spd = 0.45 + 0.40 * rng.uniform()
            cx = spd * t * np.cos(ang)
            cy = spd * t * np.sin(ang)
            # Streak from previous position to current
            tail_t = max(0.0, t - 0.25)
            tx = spd * tail_t * np.cos(ang)
            ty = spd * tail_t * np.sin(ang)
            # Distance to segment
            vx, vy = cx - tx, cy - ty
            vlen2 = vx * vx + vy * vy
            if vlen2 > 1e-6:
                proj = np.clip(((X - tx) * vx + (Y - ty) * vy) / vlen2, 0.0, 1.0)
                px = tx + proj * vx
                py = ty + proj * vy
                d2 = (X - px) ** 2.0 + (Y - py) ** 2.0
            else:
                d2 = (X - cx) ** 2.0 + (Y - cy) ** 2.0
            alpha = np.maximum(alpha, np.clip(1.0 - np.sqrt(d2) / 0.055, 0.0, 1.0))
        alpha = alpha * fade * 0.95
        color = np.full((128, 128, 3), [1.0, 0.85, 0.3], dtype=np.float32)


    elif effect_id == 5:  # shockwave_ring
        fade = float((1.0 - t) ** 1.2)
        r_ring = 0.08 + 0.60 * t
        width = 0.08 + 0.10 * t
        dr = np.abs(R - r_ring)
        ring = np.clip(1.0 - dr / width, 0.0, 1.0)
        wash = np.clip(1.0 - R / max(1e-4, r_ring), 0.0, 1.0) * 0.25
        alpha = np.maximum(ring, wash) * fade * 0.95
        color = np.full((128, 128, 3), [0.85, 0.92, 1.0], dtype=np.float32)

    elif effect_id == 6:  # fire_loop
        # Crossfade trick: (1-t)*P(t) + t*P(t-1)
        def flame_p(st):
            y_scrolled = Y + st * 2.5
            w = afbm(X * 4.0, y_scrolled * 4.0, s, 3)
            # Column narrowing toward top (Y < 0)
            col_w = np.clip(0.40 * (0.8 - Y * 0.5), 0.05, 0.6)
            d = np.abs(X) - col_w * (0.6 + 0.5 * w)
            a = np.clip(1.0 - d / 0.08, 0.0, 1.0) * np.clip(1.2 - Y, 0.0, 1.0) * np.clip(Y + 0.8, 0.0, 1.0)
            return a

        a0 = flame_p(t)
        a1 = flame_p(t - 1.0)
        alpha = ((1.0 - t) * a0 + t * a1) * 0.95
        c_core = np.array([1.0, 0.9, 0.4], dtype=np.float32)
        c_edge = np.array([0.9, 0.25, 0.02], dtype=np.float32)
        x_norm = np.clip(np.abs(X) / 0.3, 0.0, 1.0)[:, :, None]
        color = c_core * (1.0 - x_norm) + c_edge * x_norm

    elif effect_id == 7:  # smoke_loop
        def smoke_p(st):
            y_scrolled = Y + st * 1.8
            w = afbm(X * 3.5, y_scrolled * 3.5, s, 3)
            col_w = np.clip(0.35 * (1.0 - Y * 0.3), 0.1, 0.6)
            d = np.abs(X) - col_w * (0.6 + 0.5 * w)
            a = np.clip(1.0 - d / 0.12, 0.0, 1.0) * np.clip(1.1 - Y, 0.0, 1.0) * np.clip(Y + 0.9, 0.0, 1.0)
            return a

        a0 = smoke_p(t)
        a1 = smoke_p(t - 1.0)
        alpha = ((1.0 - t) * a0 + t * a1) * 0.80
        color = np.full((128, 128, 3), [0.22, 0.22, 0.24], dtype=np.float32)

    elif effect_id == 8:  # laser_impact
        fade = float((1.0 - t) ** 1.1)
        r_core = 0.18 * (1.0 - t)
        core = np.clip(1.0 - R / max(1e-4, r_core), 0.0, 1.0)
        glow = np.clip(1.0 / (1.0 + (R / 0.28) ** 2.0) - 0.15, 0.0, 1.0) * 0.65 * float(max(0.0, 1.0 - t * 2.0))
        ring_r = 0.08 + 0.65 * t
        ring = np.clip(1.0 - np.abs(R - ring_r) / (0.09 + 0.05 * t), 0.0, 1.0) * 0.85
        alpha = np.maximum(np.maximum(core, glow), ring) * fade * 0.95
        color = np.full((128, 128, 3), [0.4, 0.9, 1.0], dtype=np.float32)



    elif effect_id == 9:  # emp_pulse
        fade = float((1.0 - t) ** 1.3)
        r_ring = 0.08 + 0.55 * t
        width = 0.08 + 0.08 * t
        w = afbm(X * 8.0, Y * 8.0, s, 3)
        dr = np.abs(R - r_ring + 0.06 * (w - 0.5))
        ring = np.clip(1.0 - dr / width, 0.0, 1.0)
        glow = np.clip(1.0 - R / max(1e-4, r_ring), 0.0, 1.0) * 0.30
        alpha = np.maximum(ring, glow) * fade * 0.92
        color = np.full((128, 128, 3), [0.35, 0.65, 1.0], dtype=np.float32)

    elif effect_id == 10:  # debris_chunks
        fade = float((1.0 - t) ** 1.1)
        r_dust = 0.12 + 0.45 * t
        w = afbm(X * 3.0, Y * 3.0, s, 3)
        dust = np.clip(1.0 - (R - r_dust + 0.10 * (w - 0.5)) / 0.15, 0.0, 1.0) * 0.45
        alpha = dust
        rng = Rng(s)
        for _ in range(12):
            vx = (rng.uniform() * 2.0 - 1.0) * 0.65
            vy = -rng.uniform() * 0.7 - 0.1
            cx = vx * t
            cy = vy * t + 0.5 * 1.2 * (t * t)  # Gravity
            dx = np.abs(X - cx)
            dy = np.abs(Y - cy)
            box = np.maximum(dx, dy)
            alpha = np.maximum(alpha, np.clip(1.0 - box / 0.05, 0.0, 1.0))
        alpha = alpha * fade * 0.90
        color = np.full((128, 128, 3), [0.30, 0.26, 0.22], dtype=np.float32)




    elif effect_id == 11:  # nuke_mushroom
        fade = float(1.0 - t * t)
        cap_y = -0.15 - 0.15 * t
        r_cap = 0.15 + 0.40 * t
        d_cap = np.sqrt(X * X + ((Y - cap_y) / 0.6) ** 2.0)
        w_cap = afbm(X * 4.0, (Y - cap_y) * 4.0, s, 3)
        cap = np.clip(1.0 - (d_cap - r_cap + 0.08 * (w_cap - 0.5)) / 0.08, 0.0, 1.0)

        # Stem
        stem_w = 0.12 * (1.0 - t * 0.3)
        stem = np.clip(1.0 - np.abs(X) / stem_w, 0.0, 1.0) * np.clip((Y - cap_y) / 0.1, 0.0, 1.0) * np.clip(0.8 - Y, 0.0, 1.0)

        alpha = np.maximum(cap, stem * 0.8) * fade * 0.95
        c_cap = np.array([1.0, 0.6, 0.1], dtype=np.float32)
        c_stem = np.array([0.7, 0.3, 0.05], dtype=np.float32)
        color = np.where(Y[:, :, None] < cap_y + 0.1, c_cap, c_stem)

    else:
        alpha = np.zeros((128, 128), dtype=np.float32)
        color = np.zeros((128, 128, 3), dtype=np.float32)

    # Edge clipping & forcing outermost 2 px to 0
    alpha = alpha * EDGE_MASK
    alpha[:2, :] = 0.0
    alpha[-2:, :] = 0.0
    alpha[:, :2] = 0.0
    alpha[:, -2:] = 0.0

    # If t >= 1.0 on one-shot, force exactly 0
    if kind == "once" and t >= 0.9999:
        alpha[:, :] = 0.0

    alpha = np.clip(alpha, 0.0, 1.0)
    color = np.clip(color, 0.0, 1.0)

    # Premultiply: rgb = color * alpha
    rgb = color * alpha[:, :, None]
    rgb = np.minimum(rgb, alpha[:, :, None])  # enforce rgb <= alpha

    frame = np.concatenate([rgb, alpha[:, :, None]], axis=-1).astype(np.float32)
    return frame
