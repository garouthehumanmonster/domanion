"""validate.py - Rigorous verification of theatre invariants V1 through V10."""

from collections import deque
import numpy as np
from .params import (
    WORLD_M, LOGIC_N, Surface, Biome, NodeKind,
    LAND_FRACTION_MIN, LAND_FRACTION_MAX, START_MIN_DIST_M,
    PLATEAU_FLAT_RADIUS_M, BEACH_CELLS_MIN, FOREST_GRASS_MIN_SHARE,
    KIT_RESOURCES, NEUTRAL_RESOURCES, MAX_TOTAL_RESOURCES
)
from .tformat import m_to_code


def validate(h_m: np.ndarray, layers, biome: np.ndarray, starts: list,
             resources: list, landings: list, N: int) -> tuple[bool, dict, list]:
    """Validate all continental theatre invariants and return report dictionary and filtered landings."""
    cell_m = WORLD_M / float(N)
    report = {}
    ok = True

    # V1: Land fraction
    land_frac = float(np.mean(h_m >= 0.0))
    v1_ok = (LAND_FRACTION_MIN <= land_frac <= LAND_FRACTION_MAX)
    report["V1_land_fraction"] = {"value": land_frac, "min": LAND_FRACTION_MIN, "max": LAND_FRACTION_MAX, "ok": v1_ok}
    if not v1_ok: ok = False

    # V2: All arrays finite
    v2_ok = bool(np.all(np.isfinite(h_m)) and np.all(np.isfinite(layers.hm)) and
                 np.all(np.isfinite(layers.slope_max_deg)))
    report["V2_all_arrays_finite"] = {"ok": v2_ok}
    if not v2_ok: ok = False

    # V3: 8 starts, pairwise >= 600m, flat plateau within 60m, surface == Land
    v3_ok = (len(starts) == 8)
    if v3_ok:
        # Pairwise distance >= 600m
        for i in range(8):
            for j in range(i + 1, 8):
                d2 = (starts[i]["x_m"] - starts[j]["x_m"]) ** 2 + (starts[i]["z_m"] - starts[j]["z_m"]) ** 2
                if d2 < START_MIN_DIST_M * START_MIN_DIST_M:
                    v3_ok = False
                    break

        # Check plateau flatness and surface
        h_codes = m_to_code(h_m)
        for s in starts:
            sx = s["x_m"]
            sz = s["z_m"]
            expected_code = s["plateau_hcode"]

            lc_r = int(sz / 8.0)
            lc_c = int(sx / 8.0)
            if layers.surface[lc_r, lc_c] != Surface.Land:
                v3_ok = False

            # Samples within 60m
            c_min = max(0, int(np.floor((sx - 60.0) / cell_m)))
            c_max = min(N, int(np.ceil((sx + 60.0) / cell_m)) + 1)
            r_min = max(0, int(np.floor((sz - 60.0) / cell_m)))
            r_max = min(N, int(np.ceil((sz + 60.0) / cell_m)) + 1)

            cols = np.arange(c_min, c_max, dtype=np.float32) * cell_m
            rows = np.arange(r_min, r_max, dtype=np.float32) * cell_m
            gx, gz = np.meshgrid(cols, rows)
            r2 = (gx - np.float32(sx)) ** 2 + (gz - np.float32(sz)) ** 2

            sub_codes = h_codes[r_min:r_max, c_min:c_max]
            within_60m = sub_codes[r2 <= 60.0 * 60.0]
            if not np.all(within_60m == expected_code):
                v3_ok = False

    report["V3_starts_and_plateaus"] = {"count": len(starts), "ok": v3_ok}
    if not v3_ok: ok = False

    # V4: All 8 starts in one 4-connected ground-passable component >= 60% passable cells
    passable = (layers.surface == Surface.Beach) | \
               (layers.surface == Surface.Land) | \
               (layers.surface == Surface.Rough)
    tot_passable = int(np.sum(passable))

    start0_r = int(starts[0]["z_m"] / 8.0)
    start0_c = int(starts[0]["x_m"] / 8.0)

    visited = np.zeros((LOGIC_N, LOGIC_N), dtype=bool)
    queue = deque([(start0_r, start0_c)])
    visited[start0_r, start0_c] = True
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

    all_starts_reached = True
    for s in starts:
        sr = int(s["z_m"] / 8.0)
        sc = int(s["x_m"] / 8.0)
        if not visited[sr, sc]:
            all_starts_reached = False
            break

    v4_ok = all_starts_reached and (comp_size >= 0.60 * tot_passable)
    report["V4_starts_connectivity"] = {
        "component_size": comp_size,
        "tot_passable": tot_passable,
        "all_reached": all_starts_reached,
        "ok": v4_ok
    }
    if not v4_ok: ok = False

    # V5: Kit complete and neutrals >= 420m from all starts
    v5_ok = True
    for slot in range(8):
        slot_nodes = [r for r in resources if r["owner"] == slot]
        for kind, count, _, _, _, _ in KIT_RESOURCES:
            actual = sum(1 for n in slot_nodes if n["kind"] == kind)
            if actual != count:
                v5_ok = False
                break

    for n in resources:
        if n["owner"] == 0xFF:
            for s in starts:
                if (n["x_m"] - s["x_m"]) ** 2 + (n["z_m"] - s["z_m"]) ** 2 < 420.0 * 420.0:
                    v5_ok = False
                    break

    report["V5_kit_and_neutral_spacing"] = {"ok": v5_ok}
    if not v5_ok: ok = False

    # V6: Filter landings (drop non-Beach) and require >= 6
    filtered_landings = []
    for l in landings:
        if isinstance(l, dict):
            xm = l["x_m"] if "x_m" in l else float(l["x_q"]) / 16.0
            zm = l["z_m"] if "z_m" in l else float(l["z_q"]) / 16.0
        else:
            xm, zm = l[0], l[1]
        lc_r = int(zm / 8.0)
        lc_c = int(xm / 8.0)
        if 0 <= lc_r < LOGIC_N and 0 <= lc_c < LOGIC_N:
            if layers.surface[lc_r, lc_c] == Surface.Beach:
                filtered_landings.append(l)

    v6_ok = (len(filtered_landings) >= 6)
    report["V6_landings_count"] = {"count": len(filtered_landings), "min": 6, "ok": v6_ok}
    if not v6_ok: ok = False

    # V7: Beach cells >= 300
    beach_cells = int(np.sum(layers.surface == Surface.Beach))
    v7_ok = (beach_cells >= BEACH_CELLS_MIN)
    report["V7_beach_cell_count"] = {"count": beach_cells, "min": BEACH_CELLS_MIN, "ok": v7_ok}
    if not v7_ok: ok = False

    # V8: (Forest + Grass) >= 30% of land logic cells
    land_cells = int(np.sum(layers.surface >= Surface.Land))
    fg_cells = int(np.sum((layers.surface >= Surface.Land) &
                          ((biome == Biome.Forest) | (biome == Biome.Grass))))
    fg_share = (float(fg_cells) / float(land_cells)) if land_cells > 0 else 0.0
    v8_ok = (fg_share >= FOREST_GRASS_MIN_SHARE)
    report["V8_forest_grass_share"] = {"share": fg_share, "min": FOREST_GRASS_MIN_SHARE, "ok": v8_ok}
    if not v8_ok: ok = False

    # V9: Neutral minimum counts
    v9_ok = True
    neutral_counts = {}
    for name, kind, target, min_cnt, _, _, _ in NEUTRAL_RESOURCES:
        cnt = sum(1 for n in resources if n["owner"] == 0xFF and n["kind"] == kind)
        # Note: OilLand and OilSea both map to Oil, check total
        neutral_counts[name] = cnt
        if name != "OilLand" and name != "OilSea" and cnt < min_cnt:
            v9_ok = False
    oil_total = sum(1 for n in resources if n["owner"] == 0xFF and n["kind"] == NodeKind.Oil)
    if oil_total < (16 + 12) // 2: # sum of mins is 8 + 6 = 14
        if oil_total < 14:
            v9_ok = False
    report["V9_neutral_counts"] = {"counts": neutral_counts, "ok": v9_ok}
    if not v9_ok: ok = False

    # V10: Total resources <= 1024, in bounds, water restrictions
    v10_ok = (len(resources) <= MAX_TOTAL_RESOURCES)
    for n in resources:
        xm = n["x_m"]
        zm = n["z_m"]
        if xm < 0.0 or xm >= WORLD_M or zm < 0.0 or zm >= WORLD_M:
            v10_ok = False
        lc_r = int(zm / 8.0)
        lc_c = int(xm / 8.0)
        surf = layers.surface[lc_r, lc_c]
        if surf == Surface.DeepWater:
            v10_ok = False
        if surf == Surface.ShallowWater and n["kind"] != NodeKind.Oil:
            v10_ok = False

    report["V10_resource_limits_and_water"] = {"total": len(resources), "max": MAX_TOTAL_RESOURCES, "ok": v10_ok}
    if not v10_ok: ok = False

    # Informational diagnostics (no pass/fail)
    report["summary"] = {
        "land_fraction": land_frac,
        "beach_cells": beach_cells,
        "landings": len(filtered_landings),
        "total_resources": len(resources),
        "all_valid": ok
    }

    return ok, report, filtered_landings
