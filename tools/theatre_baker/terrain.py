"""terrain.py - Procedural base continental landmass generator with quantile land fraction."""

import numpy as np
from .params import (
    WORLD_M, LAND_FRACTION, MAX_LAND_M
)
from .noise import fbm, ridged, smoothstep


def make_heights(N: int, seed: int) -> np.ndarray:
    """Generate base continental heightmap in metres with exact quantile land fraction."""
    # Normalized coordinates u (x, east) and v (z, south)
    coords = (np.arange(N, dtype=np.float32) + 0.5) / np.float32(N)
    u, v = np.meshgrid(coords, coords) # u is x, v is z

    # Domain warping
    wu = u + 0.18 * (fbm(u * 3.0, v * 3.0, seed + 1, 4) - 0.5)
    wv = v + 0.18 * (fbm(u * 3.0 + 17.0, v * 3.0 + 31.0, seed + 2, 4) - 0.5)

    # Elliptical distance from continent centre
    d = np.sqrt(((wu - 0.5) / 0.40) ** 2 + ((wv - 0.5) / 0.34) ** 2)
    mask = 1.0 - smoothstep(0.55, 1.0, d)

    # Terrain layers
    base = fbm(wu * 4.0, wv * 4.0, seed + 10, 6)
    mnt = ridged(wu * 6.0, wv * 6.0, seed + 11, 5)
    interior = smoothstep(0.15, 0.85, mask)

    raw = 0.55 * mask + 0.25 * base + 0.20 * mnt * interior
    raw_min = raw.min()
    raw_max = raw.max()
    raw = (raw - raw_min) / (raw_max - raw_min)

    # Quantile threshold for exact land fraction
    t = float(np.quantile(raw.astype(np.float64), 1.0 - LAND_FRACTION))

    land_mask = (raw >= t)
    h = np.empty_like(raw, dtype=np.float32)

    # Land formula
    uu = (raw - t) / (1.0 - t + 1e-12)
    h_land = MAX_LAND_M * (np.maximum(0.0, uu) ** 1.4)
    h[land_mask] = h_land[land_mask]

    # Sea formula
    vv = (t - raw) / (t + 1e-12)
    depth = 20.0 * vv + 90.0 * smoothstep(0.35, 0.80, vv)
    sea_noise = (fbm(u * 40.0, v * 40.0, seed + 12, 3) - 0.5) * 8.0 * vv
    h_sea = -depth + sea_noise
    h[~land_mask] = h_sea[~land_mask]

    # Final bounds clipping and non-negative land guarantee
    h = np.clip(h, -120.0, 440.0)
    h[land_mask] = np.maximum(h[land_mask], 0.0)

    return h.astype(np.float32)
