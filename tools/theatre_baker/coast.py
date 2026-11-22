"""coast.py - Coastal shaping, amphibious landing ramp carving and landing site selection."""

import numpy as np
from .params import (
    WORLD_M, Surface,
    LANDING_TARGET, LANDING_MIN_DIST_M, LANDING_MAX_SLOPE,
    LANDING_MAX_DISTWATER, LANDING_MAX_H_BEHIND_M
)
from .noise import smoothstep


def _blur1d(arr, axis):
    """1D binomial blur [1, 4, 6, 4, 1] / 16 with edge replication."""
    if axis == 0:
        k0 = np.empty_like(arr)
        k0[2:, :] = arr[:-2, :]
        k0[:2, :] = arr[0, :]

        k1 = np.empty_like(arr)
        k1[1:, :] = arr[:-1, :]
        k1[0, :] = arr[0, :]

        k2 = arr

        k3 = np.empty_like(arr)
        k3[:-1, :] = arr[1:, :]
        k3[-1, :] = arr[-1, :]

        k4 = np.empty_like(arr)
        k4[:-2, :] = arr[2:, :]
        k4[-2:, :] = arr[-1, :]
    else:
        k0 = np.empty_like(arr)
        k0[:, 2:] = arr[:, :-2]
        k0[:, :2] = arr[:, :1]

        k1 = np.empty_like(arr)
        k1[:, 1:] = arr[:, :-1]
        k1[:, 0] = arr[:, 0]

        k2 = arr

        k3 = np.empty_like(arr)
        k3[:, :-1] = arr[:, 1:]
        k3[:, -1] = arr[:, -1]

        k4 = np.empty_like(arr)
        k4[:, :-2] = arr[:, 2:]
        k4[:, -2:] = arr[:, -1:]

    return (k0 + 4.0 * k1 + 6.0 * k2 + 4.0 * k3 + k4) * (1.0 / 16.0)


def shape_shore(h_m: np.ndarray) -> np.ndarray:
    """Smooth coastal band between -8m and +8m with 4 passes of binomial blur."""
    w = 1.0 - smoothstep(2.0, 8.0, np.abs(h_m))
    hb = h_m.copy()

    for _ in range(4):
        hb = _blur1d(hb, axis=1)
        hb = _blur1d(hb, axis=0)

    h_out = h_m * (1.0 - w) + hb * w
    return h_out.astype(np.float32)


def carve_landing(h_m: np.ndarray, cell_m: float, x_m: float, z_m: float, dir_vec) -> None:
    """Carve a gentle amphibious landing ramp at (x_m, z_m) directed along dir_vec."""
    dx, dz = dir_vec
    N = h_m.shape[0]

    # Bounding window: +- 100 metres
    c_min = max(0, int(np.floor((x_m - 105.0) / cell_m)))
    c_max = min(N, int(np.ceil((x_m + 105.0) / cell_m)) + 1)
    r_min = max(0, int(np.floor((z_m - 105.0) / cell_m)))
    r_max = min(N, int(np.ceil((z_m + 105.0) / cell_m)) + 1)

    if c_min >= c_max or r_min >= r_max:
        return

    cols = np.arange(c_min, c_max, dtype=np.float32) * cell_m
    rows = np.arange(r_min, r_max, dtype=np.float32) * cell_m
    gx, gz = np.meshgrid(cols, rows)

    px = gx - np.float32(x_m)
    pz = gz - np.float32(z_m)

    # Local coordinates: a is seaward (parallel to dir), b is lateral
    a = px * np.float32(dx) + pz * np.float32(dz)
    b = -px * np.float32(dz) + pz * np.float32(dx)

    p_ramp = -0.0875 * a
    wb = (1.0 - smoothstep(16.0, 40.0, np.abs(b))) * (1.0 - smoothstep(60.0, 100.0, np.abs(a)))

    sub_h = h_m[r_min:r_max, c_min:c_max]
    h_m[r_min:r_max, c_min:c_max] = sub_h * (1.0 - wb) + p_ramp * wb


def pick_landings(h_m: np.ndarray, layers, rng) -> list:
    """Select candidate amphibious landing sites along low-slope beaches."""
    N = h_m.shape[0]
    cell_m = WORLD_M / float(N)
    k = max(1, int(round(24.0 / cell_m)))

    def sample_h(x, z):
        ci = np.clip(int(round(x / cell_m)), 0, N - 1)
        ri = np.clip(int(round(z / cell_m)), 0, N - 1)
        return float(h_m[ri, ci])

    # Find logic cells satisfying beach criteria
    match = (layers.surface == Surface.Beach) & \
            (layers.distwater <= LANDING_MAX_DISTWATER) & \
            (layers.slope <= LANDING_MAX_SLOPE)

    cand_rows, cand_cols = np.where(match)
    candidates = []

    for r, c in zip(cand_rows, cand_cols):
        xm = (float(c) + 0.5) * 8.0
        zm = (float(r) + 0.5) * 8.0

        # Estimate seaward direction via negative gradient (downhill toward sea)
        h_left = sample_h(xm - k * cell_m, zm)
        h_right = sample_h(xm + k * cell_m, zm)
        h_top = sample_h(xm, zm - k * cell_m)
        h_bot = sample_h(xm, zm + k * cell_m)

        gx = h_left - h_right
        gz = h_top - h_bot
        length = float(np.sqrt(gx * gx + gz * gz))
        if length < 1e-6:
            continue

        dir_x = gx / length
        dir_z = gz / length

        # Check point 80m behind site inland
        bx = xm - dir_x * 80.0
        bz = zm - dir_z * 80.0
        if bx < 0.0 or bx >= WORLD_M or bz < 0.0 or bz >= WORLD_M:
            continue
        if sample_h(bx, bz) > LANDING_MAX_H_BEHIND_M:
            continue

        candidates.append((xm, zm, (dir_x, dir_z)))

    rng.shuffle(candidates)

    accepted = []
    min_dist_sq = LANDING_MIN_DIST_M * LANDING_MIN_DIST_M

    for cand in candidates:
        xm, zm, dir_vec = cand
        too_close = False
        for ax, az, _ in accepted:
            d2 = (xm - ax) ** 2 + (zm - az) ** 2
            if d2 < min_dist_sq:
                too_close = True
                break
        if not too_close:
            accepted.append(cand)
            if len(accepted) >= LANDING_TARGET:
                break

    return accepted
