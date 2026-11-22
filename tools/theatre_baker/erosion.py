"""erosion.py - Deterministic pipe-model hydraulic erosion and mass-conserving thermal relaxation."""

import numpy as np
from .params import (
    KC, KS, KD, RAIN_PER_ITER, EVAP,
    DT, G, PIPE_A, FLUX_DAMP, DMAX, SIN_MIN, EPS, UMAX, EROSION_CLAMP_M,
    COARSE_ITERS, FINE_ITERS, THERMAL_ITERS, THERMAL_RATE
)


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


def erode(h_m: np.ndarray, cell_m: float, iters: int, seed: int = 0) -> np.ndarray:
    """Pipe-model hydraulic erosion on 2D heightmap in cell units."""
    b = (h_m / cell_m).astype(np.float32)
    land = (h_m > 0.0)

    N = b.shape[0]
    d = np.zeros_like(b)
    s = np.zeros_like(b)
    fN = np.zeros_like(b)
    fS = np.zeros_like(b)
    fE = np.zeros_like(b)
    fW = np.zeros_like(b)

    grid_z, grid_x = np.meshgrid(np.arange(N, dtype=np.float32),
                                 np.arange(N, dtype=np.float32),
                                 indexing='ij')

    dt_pipe_g = DT * PIPE_A * G

    for _ in range(iters):
        # 1. Rain and sea drain
        d += RAIN_PER_ITER
        d[b <= 0.0] = 0.0
        s[b <= 0.0] = 0.0

        # 2. Water surface and directional flux
        w = b + d
        wN = _shift_N(w)
        wS = _shift_S(w)
        wW = _shift_W(w)
        wE = _shift_E(w)

        fN = np.maximum(0.0, fN * FLUX_DAMP + dt_pipe_g * (w - wN))
        fS = np.maximum(0.0, fS * FLUX_DAMP + dt_pipe_g * (w - wS))
        fE = np.maximum(0.0, fE * FLUX_DAMP + dt_pipe_g * (w - wE))
        fW = np.maximum(0.0, fW * FLUX_DAMP + dt_pipe_g * (w - wW))

        # 3. Flux scaling to available water and border boundary condition
        tot = fN + fS + fE + fW
        K = np.minimum(1.0, d / (tot * DT + 1e-9))
        fN *= K
        fS *= K
        fE *= K
        fW *= K

        fN[0, :] = 0.0
        fS[-1, :] = 0.0
        fW[:, 0] = 0.0
        fE[:, -1] = 0.0

        # 4. Inflow and new water depth
        inflow = np.zeros_like(d)
        inflow[1:, :] += fS[:-1, :]
        inflow[:-1, :] += fN[1:, :]
        inflow[:, 1:] += fE[:, :-1]
        inflow[:, :-1] += fW[:, 1:]

        d2 = np.maximum(0.0, d + DT * (inflow - tot))
        dbar = 0.5 * (d + d2)

        # 5. Velocity field
        fE_W = np.zeros_like(fE)
        fE_W[:, 1:] = fE[:, :-1]
        fW_E = np.zeros_like(fW)
        fW_E[:, :-1] = fW[:, 1:]
        dWx = 0.5 * (fE_W - fW + fE - fW_E)

        fS_N = np.zeros_like(fS)
        fS_N[1:, :] = fS[:-1, :]
        fN_S = np.zeros_like(fN)
        fN_S[:-1, :] = fN[1:, :]
        dWz = 0.5 * (fS_N - fN + fS - fN_S)

        ux = np.clip(dWx / (dbar + EPS), -UMAX, UMAX)
        uz = np.clip(dWz / (dbar + EPS), -UMAX, UMAX)
        speed = np.sqrt(ux * ux + uz * uz)

        # 6. Terrain slope
        bE = _shift_E(b)
        bW = _shift_W(b)
        gx = 0.5 * (bE - bW)

        bS = _shift_S(b)
        bN = _shift_N(b)
        gz = 0.5 * (bS - bN)

        g2 = gx * gx + gz * gz
        sin_a = np.sqrt(g2) / np.sqrt(1.0 + g2)

        # 7. Sediment capacity and transport
        C = KC * np.maximum(sin_a, SIN_MIN) * np.minimum(1.0, speed * DT) * np.minimum(1.0, d2 / DMAX)
        diff = C - s
        ero = np.where(diff > 0.0, KS * diff, 0.0)
        dep = np.where(diff < 0.0, KD * (-diff), 0.0)
        ero[~land] = 0.0
        dep[~land] = 0.0

        b = b - ero + dep
        s = s + ero - dep

        # 8. Semi-Lagrangian sediment advection
        src_x = np.clip(grid_x - ux * DT, 0.0, N - 1.0)
        src_z = np.clip(grid_z - uz * DT, 0.0, N - 1.0)

        x0 = np.floor(src_x).astype(np.int32)
        z0 = np.floor(src_z).astype(np.int32)
        x1 = np.minimum(x0 + 1, N - 1)
        z1 = np.minimum(z0 + 1, N - 1)
        fx = src_x - x0.astype(np.float32)
        fz = src_z - z0.astype(np.float32)

        s00 = s[z0, x0]
        s10 = s[z0, x1]
        s01 = s[z1, x0]
        s11 = s[z1, x1]
        s = (s00 * (1.0 - fx) + s10 * fx) * (1.0 - fz) + (s01 * (1.0 - fx) + s11 * fx) * fz

        # 9. Evaporation and drain
        d = d2 * EVAP
        d[b <= 0.0] = 0.0
        s[b <= 0.0] = 0.0

    h_new = b * cell_m
    # Strict clamping on land; non-land is bit-identical copy
    h_clamped = np.clip(h_new, np.maximum(0.0, h_m - EROSION_CLAMP_M), h_m + EROSION_CLAMP_M)
    h_res = np.empty_like(h_m)
    h_res[land] = h_clamped[land]
    h_res[~land] = h_m[~land]
    return h_res.astype(np.float32)


def erode_multiscale(h_m: np.ndarray, N: int, seed: int = 0) -> np.ndarray:
    """Multi-scale erosion pipeline running coarse pass at 512 followed by fine pass."""
    cell_m = 4096.0 / float(N)
    factor = N // 512

    if factor == 1:
        h512 = h_m.copy()
    else:
        h512 = h_m.reshape(512, factor, 512, factor).mean(axis=(1, 3))

    e512 = erode(h512, 8.0, COARSE_ITERS, seed)
    delta512 = e512 - h512

    if factor > 1:
        # Bilinear upsample of delta
        coord = (np.arange(N, dtype=np.float32) + 0.5) / float(factor) - 0.5
        c0 = np.clip(np.floor(coord).astype(np.int32), 0, 511)
        c1 = np.clip(c0 + 1, 0, 511)
        f = np.clip(coord - c0.astype(np.float32), 0.0, 1.0)

        # Horizontal pass
        d_x = delta512[:, c0] * (1.0 - f) + delta512[:, c1] * f
        # Vertical pass
        delta_up = d_x[c0, :] * (1.0 - f[:, None]) + d_x[c1, :] * f[:, None]

        h = h_m + delta_up
        land = (h_m > 0.0)
        h[land] = np.maximum(h[land], 0.0)
        h[~land] = h_m[~land]

        h = erode(h, cell_m, FINE_ITERS, seed)
    else:
        h = h_m + delta512
        land = (h_m > 0.0)
        h[land] = np.maximum(h[land], 0.0)
        h[~land] = h_m[~land]

    return h.astype(np.float32)


def thermal(h_m: np.ndarray, cell_m: float, iters: int = THERMAL_ITERS) -> np.ndarray:
    """Jacobi mass-conserving thermal erosion relaxation."""
    h = h_m.copy().astype(np.float64)
    talus = 0.8 * float(cell_m)
    rate = float(THERMAL_RATE)
    land = (h > 0.0)

    for _ in range(iters):
        hN = _shift_N(h)
        hS = _shift_S(h)
        hW = _shift_W(h)
        hE = _shift_E(h)

        landN = np.zeros_like(land)
        landN[1:, :] = land[:-1, :]

        landS = np.zeros_like(land)
        landS[:-1, :] = land[1:, :]

        landW = np.zeros_like(land)
        landW[:, 1:] = land[:, :-1]

        landE = np.zeros_like(land)
        landE[:, :-1] = land[:, 1:]

        maskN = land & landN
        maskS = land & landS
        maskW = land & landW
        maskE = land & landE

        outN = np.where(maskN, rate * np.maximum(0.0, h - hN - talus), 0.0)
        outS = np.where(maskS, rate * np.maximum(0.0, h - hS - talus), 0.0)
        outW = np.where(maskW, rate * np.maximum(0.0, h - hW - talus), 0.0)
        outE = np.where(maskE, rate * np.maximum(0.0, h - hE - talus), 0.0)

        inN = np.zeros_like(h)
        inN[1:, :] = outS[:-1, :]

        inS = np.zeros_like(h)
        inS[:-1, :] = outN[1:, :]

        inW = np.zeros_like(h)
        inW[:, 1:] = outE[:, :-1]

        inE = np.zeros_like(h)
        inE[:, :-1] = outW[:, 1:]

        tot_out = outN + outS + outW + outE
        tot_in = inN + inS + inW + inE

        h = h - tot_out + tot_in
        h[~land] = h_m[~land]

    return h.astype(np.float32)
