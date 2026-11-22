"""selftest.py - Automated verification tests for theatre_baker groups."""

import os
import sys
import struct
import tempfile
import numpy as np

# Ensure dominion2026 directory is on sys.path
script_dir = os.path.dirname(os.path.abspath(__file__))
project_dir = os.path.abspath(os.path.join(script_dir, "../.."))
if project_dir not in sys.path:
    sys.path.insert(0, project_dir)

from tools.theatre_baker.rng import Rng, xxh64
from tools.theatre_baker.noise import value_noise, fbm, ridged, smoothstep
from tools.theatre_baker.tformat import m_to_code, code_to_m, q16, write_theatre, read_theatre
from tools.theatre_baker.params import LOGIC_N, SEA_LEVEL_CODE


def test_group_a():
    """Group A tests: Rng, XXH64, noise, codes, tformat roundtrip."""
    passed = 0

    # 1. Rng(0) first three outputs
    rng = Rng(0)
    v0 = rng.next_u64()
    v1 = rng.next_u64()
    v2 = rng.next_u64()
    assert v0 == 0xE220A8397B1DCDAF, f"Rng v0 failed: 0x{v0:016X}"
    assert v1 == 0x6E789E6AA1B965F4, f"Rng v1 failed: 0x{v1:016X}"
    assert v2 == 0x06C45D188009454F, f"Rng v2 failed: 0x{v2:016X}"
    passed += 1

    # 2. Rng distinct across 1000 seeds
    seen = set()
    for s in range(1000):
        val = Rng(s).next_u64()
        assert val not in seen, f"Seed {s} collided"
        seen.add(val)
    passed += 1

    # 3. XXH64 golden vectors and memoryview consistency
    assert xxh64(b"") == 0xEF46DB3751D8E999, f"xxh64(empty) failed: 0x{xxh64(b''):016X}"
    test_rng = Rng(42)
    rand_bytes = bytearray(test_rng.randint(0, 255) for _ in range(1000))
    h_bytes = xxh64(bytes(rand_bytes))
    h_mview = xxh64(memoryview(rand_bytes))
    assert h_bytes == h_mview, "xxh64 memoryview self-consistency failed"
    passed += 1

    # 4. Noise in [0, 1), float32, deterministic, seed variance
    gx, gy = np.meshgrid(np.linspace(0, 8, 64, dtype=np.float32),
                         np.linspace(0, 8, 64, dtype=np.float32))
    n1 = value_noise(gx, gy, 1234)
    n2 = value_noise(gx, gy, 1234)
    n3 = value_noise(gx, gy, 5678)
    assert n1.dtype == np.float32, f"Noise dtype was {n1.dtype}"
    assert np.all(n1 >= 0.0) and np.all(n1 < 1.0), "Noise outside [0, 1)"
    assert np.array_equal(n1, n2), "value_noise not deterministic"
    assert np.mean(np.abs(n1 - n3)) > 0.05, "Different seeds did not vary sufficiently"
    f = fbm(gx, gy, 100, 4)
    assert np.all(f >= 0.0) and np.all(f <= 1.0), "fbm outside [0, 1]"
    passed += 1

    # 5. Height code conversion accuracy and invertibility
    assert int(m_to_code(-128.0)) == 0, f"m_to_code(-128) = {m_to_code(-128.0)}"
    assert int(m_to_code(512.0)) == 65535, f"m_to_code(512) = {m_to_code(512.0)}"
    assert int(m_to_code(0.0)) == SEA_LEVEL_CODE, f"m_to_code(0) = {m_to_code(0.0)}"

    test_h = np.linspace(-128.0, 512.0, 10000, dtype=np.float64)
    recovered_h = code_to_m(m_to_code(test_h))
    max_err = np.max(np.abs(recovered_h - test_h))
    assert max_err <= 0.006, f"Height decode error too large: {max_err}"

    all_codes = np.arange(65536, dtype=np.uint16)
    rec_codes = m_to_code(code_to_m(all_codes))
    assert np.array_equal(all_codes, rec_codes), "Code roundtrip not 100% exact over all 65536 codes"
    passed += 1

    # 6. tformat roundtrip and corrupt rejection
    N = 512
    syn_rng = Rng(999)
    syn_h = np.zeros((N, N), dtype=np.uint16)
    for r in range(N):
        for c in range(0, N, 4):
            val = syn_rng.next_u64()
            syn_h[r, c] = val & 0xFFFF
            if c + 1 < N: syn_h[r, c+1] = (val >> 16) & 0xFFFF
            if c + 2 < N: syn_h[r, c+2] = (val >> 32) & 0xFFFF
            if c + 3 < N: syn_h[r, c+3] = (val >> 48) & 0xFFFF

    syn_surf = np.zeros((LOGIC_N, LOGIC_N), dtype=np.uint8)
    syn_slope = np.zeros((LOGIC_N, LOGIC_N), dtype=np.uint8)
    syn_biome = np.zeros((LOGIC_N, LOGIC_N), dtype=np.uint8)
    syn_dist = np.zeros((LOGIC_N, LOGIC_N), dtype=np.uint8)

    syn_starts = [
        {"slot": i, "flags": 1, "x_q": i * 1000, "z_q": i * 1000, "plateau_hcode": 14000, "flat_radius_q": 1024}
        for i in range(8)
    ]
    syn_res = [
        {"kind": i % 8, "owner": 0xFF, "amount": 1000, "x_q": i * 500, "z_q": i * 500, "footprint_q": 64}
        for i in range(5)
    ]
    syn_land = [
        {"x_q": i * 800, "z_q": i * 800, "heading_a16": i * 5000}
        for i in range(3)
    ]

    with tempfile.TemporaryDirectory() as tmp_dir:
        tmp_file = os.path.join(tmp_dir, "test_syn.thtr")
        write_theatre(tmp_file, 12345, N, syn_h, syn_surf, syn_slope, syn_biome,
                      syn_dist, syn_starts, syn_res, syn_land)

        read_back = read_theatre(tmp_file)
        assert read_back["height_n"] == N
        assert np.array_equal(read_back["height_codes"], syn_h)
        assert np.array_equal(read_back["surface"], syn_surf)
        assert len(read_back["starts"]) == 8
        assert len(read_back["resources"]) == 5
        assert len(read_back["landings"]) == 3

        # Corrupt one byte in payload (e.g. byte 200)
        with open(tmp_file, "r+b") as f:
            f.seek(200)
            b = f.read(1)
            f.seek(200)
            f.write(bytes([b[0] ^ 0xFF]))

        try:
            read_theatre(tmp_file)
            assert False, "read_theatre should have raised ValueError on corrupted byte"
        except ValueError:
            pass # Expected

    passed += 1
    print(f"GROUP A PASS ({passed}/6)")


from tools.theatre_baker.terrain import make_heights
from tools.theatre_baker.erosion import erode, erode_multiscale, thermal
from tools.theatre_baker.coast import shape_shore, carve_landing


def test_group_b():
    """Group B tests: Terrain, erosion, thermal relaxation, and coastal shaping."""
    passed = 0

    # 1. make_heights: land fraction, bounds, determinism
    h1 = make_heights(512, 2026)
    h2 = make_heights(512, 2026)
    assert np.array_equal(h1, h2), "make_heights not deterministic"
    land_frac = float(np.mean(h1 >= 0.0))
    assert 0.455 <= land_frac <= 0.465, f"Land fraction out of range: {land_frac}"
    assert float(h1.min()) >= -128.0, f"Min height below -128: {h1.min()}"
    assert float(h1.max()) <= 440.0, f"Max height above 440: {h1.max()}"
    passed += 1

    # 2. thermal on synthetic 64x64 cone conserves sum and doesn't increase max
    N_cone = 64
    coords = np.arange(N_cone, dtype=np.float64) - 31.5
    gx, gz = np.meshgrid(coords, coords)
    r = np.sqrt(gx * gx + gz * gz)
    cone_b = np.maximum(0.0, 30.0 - r * 1.5)
    cone_a = thermal(cone_b, 8.0, 24)
    sum_b = float(np.sum(cone_b))
    sum_a = float(np.sum(cone_a))
    assert abs(sum_a - sum_b) / (sum_b + 1e-12) <= 1e-6, f"Thermal mass conservation failed: delta rel = {abs(sum_a - sum_b) / sum_b}"
    assert float(np.max(cone_a)) <= float(np.max(cone_b)) + 1e-6, "Thermal increased max height"
    passed += 1

    # 3. erode on synthetic cone: finite, max bounds, sea cells untouched
    cone_erode_b = np.maximum(0.0, 50.0 - r.astype(np.float32) * 3.0)
    cone_erode_a = erode(cone_erode_b, 8.0, 50, seed=1)
    assert np.all(np.isfinite(cone_erode_a)), "Erode produced non-finite values"
    assert float(np.max(cone_erode_a)) <= float(np.max(cone_erode_b)) + 1e-3, "Erode increased max height"
    sea_mask = (cone_erode_b <= 0.0)
    assert np.array_equal(cone_erode_a[sea_mask], cone_erode_b[sea_mask]), "Erode touched sea cells"
    passed += 1

    # 4. erode_multiscale on real N=512 heights
    e1 = erode_multiscale(h1, 512, seed=2026)
    e2 = erode_multiscale(h1, 512, seed=2026)
    assert np.array_equal(e1, e2), "erode_multiscale not deterministic"
    assert np.all(np.isfinite(e1)), "erode_multiscale produced non-finite values"
    real_land = (h1 > 0.0)
    assert np.all(e1[real_land] >= 0.0), "erode_multiscale caused land to dip below 0m"
    assert np.array_equal(e1[~real_land], h1[~real_land]), "erode_multiscale modified sea cells"

    delta = e1[real_land] - h1[real_land]
    rms = float(np.sqrt(np.mean(delta * delta)))
    max_d = float(np.max(np.abs(delta)))
    assert 0.05 <= rms <= 20.0, f"Erosion RMS over land out of range [0.05, 20]: {rms}"
    assert max_d <= 40.0 + 1e-3, f"Erosion max delta exceeded 40m: {max_d}"
    passed += 1

    # 5. shape_shore: cells with |h|>10 unchanged
    s_before = e1.copy()
    s_after = shape_shore(s_before)
    assert np.all(np.isfinite(s_after)), "shape_shore non-finite"
    untouched = (np.abs(s_before) > 10.0)
    assert np.array_equal(s_after[untouched], s_before[untouched]), "shape_shore modified cells outside |h|<=10"
    passed += 1

    # 6. carve_landing on synthetic planar beach
    beach_n = 64
    b_coords = np.arange(beach_n, dtype=np.float32) * 8.0
    bgx, bgz = np.meshgrid(b_coords, b_coords)
    site_x = 256.0
    site_z = 256.0
    # Slope 5 degrees downhill toward +x
    h_beach = -float(np.tan(np.radians(5.0))) * (bgx - site_x)
    beach_before = h_beach.copy()
    carve_landing(h_beach, 8.0, site_x, site_z, (1.0, 0.0))

    centre_h = float(h_beach[int(site_z / 8.0), int(site_x / 8.0)])
    assert abs(centre_h) <= 0.01, f"Landing centre height was not near 0: {centre_h}"
    # Cell at lateral b = 64m (e.g. z = site_z + 64m)
    lat_row = int((site_z + 64.0) / 8.0)
    lat_col = int(site_x / 8.0)
    assert float(h_beach[lat_row, lat_col]) == float(beach_before[lat_row, lat_col]), "Lateral cell at b=64m was modified"
    passed += 1

    print(f"GROUP B PASS ({passed}/6)")


import json
import hashlib
import subprocess
from tools.theatre_baker.terrain import make_heights
from tools.theatre_baker.erosion import erode, erode_multiscale, thermal
from tools.theatre_baker.coast import shape_shore, carve_landing
from tools.theatre_baker.layers import compute_layers, compute_distwater, compute_biome, Layers
from tools.theatre_baker.placement import pick_starts, flatten_plateau
from tools.theatre_baker.validate import validate
from tools.theatre_baker.params import Surface, Biome, NodeKind, SEA_LEVEL_CODE


def test_group_d():
    """Group D tests: End-to-end pipeline bake, CLI validation, report checks, PNG integrity."""
    passed = 0

    with tempfile.TemporaryDirectory() as tmp_dir1, tempfile.TemporaryDirectory() as tmp_dir2:
        bake_script = os.path.join(project_dir, "tools/theatre_baker/bake_theatre.py")

        # 1. Bake N=512 seed 2026
        cmd1 = [sys.executable, bake_script, "--seed", "2026", "--size", "512",
                "--name", "test_512", "--out", tmp_dir1]
        proc1 = subprocess.run(cmd1, capture_output=True, text=True)
        assert proc1.returncode == 0, f"Bake failed with exit {proc1.returncode}:\n{proc1.stdout}\n{proc1.stderr}"

        thtr_file1 = os.path.join(tmp_dir1, "test_512.thtr")
        report_file1 = os.path.join(tmp_dir1, "test_512.report.json")
        preview_file1 = os.path.join(tmp_dir1, "test_512.preview.png")

        assert os.path.exists(thtr_file1), "thtr file was not created"
        data1 = read_theatre(thtr_file1)
        assert data1["height_n"] == 512, f"Expected height_n 512, got {data1['height_n']}"
        assert data1["start_count"] == 8, f"Expected 8 starts, got {data1['start_count']}"
        assert data1["sea_level_code"] == SEA_LEVEL_CODE, "Sea level code mismatch"
        passed += 1

        # 2. Second bake with identical args is byte-identical
        cmd2 = [sys.executable, bake_script, "--seed", "2026", "--size", "512",
                "--name", "test_512", "--out", tmp_dir2]
        proc2 = subprocess.run(cmd2, capture_output=True, text=True)
        assert proc2.returncode == 0, f"Second bake failed: {proc2.stderr}"

        thtr_file2 = os.path.join(tmp_dir2, "test_512.thtr")
        with open(thtr_file1, "rb") as f1, open(thtr_file2, "rb") as f2:
            h1 = hashlib.sha256(f1.read()).hexdigest()
            h2 = hashlib.sha256(f2.read()).hexdigest()
        assert h1 == h2, f"Bake determinism failed: sha256 mismatch ({h1} vs {h2})"
        passed += 1

        # 3. Report JSON has V1..V10 with ok=true
        assert os.path.exists(report_file1), "Report JSON missing"
        with open(report_file1, "r") as rf:
            rep = json.load(rf)
        for v_num in range(1, 11):
            key = [k for k in rep.keys() if k.startswith(f"V{v_num}_")]
            assert len(key) == 1, f"Missing report key for V{v_num}"
            assert rep[key[0]].get("ok") is True, f"Validator {key[0]} was not ok: {rep[key[0]]}"
        passed += 1

        # 4. Preview PNG signature, IHDR, and dimensions 1024x1024
        assert os.path.exists(preview_file1), "Preview PNG missing"
        with open(preview_file1, "rb") as pf:
            png_bytes = pf.read()
        assert png_bytes[:8] == b"\x89PNG\r\n\x1a\n", "Bad PNG signature"
        # IHDR is at offset 8 (length 4 bytes, type 4 bytes, data 13 bytes)
        ihdr_type = png_bytes[12:16]
        assert ihdr_type == b"IHDR", f"Expected IHDR chunk, got {ihdr_type}"
        w, h = struct.unpack(">II", png_bytes[16:24])
        assert (w, h) == (1024, 1024), f"Expected (1024, 1024) PNG, got ({w}, {h})"
        passed += 1

        # 5. Invalid arguments check (--size 300 exits code 2)
        cmd_bad = [sys.executable, bake_script, "--size", "300"]
        proc_bad = subprocess.run(cmd_bad, capture_output=True, text=True)
        assert proc_bad.returncode == 2, f"Expected return code 2 for bad size, got {proc_bad.returncode}"
        passed += 1

    print(f"GROUP D PASS ({passed}/5)")



def test_group_c():
    """Group C tests: Layers, placement algorithms, and validation checks."""
    passed = 0

    # 1. compute_layers on synthetic field
    field = np.full((512, 512), 10.0, dtype=np.float32)
    field[:, :255] = -20.0 # DeepWater
    # Put a beach cell at (row 100, col 300)
    field[100, 300] = 1.0 # hm < 2.5 and slope <= 12 -> Beach

    layers = compute_layers(field, 512)
    assert layers.surface[100, 10] == Surface.DeepWater, "Expected DeepWater on left"
    assert layers.surface[100, 400] == Surface.Land, "Expected Land on right"
    assert layers.surface[100, 300] == Surface.Beach, "Expected Beach at (100, 300)"
    assert layers.surface[100, 255] == Surface.Cliff, "Expected Cliff at steep transition"
    assert layers.distwater[100, 254] == 0, "Water cell should have distwater 0"
    assert layers.distwater[100, 255] == 1, "Cell adjacent to water should have distwater 1"
    assert layers.distwater[100, 256] == 2, "Cell at distance 2 should have distwater 2"
    passed += 1

    # 2. distwater matches brute-force Manhattan distance
    test_w = np.zeros((64, 64), dtype=bool)
    water_pts = [(10, 15), (20, 45), (50, 30)]
    for r, c in water_pts:
        test_w[r, c] = True

    dt = compute_distwater(test_w)
    brute = np.zeros((64, 64), dtype=np.uint8)
    for r in range(64):
        for c in range(64):
            min_d = min(abs(r - wr) + abs(c - wc) for wr, wc in water_pts)
            brute[r, c] = min(min_d, 255)
    assert np.array_equal(dt, brute), "distwater did not match brute-force Manhattan distance"
    passed += 1

    # 3. Flood fill component size on 16x16 maze
    maze = [
        "################",
        "#...#..........#",
        "#.#.#.########.#",
        "#.#.#.#......#.#",
        "#...#.#.####.#.#",
        "#####.#.#..#.#.#",
        "#.....#.#..#.#.#",
        "#.#####.####.#.#",
        "#.#..........#.#",
        "#.#.##########.#",
        "#.#............#",
        "#.############.#",
        "#..............#",
        "################",
        "################",
        "################"
    ]
    passable_grid = np.array([[c == '.' for c in row] for row in maze], dtype=bool)
    visited = np.zeros_like(passable_grid)
    queue = [(1, 1)]
    visited[1, 1] = True
    comp_count = 0
    while queue:
        r, c = queue.pop(0)
        comp_count += 1
        for dr, dc in ((-1, 0), (1, 0), (0, -1), (0, 1)):
            nr, nc = r + dr, c + dc
            if 0 <= nr < 16 and 0 <= nc < 16 and passable_grid[nr, nc] and not visited[nr, nc]:
                visited[nr, nc] = True
                queue.append((nr, nc))
    # Manual flood count check: starts in (1,1)-(4,3) pocket of 10 cells
    assert comp_count == 10, f"Expected 10 cells in maze pocket, got {comp_count}"
    passed += 1

    # 4. flatten_plateau on synthetic terrain
    syn_h = np.full((512, 512), 40.0, dtype=np.float32)
    syn_before = syn_h.copy()
    flatten_plateau(syn_h, 8.0, 2048.0, 2048.0)
    codes_after = m_to_code(syn_h)

    # Check flat within 64m
    cols, rows = np.meshgrid(np.arange(512, dtype=np.float32) * 8.0,
                             np.arange(512, dtype=np.float32) * 8.0)
    r2 = (cols - 2048.0) ** 2 + (rows - 2048.0) ** 2
    flat_codes = codes_after[r2 <= 64.0 * 64.0]
    assert np.all(flat_codes == flat_codes[0]), "Plateau within 64m not flat"
    far_codes = codes_after[r2 >= 200.0 * 200.0]
    far_before = m_to_code(syn_before)[r2 >= 200.0 * 200.0]
    assert np.array_equal(far_codes, far_before), "Codes at r>=200m were modified"
    passed += 1

    # 5. pick_starts on synthetic square landmass
    cands = []
    for r in range(50, 450, 10):
        for c in range(50, 450, 10):
            cands.append((float(c) * 8.0, float(r) * 8.0))
    starts1 = pick_starts(cands, (2048.0, 2048.0), Rng(42))
    starts2 = pick_starts(cands, (2048.0, 2048.0), Rng(42))
    assert len(starts1) == 8, f"Expected 8 starts, got {len(starts1)}"
    for i in range(8):
        assert starts1[i]["x_m"] == starts2[i]["x_m"] and starts1[i]["z_m"] == starts2[i]["z_m"], "pick_starts non-deterministic"
        for j in range(i + 1, 8):
            d = np.sqrt((starts1[i]["x_m"] - starts1[j]["x_m"])**2 + (starts1[i]["z_m"] - starts1[j]["z_m"])**2)
            assert d >= 600.0, f"Starts {i} and {j} too close: {d}m"
    passed += 1

    # 6. validate() on passing and failing sets
    # Construct a valid synthetic configuration
    val_h = np.full((512, 512), 20.0, dtype=np.float32)
    # Set 46% land, 54% sea
    val_h[:276, :] = -20.0
    val_layers = compute_layers(val_h, 512)
    val_layers.surface[:276, :] = Surface.DeepWater
    val_layers.surface[276:280, :] = Surface.Beach
    val_layers.surface[280:, :] = Surface.Land

    val_biome = np.full((512, 512), Biome.Grass, dtype=np.uint8)
    val_biome[val_layers.surface == Surface.DeepWater] = Biome.Ocean
    val_biome[val_layers.surface == Surface.Beach] = Biome.Beach

    val_starts = []
    for i in range(8):
        sx = 500.0 + i * 400.0
        sz = 2500.0 + (i % 2) * 600.0
        code, _ = flatten_plateau(val_h, 8.0, sx, sz)
        val_starts.append({
            "slot": i, "flags": 1, "x_m": sx, "z_m": sz,
            "x_q": q16(sx), "z_q": q16(sz), "plateau_hcode": code, "flat_radius_q": q16(64.0)
        })

    # Kit nodes for all 8 starts + minimal neutral nodes
    from tools.theatre_baker.params import KIT_RESOURCES, NEUTRAL_RESOURCES
    val_res = []
    for s in val_starts:
        for kind, count, _, _, amount, foot in KIT_RESOURCES:
            for _ in range(count):
                val_res.append({
                    "kind": kind, "owner": s["slot"], "amount": amount,
                    "x_m": s["x_m"] + 90.0, "z_m": s["z_m"],
                    "x_q": q16(s["x_m"] + 90.0), "z_q": q16(s["z_m"]),
                    "footprint_m": foot, "footprint_q": q16(foot)
                })

    for _, kind, _, min_cnt, _, amount, foot in NEUTRAL_RESOURCES:
        for _ in range(min_cnt):
            val_res.append({
                "kind": kind, "owner": 0xFF, "amount": amount,
                "x_m": 100.0, "z_m": 3500.0,
                "x_q": q16(100.0), "z_q": q16(3500.0),
                "footprint_m": foot, "footprint_q": q16(foot)
            })

    val_landings = [
        {"x_m": (100.0 + i * 20.0), "z_m": 2220.0, "x_q": q16(100.0 + i * 20.0), "z_q": q16(2220.0), "heading_a16": 0}
        for i in range(8)
    ]

    ok, rep, _ = validate(val_h, val_layers, val_biome, val_starts, val_res, val_landings, 512)
    assert ok, f"Expected synthetic validate to pass: {rep}"

    # Now break plateau flatness on start 0
    val_starts[0]["plateau_hcode"] ^= 0x5555
    ok_fail, rep_fail, _ = validate(val_h, val_layers, val_biome, val_starts, val_res, val_landings, 512)
    assert not ok_fail, "Expected validate to fail with corrupted plateau code"
    assert not rep_fail["V3_starts_and_plateaus"]["ok"], "V3 should have flagged failure"
    passed += 1

    print(f"GROUP C PASS ({passed}/6)")


if __name__ == "__main__":
    group = "A"
    if len(sys.argv) > 1 and sys.argv[1] == "--group":
        group = sys.argv[2] if len(sys.argv) > 2 else "A"

    if group in ("A", "ALL"):
        test_group_a()
    if group in ("B", "ALL"):
        test_group_b()
    if group in ("C", "ALL"):
        test_group_c()
    if group in ("D", "ALL"):
        test_group_d()

    if group == "ALL":
        print("\nALL GROUPS PASS (23/23)")
