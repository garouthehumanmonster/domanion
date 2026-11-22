"""bake_theatre.py - CLI executable for baking continental theatres with multi-attempt retry."""

import os
import sys
import time
import json
import argparse
import numpy as np

# Ensure dominion2026 directory is on sys.path
script_dir = os.path.dirname(os.path.abspath(__file__))
project_dir = os.path.abspath(os.path.join(script_dir, "../.."))
if project_dir not in sys.path:
    sys.path.insert(0, project_dir)

from tools.theatre_baker.params import WORLD_M, ALLOWED_N
from tools.theatre_baker.rng import Rng
from tools.theatre_baker.terrain import make_heights
from tools.theatre_baker.erosion import erode_multiscale, thermal
from tools.theatre_baker.coast import shape_shore, pick_landings, carve_landing
from tools.theatre_baker.layers import compute_layers, compute_biome
from tools.theatre_baker.placement import candidate_starts, pick_starts, flatten_plateau, place_resources, PlacementError
from tools.theatre_baker.validate import validate
from tools.theatre_baker.tformat import m_to_code, write_theatre
from tools.theatre_baker.preview import write_preview


def main():
    parser = argparse.ArgumentParser(description="DOMINION 2026 Continental Theatre Baker")
    parser.add_argument("--seed", type=int, default=2026, help="Initial PRNG seed")
    parser.add_argument("--name", type=str, default="continent_2026", help="Theatre name")
    parser.add_argument("--size", type=int, default=2048, help="Heightmap grid size N (512, 1024, 2048)")
    parser.add_argument("--out", type=str, default="assets/compiled/theatres", help="Output directory")
    parser.add_argument("--max-attempts", type=int, default=16, help="Maximum generation attempts")
    parser.add_argument("--no-preview", action="store_true", help="Skip rendering 1024x1024 preview PNG")

    args = parser.parse_args()

    if args.size not in ALLOWED_N:
        print(f"Error: Invalid size {args.size}. Must be one of {ALLOWED_N}", file=sys.stderr)
        sys.exit(2)

    N = args.size
    cell_m = WORLD_M / float(N)
    attempts_history = []

    for k in range(args.max_attempts):
        seed_k = (args.seed + k * 0x9E3779B1) & 0xFFFFFFFF
        rng = Rng(seed_k)
        t_start = time.time()

        print(f"\n--- Attempt {k + 1}/{args.max_attempts} (seed: {seed_k}) ---")
        try:
            # 1. Base terrain
            t0 = time.time()
            h = make_heights(N, seed_k)
            t_make = time.time() - t0
            print(f"[stage] make_heights      {t_make:6.3f} s")

            # 2. Hydraulic erosion
            t0 = time.time()
            h = erode_multiscale(h, N, seed_k)
            t_erode = time.time() - t0
            print(f"[stage] erode_multiscale  {t_erode:6.3f} s")

            # 3. Thermal relaxation
            t0 = time.time()
            h = thermal(h, cell_m)
            t_thermal = time.time() - t0
            print(f"[stage] thermal           {t_thermal:6.3f} s")

            # 4. Coastal shaping
            t0 = time.time()
            h = shape_shore(h)
            t_shore = time.time() - t0
            print(f"[stage] shape_shore       {t_shore:6.3f} s")

            # 5. Layers L1
            t0 = time.time()
            L1 = compute_layers(h, N)
            t_L1 = time.time() - t0
            print(f"[stage] layers_L1         {t_L1:6.3f} s")

            # 6. Landings
            t0 = time.time()
            landings = pick_landings(h, L1, rng)
            for l in landings:
                carve_landing(h, cell_m, l[0], l[1], l[2])
            t_landings = time.time() - t0
            print(f"[stage] landings          {t_landings:6.3f} s (picked {len(landings)})")

            # 7. Layers L2
            t0 = time.time()
            L2 = compute_layers(h, N)
            t_L2 = time.time() - t0
            print(f"[stage] layers_L2         {t_L2:6.3f} s")

            # 8. Starts & Plateau flattening
            t0 = time.time()
            cands, centroid = candidate_starts(L2)
            starts = pick_starts(cands, centroid, rng)
            for s in starts:
                p_hcode, _ = flatten_plateau(h, cell_m, s["x_m"], s["z_m"])
                s["plateau_hcode"] = p_hcode
            t_starts = time.time() - t0
            print(f"[stage] starts_plateaus   {t_starts:6.3f} s")

            # 9. Layers L3 (final) & Biome
            t0 = time.time()
            L3 = compute_layers(h, N)
            biome = compute_biome(h, L3, seed_k)
            t_L3 = time.time() - t0
            print(f"[stage] layers_L3_biome   {t_L3:6.3f} s")

            # 10. Resources
            t0 = time.time()
            resources = place_resources(L3, biome, starts, rng)
            t_resources = time.time() - t0
            print(f"[stage] place_resources   {t_resources:6.3f} s (placed {len(resources)})")

            # 11. Validation
            t0 = time.time()
            ok, report, filtered_landings = validate(h, L3, biome, starts, resources, landings, N)
            t_val = time.time() - t0
            print(f"[stage] validate          {t_val:6.3f} s (result: {'PASS' if ok else 'FAIL'})")

            report["timings"] = {
                "make_heights": t_make,
                "erode_multiscale": t_erode,
                "thermal": t_thermal,
                "shape_shore": t_shore,
                "layers_L1": t_L1,
                "landings": t_landings,
                "layers_L2": t_L2,
                "starts": t_starts,
                "layers_L3": t_L3,
                "resources": t_resources,
                "validate": t_val,
                "total": time.time() - t_start
            }

            if not ok:
                failing = [k for k, v in report.items() if isinstance(v, dict) and not v.get("ok", True)]
                print(f"Validation failed checks: {failing}")
                attempts_history.append((k, seed_k, failing))
                continue

            # Write outputs on success
            h_codes = m_to_code(h)
            os.makedirs(args.out, exist_ok=True)
            thtr_file = os.path.join(args.out, f"{args.name}.thtr")
            report_file = os.path.join(args.out, f"{args.name}.report.json")
            preview_file = os.path.join(args.out, f"{args.name}.preview.png")

            report["attempt_index"] = k
            report["effective_seed"] = seed_k

            bytes_written = write_theatre(
                thtr_file, seed_k, N, h_codes, L3.surface, L3.slope, biome,
                L3.distwater, starts, resources, filtered_landings
            )
            print(f"\nWrote theatre binary: {thtr_file} ({bytes_written} bytes)")

            with open(report_file, "w") as rf:
                json.dump(report, rf, sort_keys=True, indent=2)
            print(f"Wrote report JSON:    {report_file}")

            if not args.no_preview:
                preview_data = {
                    "h_m": h,
                    "surface": L3.surface,
                    "biome": biome,
                    "starts": starts,
                    "landings": filtered_landings,
                    "resources": resources
                }
                write_preview(preview_file, preview_data)
                print(f"Wrote preview PNG:    {preview_file}")

            print(f"\nSUCCESS on attempt {k} (seed {seed_k}) in {time.time() - t_start:.2f} s")
            sys.exit(0)

        except PlacementError as pe:
            print(f"PlacementError: {pe}")
            attempts_history.append((k, seed_k, [str(pe)]))
            continue

    print(f"\nAll {args.max_attempts} attempts failed!", file=sys.stderr)
    for k, s, fails in attempts_history:
        print(f"  Attempt {k} (seed {s}): {fails}", file=sys.stderr)
    sys.exit(3)


if __name__ == "__main__":
    main()
