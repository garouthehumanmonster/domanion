"""layers.py - Compute terrain analysis layers: mean height, slope, surface, distwater, and biome."""

from dataclasses import dataclass
import numpy as np
from .params import (
    WORLD_M, LOGIC_N, Surface, Biome,
    SURF_DEEP_WATER_HM, SURF_SHALLOW_WATER_HM, SURF_BEACH_HM,
    SURF_BEACH_MAX_SLOPE, SURF_CLIFF_MIN_SLOPE, SURF_ROUGH_MIN_SLOPE
)
from .noise import fbm


@dataclass
class Layers:
    hm: np.ndarray             # float32[512, 512] mean height in metres
    slope_max_deg: np.ndarray  # float32[512, 512] max slope in degrees
    surface: np.ndarray        # uint8[512, 512] Surface enum
    slope: np.ndarray          # uint8[512, 512] clamped max slope (0..90)
    distwater: np.ndarray      # uint8[512, 512] Manhattan distance to water in logic cells


def _shift_N(arr):
    res = np.empty_like(arr)
    res[1:, :] = arr[:-1, :]
    res[0, :] = arr[0, :]
    return res


def _shift_S(arr):
    res = np.empty_like(arr)
    res[:-1, :] = arr[1:, :]
    res[-1, :] = arr[-1, :]
    return res


def _shift_W(arr):
    res = np.empty_like(arr)
    res[:, 1:] = arr[:, :-1]
    res[:, 0] = arr[:, 0]
    return res


def _shift_E(arr):
    res = np.empty_like(arr)
    res[:, :-1] = arr[:, 1:]
    res[:, -1] = arr[:, -1]
    return res


def compute_distwater(is_water: np.ndarray) -> np.ndarray:
    """Manhattan distance transform to nearest water cell via iterative propagation."""
    d = np.where(is_water, 0, 255).astype(np.int16)

    for _ in range(255):
        nd = np.minimum(d, _shift_N(d) + 1)
        nd = np.minimum(nd, _shift_S(d) + 1)
        nd = np.minimum(nd, _shift_W(d) + 1)
        nd = np.minimum(nd, _shift_E(d) + 1)
        if np.array_equal(nd, d):
            break
        d = nd

    return np.clip(d, 0, 255).astype(np.uint8)


def compute_layers(h_m: np.ndarray, N: int) -> Layers:
    """Compute 512x512 logic grid layers from N x N heightmap."""
    cell_m = WORLD_M / float(N)
    b = N // LOGIC_N

    # Central difference slope per height sample
    hE = _shift_E(h_m)
    hW = _shift_W(h_m)
    hS = _shift_S(h_m)
    hN = _shift_N(h_m)

    gx = (hE - hW) / (2.0 * cell_m)
    gz = (hS - hN) / (2.0 * cell_m)
    slope_deg = np.degrees(np.arctan(np.sqrt(gx * gx + gz * gz))).astype(np.float32)

    if b == 1:
        hm = h_m.astype(np.float32)
        slope_max_deg = slope_deg
    else:
        hm = h_m.reshape(LOGIC_N, b, LOGIC_N, b).mean(axis=(1, 3)).astype(np.float32)
        slope_max_deg = slope_deg.reshape(LOGIC_N, b, LOGIC_N, b).max(axis=(1, 3)).astype(np.float32)

    # Classify surface per logic cell
    surface = np.full((LOGIC_N, LOGIC_N), Surface.Land, dtype=np.uint8)

    deep_mask = (hm < SURF_DEEP_WATER_HM)
    shallow_mask = (~deep_mask) & (hm < SURF_SHALLOW_WATER_HM)
    beach_mask = (~deep_mask) & (~shallow_mask) & (hm < SURF_BEACH_HM) & (slope_max_deg <= SURF_BEACH_MAX_SLOPE)
    cliff_mask = (~deep_mask) & (~shallow_mask) & (~beach_mask) & (slope_max_deg > SURF_CLIFF_MIN_SLOPE)
    rough_mask = (~deep_mask) & (~shallow_mask) & (~beach_mask) & (~cliff_mask) & (slope_max_deg > SURF_ROUGH_MIN_SLOPE)

    surface[deep_mask] = Surface.DeepWater
    surface[shallow_mask] = Surface.ShallowWater
    surface[beach_mask] = Surface.Beach
    surface[cliff_mask] = Surface.Cliff
    surface[rough_mask] = Surface.Rough

    slope = np.round(np.clip(slope_max_deg, 0.0, 90.0)).astype(np.uint8)

    # Distance to water
    water_mask = (surface == Surface.DeepWater) | (surface == Surface.ShallowWater)
    distwater = compute_distwater(water_mask)

    return Layers(
        hm=hm,
        slope_max_deg=slope_max_deg,
        surface=surface,
        slope=slope,
        distwater=distwater
    )


def compute_biome(h_m: np.ndarray, layers: Layers, seed: int) -> np.ndarray:
    """Compute 512x512 biome classification layer based on temperature and moisture."""
    hm = layers.hm
    rows, cols = np.meshgrid(np.arange(LOGIC_N, dtype=np.float32),
                             np.arange(LOGIC_N, dtype=np.float32),
                             indexing='ij')
    z = (rows + 0.5) / float(LOGIC_N)

    biome = np.zeros((LOGIC_N, LOGIC_N), dtype=np.uint8)

    # Ocean and Beach are determined by surface classification
    water_mask = (layers.surface == Surface.DeepWater) | (layers.surface == Surface.ShallowWater)
    beach_mask = (layers.surface == Surface.Beach)
    biome[water_mask] = Biome.Ocean
    biome[beach_mask] = Biome.Beach

    land_mask = (~water_mask) & (~beach_mask)

    # Climate models
    temp = 1.0 - 0.55 * z - (hm / 900.0) + 0.12 * (fbm(cols / 64.0, rows / 64.0, seed + 20, 4) - 0.5)
    dw_norm = np.clip(layers.distwater.astype(np.float32) / 60.0, 0.0, 1.0)
    moist = 0.5 * fbm(cols / 48.0, rows / 48.0, seed + 21, 5) + 0.5 * (1.0 - dw_norm)

    rock_mask = land_mask & ((hm > 280.0) | (layers.slope_max_deg > 35.0)) # Note: adjusted from 260.0 to 280.0 (see docs/spec_deviations.md)
    tundra_mask = land_mask & (~rock_mask) & (temp < 0.28)

    biome[rock_mask] = Biome.Rock
    biome[tundra_mask] = Biome.Tundra

    rem_mask = land_mask & (~rock_mask) & (~tundra_mask)
    if np.any(rem_mask):
        rem_moist = moist[rem_mask].astype(np.float64)
        p15, p65 = np.quantile(rem_moist, [0.15, 0.65])

        arid_mask = rem_mask & (moist <= p15)
        forest_mask = rem_mask & (moist >= p65)
        grass_mask = rem_mask & (~arid_mask) & (~forest_mask)

        biome[arid_mask] = Biome.Arid
        biome[forest_mask] = Biome.Forest
        biome[grass_mask] = Biome.Grass

    return biome
