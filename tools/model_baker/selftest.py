"""selftest.py - Automated self-test runner for Model Baker pipeline."""

import argparse
import json
import math
import os
import sys

_TOOLS_DIR = os.path.abspath(os.path.join(os.path.dirname(__file__), ".."))
_OWN_DIR = os.path.abspath(os.path.dirname(__file__))
if _TOOLS_DIR not in sys.path:
    sys.path.insert(0, _TOOLS_DIR)
if _OWN_DIR not in sys.path:
    sys.path.insert(0, _OWN_DIR)

import numpy as np
from asset_common.rng import Rng
from asset_common.hashing import xxh64, fnv1a64
from asset_common.pngio import write_png, read_png_info, decode_png_rgb
import params
import trims
import octa
import prims
import meshbuilder
import anim
import rig
import vat


def _signed_vol(v: np.ndarray, t: np.ndarray) -> float:
    v0 = v[t[:, 0]]
    v1 = v[t[:, 1]]
    v2 = v[t[:, 2]]
    return float(np.sum(v0 * np.cross(v1, v2)) / 6.0)


def _check_closed(pos: np.ndarray, tris: np.ndarray) -> tuple[bool, str]:
    key_map = {}
    remap = []
    for p in pos:
        key = (round(float(p[0]), 5), round(float(p[1]), 5), round(float(p[2]), 5))
        if key not in key_map:
            key_map[key] = len(key_map)
        remap.append(key_map[key])
    remap = np.array(remap)
    w_tris = remap[tris]
    edges = {}
    for tri in w_tris:
        for i in range(3):
            u, v = tri[i], tri[(i + 1) % 3]
            if u == v:
                return False, "Degenerate triangle found"
            edge = tuple(sorted((u, v)))
            edges[edge] = edges.get(edge, 0) + 1
    for edge, count in edges.items():
        if count != 2:
            return False, f"Edge {edge} shared by {count} triangles (expected 2)"
    return True, "OK"


# --- GROUP A TESTS ---

def test_group_a_1() -> bool:
    """Test 1: SplitMix64 deterministic PRNG outputs."""
    rng = Rng(0)
    o1 = rng.next_u64()
    o2 = rng.next_u64()
    o3 = rng.next_u64()
    assert o1 == 0xE220A8397B1DCDAF, f"Mismatch o1: {hex(o1)}"
    assert o2 == 0x6E789E6AA1B965F4, f"Mismatch o2: {hex(o2)}"
    assert o3 == 0x06C45D188009454F, f"Mismatch o3: {hex(o3)}"

    first_outputs = {Rng(s).next_u64() for s in range(1000)}
    assert len(first_outputs) == 1000, "1000 seeds did not give 1000 distinct first outputs"
    return True


def test_group_a_2() -> bool:
    """Test 2: XXH64 and FNV-1a-64 hashes."""
    assert xxh64(b"") == 0xEF46DB3751D8E999
    assert xxh64(b"a") == 0xD24EC4F1A98C6E5B
    assert xxh64(b"abc") == 0x44BC2CF5AD770999

    rng = Rng(42)
    rand_bytes = bytes([rng.randint(0, 255) for _ in range(1000)])
    h_bytes = xxh64(rand_bytes)
    h_mv = xxh64(memoryview(rand_bytes))
    assert h_bytes == h_mv, "Bytes and memoryview hash mismatch"

    assert fnv1a64("") == 0xCBF29CE484222325
    assert fnv1a64("a") == 0xAF63DC4C8601EC8C
    return True


def test_group_a_3() -> bool:
    """Test 3: Octahedral normal encoding roundtrip."""
    rng = Rng(12345)
    vecs = []
    for _ in range(20000):
        u1 = max(1e-15, rng.uniform())
        u2 = rng.uniform()
        u3 = max(1e-15, rng.uniform())
        u4 = rng.uniform()
        r1 = math.sqrt(-2.0 * math.log(u1))
        r2 = math.sqrt(-2.0 * math.log(u3))
        x = r1 * math.cos(2.0 * math.pi * u2)
        y = r1 * math.sin(2.0 * math.pi * u2)
        z = r2 * math.cos(2.0 * math.pi * u4)
        length = math.sqrt(x * x + y * y + z * z)
        vecs.append((x / length, y / length, z / length))

    vecs = np.array(vecs, dtype=np.float64)
    sn = octa.encode(vecs)
    dec = octa.decode(sn)

    dots = np.clip(np.sum(vecs * dec, axis=-1), -1.0, 1.0)
    ang_err_deg = np.degrees(np.arccos(dots))
    max_err = np.max(ang_err_deg)
    assert max_err < 0.05, f"Octa roundtrip max angular error too high: {max_err} deg"

    axes = np.array([
        [1.0, 0.0, 0.0], [-1.0, 0.0, 0.0],
        [0.0, 1.0, 0.0], [0.0, -1.0, 0.0],
        [0.0, 0.0, 1.0], [0.0, 0.0, -1.0],
    ], dtype=np.float64)
    dec_axes = octa.decode(octa.encode(axes))
    axis_diff = np.max(np.abs(axes - dec_axes))
    assert axis_diff < 1e-4, f"Axis vector roundtrip diff: {axis_diff}"

    neg_z = np.array([[0.0, 0.0, -1.0]], dtype=np.float64)
    dec_neg_z = octa.decode(octa.encode(neg_z))
    assert not np.isnan(dec_neg_z).any(), "NaN on (0, 0, -1)"
    assert np.allclose(dec_neg_z, neg_z, atol=1e-4), f"Failed on (0, 0, -1): {dec_neg_z}"
    return True


def test_group_a_4() -> bool:
    """Test 4: Trim sheet configuration and trims.json export."""
    assert len(trims.TRIMS) == 32
    for t in range(32):
        assert trims.TRIMS[t]["id"] == t
    names = [t["name"] for t in trims.TRIMS]
    assert len(set(names)) == 32
    assert names == trims.TRIM_NAMES

    rects = [t["px"] for t in trims.TRIMS]
    for r in rects:
        assert 0 <= r[0] < r[2] <= 2048
        assert 0 <= r[1] < r[3] <= 2048

    for i in range(32):
        for j in range(i + 1, 32):
            r1, r2 = rects[i], rects[j]
            dx = max(0, r1[0] - r2[2], r2[0] - r1[2])
            dy = max(0, r1[1] - r2[3], r2[1] - r1[3])
            assert max(dx, dy) >= 8, f"Gap < 8 between trims {i} and {j}"

    u0, v0, u1, v1 = trims.trim_rect_uv(0)
    assert abs(u0 - 4.0 / 2048.0) < 1e-12
    assert abs(v0 - 4.0 / 2048.0) < 1e-12
    assert abs(u1 - 252.0 / 2048.0) < 1e-12
    assert abs(v1 - 252.0 / 2048.0) < 1e-12

    tmp_path = os.path.join(_OWN_DIR, "tmp_trims_test.json")
    trims.write_trims_json(tmp_path)
    with open(tmp_path, "r", encoding="utf-8") as f:
        re_read = json.load(f)
    os.remove(tmp_path)
    assert re_read == trims.TRIMS

    for t in trims.TRIMS:
        rgb = t["preview_rgb"]
        assert len(rgb) == 3
        for c in rgb:
            assert isinstance(c, int) and 0 <= c <= 255
    return True


def test_group_a_5() -> bool:
    """Test 5: PNG I/O and CRC error detection."""
    test_img = np.zeros((3, 4, 3), dtype=np.uint8)
    for y in range(3):
        for x in range(4):
            test_img[y, x] = [y * 40, x * 50, (x + y) * 25]

    tmp_png = os.path.join(_OWN_DIR, "tmp_selftest_a5.png")
    write_png(tmp_png, test_img)

    w, h = read_png_info(tmp_png)
    assert (w, h) == (4, 3), f"PNG info mismatch: {(w, h)}"

    dec = decode_png_rgb(tmp_png)
    assert np.array_equal(test_img, dec), "Decoded PNG RGB mismatch"

    with open(tmp_png, "rb") as f:
        raw_b = bytearray(f.read())
    raw_b[29] ^= 0xFF
    with open(tmp_png, "wb") as f:
        f.write(raw_b)

    raised = False
    try:
        read_png_info(tmp_png)
    except ValueError:
        raised = True
    finally:
        if os.path.exists(tmp_png):
            os.remove(tmp_png)

    assert raised, "Corrupted CRC did not raise ValueError"
    return True


def test_group_a_6() -> bool:
    """Test 6: unorm16 quantization and inverse mapping."""
    u_0 = octa.unorm16(0.0)
    u_half = octa.unorm16(0.5)
    u_1 = octa.unorm16(1.0)
    assert u_0 == 0
    assert abs(int(u_half) - 32767.5) <= 1.0, f"u_half was {u_half}"
    assert u_1 == 65535

    test_vals = np.linspace(0.0, 1.0, 10000, dtype=np.float64)
    quant = octa.unorm16(test_vals)
    inv = octa.unorm16_to_float(quant)
    max_err = np.max(np.abs(test_vals - inv))
    assert max_err <= (1.0 / 65535.0 + 1e-12), f"Inverse error too high: {max_err}"
    return True


# --- GROUP B TESTS ---

def test_group_b_1() -> bool:
    """Test B.1: box(2, 3, 4) volume and normal dot products."""
    b = prims.box(2, 3, 4)
    assert len(b.tris) == 12, f"Box tris: {len(b.tris)}"
    assert len(b.pos) == 24, f"Box verts: {len(b.pos)}"
    vol = _signed_vol(b.pos, b.tris)
    assert abs(vol - 24.0) < 1e-5, f"Box volume: {vol}"

    v0 = b.pos[b.tris[:, 0]]
    v1 = b.pos[b.tris[:, 1]]
    v2 = b.pos[b.tris[:, 2]]
    gn = np.cross(v1 - v0, v2 - v0)
    gn /= np.linalg.norm(gn, axis=1, keepdims=True)
    for i in range(len(b.tris)):
        for k in range(3):
            dot_val = np.dot(b.nrm[b.tris[i, k]], gn[i])
            assert dot_val > 0.999, f"Normal dot product too low: {dot_val}"
    return True


def test_group_b_2() -> bool:
    """Test B.2: Primitive volumes and areas."""
    cyl = prims.cylinder(1, 1, 2, lod=0)
    expected_cyl = 0.5 * 12 * math.sin(2.0 * math.pi / 12) * 1.0**2 * 2.0
    vol_cyl = _signed_vol(cyl.pos, cyl.tris)
    assert abs(vol_cyl - expected_cyl) < 1e-4, f"Cyl vol: {vol_cyl} vs {expected_cyl}"

    c1 = prims.cylinder(1, 0, 2, lod=0)
    c2 = prims.cylinder(0, 1, 2, lod=0)
    assert _signed_vol(c1.pos, c1.tris) > 0, "Cone (1,0) volume <= 0"
    assert _signed_vol(c2.pos, c2.tris) > 0, "Cone (0,1) volume <= 0"
    for cone in (c1, c2):
        v0 = cone.pos[cone.tris[:, 0]]
        v1 = cone.pos[cone.tris[:, 1]]
        v2 = cone.pos[cone.tris[:, 2]]
        areas = 0.5 * np.linalg.norm(np.cross(v1 - v0, v2 - v0), axis=1)
        assert np.min(areas) > 1e-9, "Cone has zero-area triangle"

    tb = prims.taper_box(2, 3, 1, 2, 4)
    assert _signed_vol(tb.pos, tb.tris) > 0, "Taper_box volume <= 0"

    w = prims.wedge(2, 1, 3)
    vol_w = _signed_vol(w.pos, w.tris)
    assert abs(vol_w - 3.0) < 1e-5, f"Wedge volume: {vol_w}"

    poly = [(1.0, 0.0), (0.3, 0.9), (-0.8, 0.5), (-0.6, -0.7), (0.5, -0.8)]
    h = 2.0
    pr = prims.prism(poly, h)
    area = 0.5 * abs(sum(poly[i][0] * poly[(i + 1) % 5][1] - poly[(i + 1) % 5][0] * poly[i][1] for i in range(5)))
    vol_pr = _signed_vol(pr.pos, pr.tris)
    assert abs(vol_pr - area * h) < 1e-4, f"Prism vol: {vol_pr} vs {area * h}"
    return True


def test_group_b_3() -> bool:
    """Test B.3: Watertightness / CLOSED check on all primitives."""
    poly = [(1.0, 0.0), (0.3, 0.9), (-0.8, 0.5), (-0.6, -0.7), (0.5, -0.8)]
    prims_to_test = [
        ("box", prims.box(1, 2, 3)),
        ("taper_box", prims.taper_box(2, 3, 1, 2, 4)),
        ("cylinder", prims.cylinder(1, 1, 2, 0)),
        ("cone", prims.cylinder(1, 0, 2, 0)),
        ("sphere0", prims.sphere(1.0, 0)),
        ("sphere1", prims.sphere(1.0, 1)),
        ("sphere2", prims.sphere(1.0, 2)),
        ("sphere_jit", prims.sphere(1.0, 0, flat=True, jitter=(0.2, 7))),
        ("wedge", prims.wedge(2, 1, 3)),
        ("prism", prims.prism(poly, 2.0)),
    ]
    for name, p in prims_to_test:
        ok, msg = _check_closed(p.pos, p.tris)
        assert ok, f"Primitive {name} failed CLOSED check: {msg}"

    s0 = prims.sphere(1.0, 0)
    vol_s0 = _signed_vol(s0.pos, s0.tris)
    expected_s = 4.0 / 3.0 * math.pi * 1.0**3
    assert 0.80 * expected_s <= vol_s0 <= 1.0 * expected_s, f"Sphere volume ratio: {vol_s0 / expected_s}"
    return True


def test_group_b_4() -> bool:
    """Test B.4: meshbuilder.place rotations and mirroring."""
    b_rot = meshbuilder.place(prims.box(1, 2, 3), (0, 0, 0), rot_deg=(0, 90, 0))
    min_x, max_x = np.min(b_rot.pos[:, 0]), np.max(b_rot.pos[:, 0])
    min_z, max_z = np.min(b_rot.pos[:, 2]), np.max(b_rot.pos[:, 2])
    assert abs((max_x - min_x) - 3.0) < 1e-5, f"Rotated X extent: {max_x - min_x}"
    assert abs((max_z - min_z) - 1.0) < 1e-5, f"Rotated Z extent: {max_z - min_z}"

    off_box = meshbuilder.place(prims.box(1, 2, 3), (5, 6, 7))
    b_mir = meshbuilder.place(prims.box(1, 2, 3), (5, 6, 7), mirror_z=True)
    assert abs(np.min(b_mir.pos[:, 2]) - (-np.max(off_box.pos[:, 2]))) < 1e-5
    assert abs(np.max(b_mir.pos[:, 2]) - (-np.min(off_box.pos[:, 2]))) < 1e-5
    assert _signed_vol(b_mir.pos, b_mir.tris) > 0, "Mirrored box signed vol <= 0"
    assert len(b_mir.tris) == len(off_box.tris)

    pt_rot = meshbuilder.make_rotation_matrix((0, 0, 90)) @ np.array([1.0, 0.0, 0.0], dtype=np.float32)
    assert np.allclose(pt_rot, [0.0, 1.0, 0.0], atol=1e-6), f"Rotation order point: {pt_rot}"
    return True


def test_group_b_5() -> bool:
    """Test B.5: UV ranges and assemble sheet mapping."""
    poly = [(1.0, 0.0), (0.3, 0.9), (-0.8, 0.5), (-0.6, -0.7), (0.5, -0.8)]
    for p in [
        prims.box(1, 1, 1),
        prims.cylinder(1, 1, 1, 0),
        prims.sphere(1, 0),
        prims.wedge(1, 1, 1),
        prims.prism(poly, 1),
    ]:
        assert np.all(p.uv >= -1e-7) and np.all(p.uv <= 1.0 + 1e-7), "UV out of [0, 1]"

    part0 = meshbuilder.Part(
        part=0,
        make=lambda lod: prims.box(1, 1, 1),
        trim="steel_plain",
        team=True,
        emissive=False,
        scroll=False,
    )
    mesh = meshbuilder.assemble([part0], lod=0)
    u0, v0, u1, v1 = trims.trim_rect_uv(0)
    assert np.all(mesh.uv_sheet[:, 0] >= u0 - 1e-7)
    assert np.all(mesh.uv_sheet[:, 0] <= u1 + 1e-7)
    assert np.all(mesh.uv_sheet[:, 1] >= v0 - 1e-7)
    assert np.all(mesh.uv_sheet[:, 1] <= v1 + 1e-7)
    assert np.all((mesh.flags & params.FLAG_TEAM_COLOR) != 0)
    return True


def test_group_b_6() -> bool:
    """Test B.6: LOD tables and part masking."""
    s0 = prims.sphere(1, 0, flat=True)
    s1 = prims.sphere(1, 1, flat=True)
    s2 = prims.sphere(1, 2, flat=True)
    assert len(s0.tris) == 168 and len(s1.tris) == 80 and len(s2.tris) == 36, (
        f"Sphere tri counts: {len(s0.tris)}, {len(s1.tris)}, {len(s2.tris)}"
    )

    c0 = prims.cylinder(1, 1, 2, 0)
    c1 = prims.cylinder(1, 1, 2, 1)
    c2 = prims.cylinder(1, 1, 2, 2)
    assert len(c0.tris) == 44 and len(c1.tris) == 28 and len(c2.tris) == 20, (
        f"Cylinder tri counts: {len(c0.tris)}, {len(c1.tris)}, {len(c2.tris)}"
    )

    p1 = meshbuilder.Part(part=0, make=lambda lod: prims.box(1, 1, 1))
    p2 = meshbuilder.Part(part=1, make=lambda lod: prims.box(2, 2, 2))
    m_masked = meshbuilder.assemble([p1, p2], lod=0, mask=[True, False])
    assert len(m_masked.pos) == 24, f"Masked mesh vert count: {len(m_masked.pos)}"
    return True


# --- GROUP C TESTS ---

def test_group_c_1() -> bool:
    """Test C.1: Pivot rotations and fixed points."""
    p = (1.0, 0.0, 0.0)
    op = anim.tip(2, 90.0)
    m = op.matrix(1.0, p)
    pt = m @ np.array([2.0, 0.0, 0.0, 1.0])
    assert np.allclose(pt[:3], [1.0, 1.0, 0.0], atol=1e-9)
    pt_piv = m @ np.array([1.0, 0.0, 0.0, 1.0])
    assert np.allclose(pt_piv[:3], [1.0, 0.0, 0.0], atol=1e-9)
    return True


def test_group_c_2() -> bool:
    """Test C.2: Shifts (bob, kick, sink)."""
    b = anim.bob(1, 0.5, 1)
    mb = b.matrix(0.25, (0, 0, 0))
    assert abs(mb[1, 3] - 0.5) < 1e-9

    k = anim.kick(0, 1.0, peak=0.2)
    mk_peak = k.matrix(0.2, (0, 0, 0))
    assert abs(mk_peak[0, 3] - 1.0) < 1e-9
    mk_end = k.matrix(1.0, (0, 0, 0))
    assert abs(mk_end[0, 3] - 0.0) < 1e-9

    s = anim.sink(1, -2.0)
    ms = s.matrix(1.0, (0, 0, 0))
    assert abs(ms[1, 3] - (-2.0)) < 1e-9
    return True


def test_group_c_3() -> bool:
    """Test C.3: Rig hierarchy and forward kinematics chain."""
    r = rig.Rig()
    r.add_part("root", (0, 0, 0), parent=-1)
    r.add_part("child", (2, 0, 0), parent=0)
    r.set_ops(0, "Die", [anim.tip(1, 90.0)])
    r.set_ops(1, "Die", [anim.tip(2, 90.0)])
    mats = r.world_matrices(3, 11)
    pt_bind = np.array([3.0, 0.0, 0.0, 1.0])
    res = mats[1] @ pt_bind
    assert np.allclose(res[:3], [0.0, 1.0, -2.0], atol=1e-9)

    try:
        r_bad = rig.Rig()
        r_bad.add_part("part0", (0, 0, 0), parent=1)
        assert False, "Should have raised ValueError"
    except ValueError:
        pass
    return True


def test_group_c_4() -> bool:
    """Test C.4: Zero at t=0 and boundary identities."""
    all_ops = [
        anim.sway(0, 45.0, 1), anim.spin(1, 2), anim.bob(2, 1.0, 1),
        anim.pulse(0, 30.0, 0.35), anim.kick(1, 0.5, 0.2),
        anim.tip(2, 90.0), anim.sink(1, -1.0),
    ]
    for op in all_ops:
        m0 = op.matrix(0.0, (1, 2, 3))
        assert np.allclose(m0, np.eye(4), atol=1e-12)

    for op in [anim.sway(0, 45.0, 1), anim.spin(1, 2), anim.bob(2, 1.0, 1), anim.pulse(0, 30.0, 0.35), anim.kick(1, 0.5, 0.2)]:
        m1 = op.matrix(1.0, (1, 2, 3))
        assert np.allclose(m1, np.eye(4), atol=1e-9)

    m_tip = anim.tip(2, 90.0).matrix(1.0, (0, 0, 0))
    assert np.allclose(m_tip[:3, :3], [[0, -1, 0], [1, 0, 0], [0, 0, 1]], atol=1e-9)
    return True


def test_group_c_5() -> bool:
    """Test C.5: pack_half layout, precision, and roundtrip."""
    r_rand = rig.Rig()
    r_rand.add_part("root", (0, 0, 0))
    r_rand.set_ops(0, "Idle", [anim.sway(0, 15.0, 1), anim.sway(1, 20.0, 1)])
    rows = vat.build_model_rows(r_rand)
    half = vat.pack_half(rows)
    assert half.shape == (44, 48, 4)
    assert half.dtype == np.float16

    half_f = half.astype(np.float32)
    for f in range(44):
        for p in range(16):
            if p == 0:
                m33 = half_f[f, 0:3, :3]
                norms = np.linalg.norm(m33, axis=1)
                for n in norms:
                    assert abs(n - 1.0) < 0.005
            else:
                m4 = half_f[f, p * 3 : (p + 1) * 3, :]
                assert np.allclose(m4, [[1, 0, 0, 0], [0, 1, 0, 0], [0, 0, 1, 0]], atol=1e-3)
    return True


def test_group_c_6() -> bool:
    """Test C.6: build_vat across models and clip indexing."""
    r1 = rig.Rig()
    r1.add_part("root", (0, 0, 0))
    r2 = rig.Rig()
    r2.add_part("root", (0, 0, 0))
    vats = vat.build_vat([r1, r2])
    assert vats.shape == (88, 48, 4)
    row_idx = 44 + 24 + 3
    assert row_idx == 71

    for clip_idx in range(4):
        c_start = [0, 8, 24, 32][clip_idx]
        for m_idx in range(2):
            row = vats[m_idx * 44 + c_start]
            for p in range(16):
                m3 = row[p * 3 : (p + 1) * 3, :].astype(np.float32)
                assert np.allclose(m3, [[1, 0, 0, 0], [0, 1, 0, 0], [0, 0, 1, 0]], atol=1e-3)
    return True


def run_group_a() -> bool:
    print("Running GROUP A tests...")
    tests = [
        ("Test A.1: PRNG Determinism", test_group_a_1),
        ("Test A.2: Hashes (XXH64 & FNV-1a)", test_group_a_2),
        ("Test A.3: Octahedral Normals", test_group_a_3),
        ("Test A.4: Trim Sheet Setup", test_group_a_4),
        ("Test A.5: PNG I/O & CRC", test_group_a_5),
        ("Test A.6: unorm16 Accuracy", test_group_a_6),
    ]
    passed = 0
    for name, fn in tests:
        try:
            ok = fn()
            if ok:
                print(f"  {name}: PASS")
                passed += 1
            else:
                print(f"  {name}: FAIL")
        except Exception as e:
            print(f"  {name}: FAIL ({e})")
            import traceback
            traceback.print_exc()

    print(f"GROUP A {'PASS' if passed == len(tests) else 'FAIL'} ({passed}/{len(tests)})")
    return passed, len(tests)


def run_group_b() -> tuple[int, int]:
    print("Running GROUP B tests...")
    tests = [
        ("Test B.1: Box Geometry & Normals", test_group_b_1),
        ("Test B.2: Primitive Volumes & Areas", test_group_b_2),
        ("Test B.3: Watertightness Check", test_group_b_3),
        ("Test B.4: Transformations & Mirroring", test_group_b_4),
        ("Test B.5: UV Ranges & Assembly", test_group_b_5),
        ("Test B.6: LOD Levels & Masking", test_group_b_6),
    ]
    passed = 0
    for name, fn in tests:
        try:
            ok = fn()
            if ok:
                print(f"  {name}: PASS")
                passed += 1
            else:
                print(f"  {name}: FAIL")
        except Exception as e:
            print(f"  {name}: FAIL ({e})")
            import traceback
            traceback.print_exc()

    print(f"GROUP B {'PASS' if passed == len(tests) else 'FAIL'} ({passed}/{len(tests)})")
    return passed, len(tests)


def run_group_c() -> tuple[int, int]:
    print("Running GROUP C tests...")
    tests = [
        ("Test C.1: Pivot Rotations", test_group_c_1),
        ("Test C.2: Animation Shifts", test_group_c_2),
        ("Test C.3: Skeletal Hierarchy & Chain", test_group_c_3),
        ("Test C.4: Zero At Origin & Boundaries", test_group_c_4),
        ("Test C.5: VAT Half Packing & Accuracy", test_group_c_5),
        ("Test C.6: Full VAT Baking & Indexing", test_group_c_6),
    ]
    passed = 0
    for name, fn in tests:
        try:
            ok = fn()
            if ok:
                print(f"  {name}: PASS")
                passed += 1
            else:
                print(f"  {name}: FAIL")
        except Exception as e:
            print(f"  {name}: FAIL ({e})")
            import traceback
            traceback.print_exc()

    print(f"GROUP C {'PASS' if passed == len(tests) else 'FAIL'} ({passed}/{len(tests)})")
    return passed, len(tests)


def run_group_d() -> tuple[int, int]:
    import catalogue
    import checks
    models = catalogue.build_catalogue()[:23]
    print(f"Running GROUP D tests on {len(models)} models...")
    tests = [
        ("Test D.1: M1 Structure", lambda: checks.check_m1_structure(models, expected_count=23)),
        ("Test D.2: M2 Geometry", lambda: checks.check_m2_geometry(models)),
        ("Test D.3: M3 Size & Placement", lambda: checks.check_m3_size_placement(models)),
        ("Test D.4: M4 Budgets", lambda: checks.check_m4_budgets(models)),
        ("Test D.5: M5 Animation", lambda: checks.check_m5_animation(models)),
        ("Test D.6: M6 Hardpoints & Team Colour", lambda: checks.check_m6_hardpoints_team(models)),
        ("Test D.7: M7 Determinism", lambda: checks.check_m7_determinism(models)),
    ]
    passed = 0
    for name, fn in tests:
        try:
            ok, msgs = fn()
            if ok:
                print(f"  {name}: PASS")
                passed += 1
            else:
                print(f"  {name}: FAIL ({msgs[:3]})")
        except Exception as e:
            print(f"  {name}: FAIL ({e})")
            import traceback
            traceback.print_exc()

    print(f"GROUP D {'PASS' if passed == len(tests) else 'FAIL'} ({passed}/{len(tests)})")
    return passed, len(tests)


def run_group_e() -> tuple[int, int]:
    import catalogue
    import checks
    models = catalogue.build_catalogue()[:36]
    print(f"Running GROUP E tests on {len(models)} models...")
    tests = [
        ("Test E.1: M1 Structure", lambda: checks.check_m1_structure(models, expected_count=36)),
        ("Test E.2: M2 Geometry", lambda: checks.check_m2_geometry(models)),
        ("Test E.3: M3 Size & Placement", lambda: checks.check_m3_size_placement(models)),
        ("Test E.4: M4 Budgets", lambda: checks.check_m4_budgets(models)),
        ("Test E.5: M5 Animation", lambda: checks.check_m5_animation(models)),
        ("Test E.6: M6 Hardpoints & Team Colour", lambda: checks.check_m6_hardpoints_team(models)),
        ("Test E.7: M7 Determinism", lambda: checks.check_m7_determinism(models)),
    ]
    passed = 0
    for name, fn in tests:
        try:
            ok, msgs = fn()
            if ok:
                print(f"  {name}: PASS")
                passed += 1
            else:
                print(f"  {name}: FAIL ({msgs[:3]})")
        except Exception as e:
            print(f"  {name}: FAIL ({e})")
            import traceback
            traceback.print_exc()

    print(f"GROUP E {'PASS' if passed == len(tests) else 'FAIL'} ({passed}/{len(tests)})")
    return passed, len(tests)


def run_group_f() -> tuple[int, int]:
    import catalogue
    import checks
    models = catalogue.build_catalogue()[:71]
    print(f"Running GROUP F tests on {len(models)} models...")
    tests = [
        ("Test F.1: M1 Structure", lambda: checks.check_m1_structure(models, expected_count=71)),
        ("Test F.2: M2 Geometry", lambda: checks.check_m2_geometry(models)),
        ("Test F.3: M3 Size & Placement", lambda: checks.check_m3_size_placement(models)),
        ("Test F.4: M4 Budgets", lambda: checks.check_m4_budgets(models)),
        ("Test F.5: M5 Animation", lambda: checks.check_m5_animation(models)),
        ("Test F.6: M6 Hardpoints & Team Colour", lambda: checks.check_m6_hardpoints_team(models)),
        ("Test F.7: M7 Determinism", lambda: checks.check_m7_determinism(models)),
    ]
    passed = 0
    for name, fn in tests:
        try:
            ok, msgs = fn()
            if ok:
                print(f"  {name}: PASS")
                passed += 1
            else:
                print(f"  {name}: FAIL ({msgs[:3]})")
        except Exception as e:
            print(f"  {name}: FAIL ({e})")
            import traceback
            traceback.print_exc()

    print(f"GROUP F {'PASS' if passed == len(tests) else 'FAIL'} ({passed}/{len(tests)})")
    return passed, len(tests)


def run_group_g() -> tuple[int, int]:
    import tempfile
    import shutil
    import hashlib
    import json
    import bake_models
    import mformat
    from asset_common.hashing import fnv1a64
    from asset_common.pngio import read_png_info, decode_png_rgb

    print("Running GROUP G tests...")
    tmpdir1 = tempfile.mkdtemp(prefix="bake_g1_")
    tmpdir2 = tempfile.mkdtemp(prefix="bake_g2_")
    try:
        def test_g_1():
            bake_models.bake(out_dir=tmpdir1, no_preview=False)
            mdbin = os.path.join(tmpdir1, "models_2026.mdbin")
            info = mformat.read_models(mdbin)
            assert info["model_count"] == 71
            assert info["vat_rows"] == 71 * 44 == 3124
            assert info["vat_cols"] == 48
            assert info["frames_per_model"] == 44
            assert info["section_count"] == 5
            # Test flipped payload byte
            with open(mdbin, "rb") as f:
                data = bytearray(f.read())
            data[200] ^= 0xFF
            bad_path = os.path.join(tmpdir1, "bad.mdbin")
            with open(bad_path, "wb") as f:
                f.write(data)
            try:
                mformat.read_models(bad_path)
                assert False, "Expected ValueError on flipped payload byte"
            except ValueError:
                pass
            return True

        def test_g_2():
            bake_models.bake(out_dir=tmpdir2, no_preview=True)
            with open(os.path.join(tmpdir1, "models_2026.mdbin"), "rb") as f:
                h1 = hashlib.sha256(f.read()).hexdigest()
            with open(os.path.join(tmpdir2, "models_2026.mdbin"), "rb") as f:
                h2 = hashlib.sha256(f.read()).hexdigest()
            assert h1 == h2, f"Bake determinism failed: {h1} != {h2}"
            return True

        def test_g_3():
            with open(os.path.join(tmpdir1, "models_2026.manifest.json"), "r") as f:
                manifest = json.load(f)
            assert len(manifest) == 71, f"Manifest length {len(manifest)} != 71"
            id_hashes = set()
            from collections import Counter
            cats = Counter()
            for model_id, entry in manifest.items():
                expected_hash = f"{fnv1a64(model_id):016x}"
                assert entry["id_hash"] == expected_hash, f"Hash mismatch for {model_id}"
                assert entry["id_hash"] not in id_hashes, f"Duplicate hash {entry['id_hash']}"
                id_hashes.add(entry["id_hash"])
                cats[entry["category"]] += 1
            expected_cats = [9, 14, 7, 6, 18, 4, 7, 6]
            actual_cats = [cats[c] for c in range(8)]
            assert actual_cats == expected_cats, f"Category counts {actual_cats} != {expected_cats}"
            return True

        def test_g_4():
            png_path = os.path.join(tmpdir1, "models_2026.preview.png")
            w, h = read_png_info(png_path)
            assert (w, h) == (1920, 960), f"Preview dimensions {(w, h)} != (1920, 960)"
            img = decode_png_rgb(png_path)
            non_bg_tiles = 0
            for idx in range(71):
                c = idx % 12
                r = idx // 12
                tile = img[r*160+1:(r+1)*160-1, c*160+1:(c+1)*160-1]
                is_not_bg = (tile[:,:,0] != 28) | (tile[:,:,1] != 30) | (tile[:,:,2] != 36)
                if int(is_not_bg.sum()) >= 300:
                    non_bg_tiles += 1
            assert non_bg_tiles >= 68, f"Only {non_bg_tiles}/71 tiles have >= 300 non-bg pixels"
            return True

        def test_g_5():
            mdbin = os.path.join(tmpdir1, "models_2026.mdbin")
            size = os.path.getsize(mdbin)
            assert size <= 24 * 1024 * 1024, f"File size {size} > 24 MB"
            info = mformat.read_models(mdbin)
            assert info["vertex_count"] <= 450000, f"Vertex count {info['vertex_count']} > 450000"
            assert info["vat_rows"] <= 8192, f"Vat rows {info['vat_rows']} > 8192"
            for m in info["models"]:
                for lod in range(3):
                    assert m["vtx_count"][lod] <= 65535, f"LOD{lod} vtx_count {m['vtx_count'][lod]} > 65535"
                    assert m["idx_count"][lod] % 3 == 0, f"LOD{lod} idx_count {m['idx_count'][lod]} % 3 != 0"
            indices = info["indices"]
            assert np.all(indices < 65536)
            return True

        tests = [
            ("Test G.1: Bake and Format Verification", test_g_1),
            ("Test G.2: Bake Determinism (SHA-256)", test_g_2),
            ("Test G.3: Manifest Verification", test_g_3),
            ("Test G.4: Preview PNG Verification", test_g_4),
            ("Test G.5: Asset Limits Verification", test_g_5),
        ]
        passed = 0
        for name, fn in tests:
            try:
                ok = fn()
                if ok:
                    print(f"  {name}: PASS")
                    passed += 1
                else:
                    print(f"  {name}: FAIL")
            except Exception as e:
                print(f"  {name}: FAIL ({e})")
                import traceback
                traceback.print_exc()

        print(f"GROUP G {'PASS' if passed == len(tests) else 'FAIL'} ({passed}/{len(tests)})")
        return passed, len(tests)
    finally:
        shutil.rmtree(tmpdir1, ignore_errors=True)
        shutil.rmtree(tmpdir2, ignore_errors=True)


def main():
    parser = argparse.ArgumentParser(description="Model Baker Self-test Runner")
    parser.add_argument("--group", choices=["A", "B", "C", "D", "E", "F", "G", "H", "ALL"], default="ALL")
    args = parser.parse_args()

    if args.group == "A":
        p, t = run_group_a()
        sys.exit(0 if p == t else 1)
    elif args.group == "B":
        p, t = run_group_b()
        sys.exit(0 if p == t else 1)
    elif args.group == "C":
        p, t = run_group_c()
        sys.exit(0 if p == t else 1)
    elif args.group == "D":
        p, t = run_group_d()
        sys.exit(0 if p == t else 1)
    elif args.group == "E":
        p, t = run_group_e()
        sys.exit(0 if p == t else 1)
    elif args.group == "F":
        p, t = run_group_f()
        sys.exit(0 if p == t else 1)
    elif args.group == "G":
        p, t = run_group_g()
        sys.exit(0 if p == t else 1)
    elif args.group == "ALL":
        pa, ta = run_group_a()
        pb, tb = run_group_b()
        pc, tc = run_group_c()
        pf, tf = run_group_f()
        pg, tg = run_group_g()
        tot_p = pa + pb + pc + pf + pg
        tot_t = ta + tb + tc + tf + tg
        all_pass = (tot_p == tot_t)
        print(f"\nALL GROUPS {'PASS' if all_pass else 'FAIL'} ({tot_p}/{tot_t})")
        sys.exit(0 if all_pass else 1)

    if args.group == "ALL":
        all_pass = all(results.values())
        print(f"\nALL GROUPS {'PASS' if all_pass else 'FAIL'} ({sum(results.values())}/{len(results)})")
        sys.exit(0 if all_pass else 1)
    else:
        sys.exit(0 if results.get(args.group, False) else 1)


if __name__ == "__main__":
    main()
