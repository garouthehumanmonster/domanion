"""placement.py - Player start locations, plateau flattening, and resource node placement."""

import math
from collections import deque
import numpy as np
from .params import (
    WORLD_M, LOGIC_N, Surface, Biome, NodeKind,
    START_MIN_SLOPE, START_MIN_DISTWATER, START_MIN_HM, START_MAX_HM,
    START_MARGIN_CELLS, START_MIN_DIST_M,
    PLATEAU_FLAT_RADIUS_M, PLATEAU_WINDOW_RADIUS_M, PLATEAU_MEDIAN_RADIUS_M,
    KIT_RESOURCES, NEUTRAL_RESOURCES, MAX_TOTAL_RESOURCES
)
from .tformat import m_to_code, code_to_m, q16
from .noise import smoothstep


class PlacementError(Exception):
    """Raised when placement constraints cannot be satisfied."""
    pass


def candidate_starts(layers):
    """Identify candidate player start locations inside the primary connected landmass."""
    passable = (layers.surface == Surface.Beach) | \
               (layers.surface == Surface.Land) | \
               (layers.surface == Surface.Rough)

    tot_passable = int(np.sum(passable))
    if tot_passable == 0:
        raise PlacementError("no passable land cells found")

    p_rows, p_cols = np.where(passable)
    cz = float(np.mean(p_rows)) * 8.0 + 4.0
    cx = float(np.mean(p_cols)) * 8.0 + 4.0

    # Initial start criteria
    valid_cands = (layers.surface == Surface.Land) & \
                  (layers.slope_max_deg <= START_MIN_SLOPE) & \
                  (layers.distwater >= START_MIN_DISTWATER) & \
                  (layers.hm >= START_MIN_HM) & (layers.hm <= START_MAX_HM)

    # Exclude border margins
    valid_cands[:START_MARGIN_CELLS, :] = False
    valid_cands[-START_MARGIN_CELLS:, :] = False
    valid_cands[:, :START_MARGIN_CELLS] = False
    valid_cands[:, -START_MARGIN_CELLS:] = False

    cand_rows, cand_cols = np.where(valid_cands)
    if len(cand_rows) == 0:
        raise PlacementError("no candidate start cells matching criteria")

    # Candidate nearest to centroid
    d2_centroid = (cand_cols * 8.0 + 4.0 - cx) ** 2 + (cand_rows * 8.0 + 4.0 - cz) ** 2
    best_cand_idx = int(np.argmin(d2_centroid))
    seed_r = int(cand_rows[best_cand_idx])
    seed_c = int(cand_cols[best_cand_idx])

    # 4-connected BFS flood fill across passable cells
    visited = np.zeros((LOGIC_N, LOGIC_N), dtype=bool)
    queue = deque([(seed_r, seed_c)])
    visited[seed_r, seed_c] = True
    comp_size = 0

    while queue:
        r, c = queue.popleft()
        comp_size += 1
        for dr, dc in ((-1, 0), (1, 0), (0, -1), (0, 1)):
            nr, nc = r + dr, c + dc
            if 0 <= nr < LOGIC_N and 0 <= nc < LOGIC_N:
                if passable[nr, nc] and not visited[nr, nc]:
                    visited[nr, nc] = True
                    queue.append((nr, nc))

    if comp_size < 0.60 * tot_passable:
        raise PlacementError("main landmass too small")

    # Retain only candidates reached by flood fill
    cands = []
    for r, c in zip(cand_rows, cand_cols):
        if visited[r, c]:
            cands.append(((float(c) + 0.5) * 8.0, (float(r) + 0.5) * 8.0))

    return cands, (cx, cz)


def pick_starts(cands: list, centroid: tuple, rng) -> list:
    """Select 8 starts using farthest-point sampling and sort them counter-clockwise."""
    if len(cands) < 64:
        raise PlacementError("insufficient start candidates")

    cx, cz = centroid

    # First start: farthest from centroid
    d2_c = [(x - cx) ** 2 + (z - cz) ** 2 for x, z in cands]
    first_idx = int(np.argmax(d2_c))
    chosen = [cands[first_idx]]

    # 7 more by farthest-point sampling
    min_d2 = [ (x - chosen[0][0]) ** 2 + (z - chosen[0][1]) ** 2 for x, z in cands ]

    for _ in range(7):
        best_cand_idx = int(np.argmax(min_d2))
        pt = cands[best_cand_idx]
        chosen.append(pt)
        for i, (x, z) in enumerate(cands):
            d2 = (x - pt[0]) ** 2 + (z - pt[1]) ** 2
            if d2 < min_d2[i]:
                min_d2[i] = d2

    # Verify pairwise distance
    min_sq = START_MIN_DIST_M * START_MIN_DIST_M
    for i in range(8):
        for j in range(i + 1, 8):
            d2 = (chosen[i][0] - chosen[j][0]) ** 2 + (chosen[i][1] - chosen[j][1]) ** 2
            if d2 < min_sq:
                raise PlacementError("start distance < 600m")

    # Sort around centroid by atan2(z - cz, x - cx)
    def angle_from_centroid(pt):
        x, z = pt
        return math.atan2(z - cz, x - cx)

    chosen.sort(key=angle_from_centroid)

    starts = []
    for slot, (x, z) in enumerate(chosen):
        starts.append({
            "slot": slot,
            "flags": 1, # valid
            "x_m": x,
            "z_m": z,
            "x_q": q16(x),
            "z_q": q16(z),
            "plateau_hcode": 0,
            "flat_radius_q": q16(PLATEAU_FLAT_RADIUS_M)
        })

    return starts


def flatten_plateau(h_m: np.ndarray, cell_m: float, start_x: float, start_z: float) -> tuple:
    """Flatten plateau around start location to an exact quantized height code."""
    N = h_m.shape[0]

    # Median height within 96m
    c_min = max(0, int(np.floor((start_x - PLATEAU_MEDIAN_RADIUS_M) / cell_m)))
    c_max = min(N, int(np.ceil((start_x + PLATEAU_MEDIAN_RADIUS_M) / cell_m)) + 1)
    r_min = max(0, int(np.floor((start_z - PLATEAU_MEDIAN_RADIUS_M) / cell_m)))
    r_max = min(N, int(np.ceil((start_z + PLATEAU_MEDIAN_RADIUS_M) / cell_m)) + 1)

    cols = np.arange(c_min, c_max, dtype=np.float32) * cell_m
    rows = np.arange(r_min, r_max, dtype=np.float32) * cell_m
    gx, gz = np.meshgrid(cols, rows)
    r = np.sqrt((gx - np.float32(start_x)) ** 2 + (gz - np.float32(start_z)) ** 2)

    sub = h_m[r_min:r_max, c_min:c_max]
    inside = (r <= PLATEAU_MEDIAN_RADIUS_M)
    samples = sub[inside]
    target_m = float(np.clip(np.median(samples) if len(samples) > 0 else 25.0, 12.0, 90.0))

    code = int(m_to_code(target_m))
    tm = float(code_to_m(code))

    # Blend window +- 160m
    w_c_min = max(0, int(np.floor((start_x - PLATEAU_WINDOW_RADIUS_M) / cell_m)))
    w_c_max = min(N, int(np.ceil((start_x + PLATEAU_WINDOW_RADIUS_M) / cell_m)) + 1)
    w_r_min = max(0, int(np.floor((start_z - PLATEAU_WINDOW_RADIUS_M) / cell_m)))
    w_r_max = min(N, int(np.ceil((start_z + PLATEAU_WINDOW_RADIUS_M) / cell_m)) + 1)

    w_cols = np.arange(w_c_min, w_c_max, dtype=np.float32) * cell_m
    w_rows = np.arange(w_r_min, w_r_max, dtype=np.float32) * cell_m
    wgx, wgz = np.meshgrid(w_cols, w_rows)
    wr = np.sqrt((wgx - np.float32(start_x)) ** 2 + (wgz - np.float32(start_z)) ** 2)

    w = 1.0 - smoothstep(64.0, 160.0, wr)
    w[wr <= 64.0] = 1.0 # Exact flat plateau inside 64m

    sub_h = h_m[w_r_min:w_r_max, w_c_min:w_c_max]
    h_m[w_r_min:w_r_max, w_c_min:w_c_max] = sub_h * (1.0 - w) + tm * w

    return code, tm


def place_resources(layers, biome: np.ndarray, starts: list, rng) -> list:
    """Place player kit resources and neutral world resource nodes."""
    placed = []

    def can_place(x, z, footprint, kind_check=None, min_start_dist=None, min_spacing=None):
        if x < 32.0 or x >= WORLD_M - 32.0 or z < 32.0 or z >= WORLD_M - 32.0:
            return False

        if min_start_dist is not None:
            min_s_sq = min_start_dist * min_start_dist
            for s in starts:
                if (x - s["x_m"]) ** 2 + (z - s["z_m"]) ** 2 < min_s_sq:
                    return False

        for node in placed:
            dx = x - node["x_m"]
            dz = z - node["z_m"]
            d2 = dx * dx + dz * dz
            f_dist = footprint + node["footprint_m"] + 8.0
            if d2 < f_dist * f_dist:
                return False
            if kind_check is not None and node["kind"] == kind_check and min_spacing is not None:
                if d2 < min_spacing * min_spacing:
                    return False
        return True

    # 1. Place Kit Resources around each start
    for s in starts:
        sx = s["x_m"]
        sz = s["z_m"]
        slot = s["slot"]

        for kind, count, r_min, r_max, amount, footprint in KIT_RESOURCES:
            for _ in range(count):
                placed_one = False
                cur_rmin = float(r_min)
                cur_rmax = float(r_max)

                for attempt_widen in range(4): # original + 3 widen attempts
                    for _ in range(60):
                        ang = rng.uniform() * 2.0 * math.pi
                        rad = cur_rmin + rng.uniform() * (cur_rmax - cur_rmin)
                        nx = sx + rad * math.cos(ang)
                        nz = sz + rad * math.sin(ang)

                        # Check distance to start centre >= 70m
                        if (nx - sx) ** 2 + (nz - sz) ** 2 < 70.0 * 70.0:
                            continue

                        # Check logic cell properties
                        lc_c = int(nx / 8.0)
                        lc_r = int(nz / 8.0)
                        if not (0 <= lc_c < LOGIC_N and 0 <= lc_r < LOGIC_N):
                            continue
                        if layers.surface[lc_r, lc_c] != Surface.Land:
                            continue
                        if layers.slope_max_deg[lc_r, lc_c] > 15.0:
                            continue

                        if can_place(nx, nz, footprint):
                            placed.append({
                                "kind": kind,
                                "owner": slot,
                                "amount": amount,
                                "x_m": nx,
                                "z_m": nz,
                                "x_q": q16(nx),
                                "z_q": q16(nz),
                                "footprint_m": footprint,
                                "footprint_q": q16(footprint)
                            })
                            placed_one = True
                            break
                    if placed_one:
                        break
                    cur_rmin *= 1.25
                    cur_rmax *= 1.25

                if not placed_one:
                    raise PlacementError(f"failed to place kit node {kind} for slot {slot}")

    # 2. Place Neutral Resources
    for name, kind, target, min_cnt, spacing, amount, footprint in NEUTRAL_RESOURCES:
        # Candidate selection rules
        if name == "Forest":
            strict = (biome == Biome.Forest)
            relaxed = (biome == Biome.Forest) | (biome == Biome.Grass)
        elif name == "Berries":
            strict = (biome == Biome.Grass) | (biome == Biome.Forest)
            relaxed = strict
        elif name == "Herd":
            strict = (biome == Biome.Grass) | (biome == Biome.Arid)
            relaxed = strict
        elif name == "Iron":
            strict = (biome == Biome.Rock) | ((layers.slope_max_deg > 12.0) & (layers.hm > 60.0))
            relaxed = (layers.hm > 40.0) & ((layers.surface == Surface.Land) | (layers.surface == Surface.Rough))
        elif name == "Gold":
            strict = ((layers.surface == Surface.Land) | (layers.surface == Surface.Rough)) & (layers.hm > 40.0)
            relaxed = strict
        elif name == "Silicon":
            strict = (biome == Biome.Arid) | (biome == Biome.Beach)
            relaxed = (layers.surface == Surface.Land)
        elif name == "OilLand":
            strict = (biome == Biome.Arid) | (biome == Biome.Tundra) | \
                     ((layers.surface == Surface.Land) & (layers.hm < 80.0))
            relaxed = strict
        elif name == "OilSea":
            strict = (layers.surface == Surface.ShallowWater) & (layers.hm <= -3.0)
            relaxed = strict
        elif name == "Relic":
            strict = (layers.surface == Surface.Land) | (layers.surface == Surface.Rough)
            relaxed = strict
        else:
            strict = (layers.surface == Surface.Land)
            relaxed = strict

        n_placed = 0
        cand_pools = [strict]
        if name != "OilSea":
            cand_pools.append(relaxed & (~strict))
            cand_pools.append(((layers.surface == Surface.Land) | (layers.surface == Surface.Rough)) & (~strict) & (~relaxed))

        for cand_mask in cand_pools:
            if n_placed >= target:
                break
            cand_rows, cand_cols = np.where(cand_mask)
            sub_cands = list(zip(cand_rows.tolist(), cand_cols.tolist()))
            rng.shuffle(sub_cands)

            for r, c in sub_cands:
                if n_placed >= target:
                    break
                nx = float(c) * 8.0 + rng.uniform() * 8.0
                nz = float(r) * 8.0 + rng.uniform() * 8.0

                if can_place(nx, nz, footprint, kind_check=kind, min_start_dist=420.0, min_spacing=spacing):
                    placed.append({
                        "kind": kind,
                        "owner": 0xFF,
                        "amount": amount,
                        "x_m": nx,
                        "z_m": nz,
                        "x_q": q16(nx),
                        "z_q": q16(nz),
                        "footprint_m": footprint,
                        "footprint_q": q16(footprint)
                    })
                    n_placed += 1

        if n_placed < min_cnt:
            raise PlacementError(f"{name}: only {n_placed} placed, required minimum {min_cnt}")

    if len(placed) > MAX_TOTAL_RESOURCES:
        raise PlacementError(f"total resources {len(placed)} exceeds {MAX_TOTAL_RESOURCES}")

    return placed
