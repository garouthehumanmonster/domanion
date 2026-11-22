"""noise.py - Deterministic periodic and aperiodic value noise / FBM using integer hash."""

import numpy as np


def hash2(ix, iy, seed):
    """Integer hash matching DOMINION engine standard."""
    ix = np.asarray(ix, dtype=np.uint32)
    iy = np.asarray(iy, dtype=np.uint32)
    s = np.uint32(seed & 0xFFFFFFFF)
    with np.errstate(over="ignore"):
        h = (ix * np.uint32(0x27D4EB2D)) ^ (iy * np.uint32(0x165667B1)) ^ (s * np.uint32(0x9E3779B1))
        h ^= h >> np.uint32(15)
        h *= np.uint32(0x85EBCA6B)
        h ^= h >> np.uint32(13)
        h *= np.uint32(0xC2B2AE35)
        h ^= h >> np.uint32(16)
    return h


def pnoise(x, y, px, py, seed):
    """Periodic value noise on integer lattice (px, py) in [0, 1)."""
    x = np.asarray(x, dtype=np.float32)
    y = np.asarray(y, dtype=np.float32)

    x_fl = np.floor(x)
    y_fl = np.floor(y)

    ix0 = x_fl.astype(np.int64) % int(px)
    iy0 = y_fl.astype(np.int64) % int(py)
    ix1 = (ix0 + 1) % int(px)
    iy1 = (iy0 + 1) % int(py)

    fx = x - x_fl
    fy = y - y_fl

    u = fx * fx * fx * (fx * (fx * 6.0 - 15.0) + 10.0)
    v = fy * fy * fy * (fy * (fy * 6.0 - 15.0) + 10.0)

    v00 = hash2(ix0, iy0, seed).astype(np.float32) / 4294967296.0
    v10 = hash2(ix1, iy0, seed).astype(np.float32) / 4294967296.0
    v01 = hash2(ix0, iy1, seed).astype(np.float32) / 4294967296.0
    v11 = hash2(ix1, iy1, seed).astype(np.float32) / 4294967296.0

    nx0 = v00 + u * (v10 - v00)
    nx1 = v01 + u * (v11 - v01)
    val = nx0 + v * (nx1 - nx0)
    return np.clip(val, 0.0, 0.99999994).astype(np.float32)


def pfbm(x, y, px, py, seed, octaves, gain=0.5):
    """Periodic fractional Brownian motion normalized to [0, 1]."""
    x = np.asarray(x, dtype=np.float32)
    y = np.asarray(y, dtype=np.float32)
    val = np.zeros_like(x, dtype=np.float32)
    amp = 1.0
    tot_amp = 0.0
    s = int(seed)

    for o in range(octaves):
        freq = 1 << o
        val += amp * pnoise(x * freq, y * freq, px * freq, py * freq, s + o * 101)
        tot_amp += amp
        amp *= gain

    return val / tot_amp


def afbm(x, y, seed, octaves, gain=0.5):
    """Aperiodic fractional Brownian motion using huge periods."""
    huge = 1 << 20
    return pfbm(x, y, huge, huge, seed, octaves, gain)


def grid(n, fx, fy):
    """Return meshgrid (X, Y) float32 [n, n] with periodic frequencies (fx, fy)."""
    i = (np.arange(n, dtype=np.float32) + 0.5) / float(n) * float(fx)
    j = (np.arange(n, dtype=np.float32) + 0.5) / float(n) * float(fy)
    X, Y = np.meshgrid(i, j)
    return X.astype(np.float32), Y.astype(np.float32)


def smoothstep(e0, e1, x):
    """Hermite smoothstep interpolation."""
    t = np.clip((x - e0) / (e1 - e0), 0.0, 1.0)
    return t * t * (3.0 - 2.0 * t)
