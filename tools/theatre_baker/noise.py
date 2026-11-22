"""noise.py - Integer-hash lattice noise and fractal generators for deterministic terrain."""

import numpy as np


def hash2(ix, iy, seed: int):
    """Integer hash taking 2D grid coordinates and a seed, returning uint32 array."""
    with np.errstate(over='ignore'):
        ix_u = (np.asarray(ix, dtype=np.int64) & 0xFFFFFFFF).astype(np.uint32)
        iy_u = (np.asarray(iy, dtype=np.int64) & 0xFFFFFFFF).astype(np.uint32)
        s_u = np.uint32(int(seed) & 0xFFFFFFFF)

        h = (ix_u * np.uint32(0x27D4EB2D)) ^ (iy_u * np.uint32(0x165667B1)) ^ (s_u * np.uint32(0x9E3779B1))
        h ^= (h >> np.uint32(15))
        h *= np.uint32(0x85EBCA6B)
        h ^= (h >> np.uint32(13))
        h *= np.uint32(0xC2B2AE35)
        h ^= (h >> np.uint32(16))
        return h


def value_noise(x, y, seed: int):
    """Continuous 2D value noise in [0, 1) using quintic smoothstep on lattice corners."""
    x_f = np.asarray(x, dtype=np.float32)
    y_f = np.asarray(y, dtype=np.float32)

    x0 = np.floor(x_f).astype(np.int64)
    y0 = np.floor(y_f).astype(np.int64)
    x1 = x0 + 1
    y1 = y0 + 1

    fx = x_f - x0.astype(np.float32)
    fy = y_f - y0.astype(np.float32)

    # Quintic Hermite interpolant: 6t^5 - 15t^4 + 10t^3
    tx = fx * fx * fx * (fx * (fx * 6.0 - 15.0) + 10.0)
    ty = fy * fy * fy * (fy * (fy * 6.0 - 15.0) + 10.0)

    # Sample four lattice corners
    c00 = (hash2(x0, y0, seed).astype(np.float32)) / 4294967296.0
    c10 = (hash2(x1, y0, seed).astype(np.float32)) / 4294967296.0
    c01 = (hash2(x0, y1, seed).astype(np.float32)) / 4294967296.0
    c11 = (hash2(x1, y1, seed).astype(np.float32)) / 4294967296.0

    nx0 = c00 * (1.0 - tx) + c10 * tx
    nx1 = c01 * (1.0 - tx) + c11 * tx
    res = nx0 * (1.0 - ty) + nx1 * ty
    return res.astype(np.float32)


def fbm(x, y, seed: int, octaves: int, lac: float = 2.0, gain: float = 0.5):
    """Fractional Brownian Motion normalizing output to [0, 1]."""
    total = np.zeros_like(x, dtype=np.float32)
    amp = 1.0
    freq = 1.0
    amp_sum = 0.0

    for o in range(octaves):
        n = value_noise(x * freq, y * freq, seed + o * 101)
        total += n * np.float32(amp)
        amp_sum += amp
        amp *= gain
        freq *= lac

    if amp_sum > 0:
        total /= np.float32(amp_sum)
    return total


def ridged(x, y, seed: int, octaves: int, lac: float = 2.0, gain: float = 0.5):
    """Ridged multi-fractal noise normalizing output to [0, 1]."""
    total = np.zeros_like(x, dtype=np.float32)
    amp = 1.0
    freq = 1.0
    amp_sum = 0.0

    for o in range(octaves):
        n = value_noise(x * freq, y * freq, seed + o * 101)
        # Ridge transform: (1 - |2n - 1|)^2
        r = 1.0 - np.abs(2.0 * n - 1.0)
        r = r * r
        total += r * np.float32(amp)
        amp_sum += amp
        amp *= gain
        freq *= lac

    if amp_sum > 0:
        total /= np.float32(amp_sum)
    return total


def smoothstep(e0, e1, x):
    """Standard smoothstep clamping and interpolating in [0, 1]."""
    e0_f = np.float32(e0)
    e1_f = np.float32(e1)
    t = np.clip((x - e0_f) / (e1_f - e0_f), 0.0, 1.0)
    return t * t * (3.0 - 2.0 * t)
