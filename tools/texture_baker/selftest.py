"""selftest.py - Validation test suite for texture baker (Groups A..F)."""

import argparse
import os
import sys
import tempfile
import numpy as np

sys.path.insert(0, os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

import json
from asset_common.noise import pnoise, pfbm, grid
from asset_common.bc import (
    encode_bc1, decode_bc1, encode_bc4, decode_bc4,
    encode_bc5, decode_bc5, encode_bc3, decode_bc3,
    decode_rgba8, decode_texel, level_size, psnr
)
from asset_common.mips import mip_chain
from asset_common.texformat import TexSpec, level_layer_offset, write_tex26, read_tex26
from asset_common.rng import Rng

import patterns
from trim_table import TRIM_SPECS
from trim_paint import paint_trim, height_to_normal, assemble_sheet
from terrain_tiles import paint_terrain_layer, build_terrain, TERRAIN_LAYERS
from vfx_effects import EFFECTS, render_frame
from vfx import assemble_vfx_atlas, frame_origin, VFX_INFO
import font_data
import shapes
import msdf
import msdf_atlas
import params



def test_group_a_1():
    """Test A.1: Noise periodic wrap and determinism."""
    px, py = 4, 4
    gx, gy = grid(64, px, py)
    v1 = pnoise(gx, gy, px, py, 42)
    v2 = pnoise(gx + np.float32(px), gy, px, py, 42)
    v3 = pnoise(gx, gy + np.float32(py), px, py, 42)
    assert np.array_equal(v1, v2)
    assert np.array_equal(v1, v3)
    assert v1.dtype == np.float32
    assert np.all(v1 >= 0.0) and np.all(v1 < 1.0)
    v_other = pnoise(gx, gy, px, py, 999)
    assert np.mean(np.abs(v1 - v_other)) > 0.05

    img = pfbm(gx, gy, px, py, 42, 4)
    diff_wrap = np.mean(np.abs(img[:, 63] - img[:, 0]))
    diff_adj = np.mean(np.abs(img[:, 1:] - img[:, :-1]))
    assert diff_wrap <= 2.0 * diff_adj
    return True


def test_group_a_2():
    """Test A.2: Exact hand-computed BC decode vectors."""
    v1_bytes = bytes([0x00, 0xF8, 0x1F, 0x00, 0xE4, 0xE4, 0xE4, 0xE4])
    dec_v1 = decode_bc1(v1_bytes, 4, 4)
    exp_row_v1 = np.array([
        [255, 0, 0, 255],
        [0, 0, 255, 255],
        [170, 0, 85, 255],
        [85, 0, 170, 255]
    ], dtype=np.uint8)
    assert np.array_equal(dec_v1, np.tile(exp_row_v1, (4, 1, 1)))

    v2_bytes = bytes([0xFF, 0x00, 0x88, 0xC6, 0xFA, 0x88, 0xC6, 0xFA])
    dec_v2 = decode_bc4(v2_bytes, 4, 4)
    exp_v2_1d = [255, 0, 219, 182, 146, 109, 73, 36] * 2
    assert np.array_equal(dec_v2[..., 0].flatten(), exp_v2_1d)
    assert np.all(dec_v2[..., 1:3] == 0) and np.all(dec_v2[..., 3] == 255)

    v3_bytes = bytes([0x00, 0xFF, 0x88, 0xC6, 0xFA, 0x88, 0xC6, 0xFA])
    dec_v3 = decode_bc4(v3_bytes, 4, 4)
    exp_v3_1d = [0, 255, 51, 102, 153, 204, 0, 255] * 2
    assert np.array_equal(dec_v3[..., 0].flatten(), exp_v3_1d)

    v4_bytes = v2_bytes + v3_bytes
    dec_v4 = decode_bc5(v4_bytes, 4, 4)
    assert np.array_equal(dec_v4[..., 0].flatten(), exp_v2_1d)
    assert np.array_equal(dec_v4[..., 1].flatten(), exp_v3_1d)
    assert np.all(dec_v4[..., 2] == 0) and np.all(dec_v4[..., 3] == 255)

    v_bc3 = v2_bytes + v1_bytes
    dec_bc3 = decode_bc3(v_bc3, 4, 4)
    assert np.array_equal(dec_bc3[..., :3], dec_v1[..., :3])
    assert np.array_equal(dec_bc3[..., 3].flatten(), exp_v2_1d)

    v1_swapped = bytes([0x1F, 0x00, 0x00, 0xF8, 0xE4, 0xE4, 0xE4, 0xE4])
    v_bc3_swap = v2_bytes + v1_swapped
    dec_bc3_swap = decode_bc3(v_bc3_swap, 4, 4)
    pix3 = dec_bc3_swap[0, 3]
    assert not (pix3[0] == 0 and pix3[1] == 0 and pix3[2] == 0 and pix3[3] == 0)
    assert pix3[3] == exp_v2_1d[3]
    return True


def test_group_a_3():
    """Test A.3: BC1 codec quality and constraints."""
    gx, gy = grid(256, 4, 4)
    c0 = pfbm(gx, gy, 4, 4, 10, 4)
    c1 = pfbm(gx, gy, 4, 4, 20, 4)
    c2 = pfbm(gx, gy, 4, 4, 30, 4)
    img = np.rint(np.stack([c0, c1, c2], axis=-1) * 255.0).astype(np.uint8)

    enc = encode_bc1(img)
    assert len(enc) == (256 // 4) * (256 // 4) * 8
    dec = decode_bc1(enc, 256, 256)
    val_psnr = psnr(img, dec[..., :3])
    assert val_psnr >= 28.0

    solid = np.full((16, 16, 3), [200, 100, 50], dtype=np.uint8)
    dec_solid = decode_bc1(encode_bc1(solid), 16, 16)
    assert np.all(np.abs(dec_solid[..., :3].astype(int) - [200, 100, 50]) <= 4)

    zero_img = np.zeros((16, 16, 3), dtype=np.uint8)
    dec_zero = decode_bc1(encode_bc1(zero_img), 16, 16)
    assert np.all(dec_zero[..., :3] == 0)

    non_mult = np.full((5, 6, 3), [100, 150, 200], dtype=np.uint8)
    dec_nm = decode_bc1(encode_bc1(non_mult), 6, 5)
    assert dec_nm.shape == (5, 6, 4)
    return True


def test_group_a_4():
    """Test A.4: BC4, BC5, and BC3 codecs quality and constraints."""
    gx, gy = grid(256, 4, 4)
    ch = np.rint(pfbm(gx, gy, 4, 4, 100, 4) * 255.0).astype(np.uint8)

    enc_bc4 = encode_bc4(ch)
    assert len(enc_bc4) == (256 // 4) * (256 // 4) * 8
    dec_bc4 = decode_bc4(enc_bc4, 256, 256)
    p_bc4 = psnr(ch, dec_bc4[..., 0])
    assert p_bc4 >= 38.0

    solid = np.full((16, 16), 137, dtype=np.uint8)
    assert np.all(decode_bc4(encode_bc4(solid), 16, 16)[..., 0] == 137)

    checker = ((np.arange(16)[:, None] + np.arange(16)[None, :]) % 2 * 255).astype(np.uint8)
    assert np.all(decode_bc4(encode_bc4(checker), 16, 16)[..., 0] == checker)

    ch2 = np.rint(pfbm(gx, gy, 4, 4, 200, 4) * 255.0).astype(np.uint8)
    rg = np.stack([ch, ch2], axis=-1)
    enc_bc5 = encode_bc5(rg)
    assert len(enc_bc5) == (256 // 4) * (256 // 4) * 16
    dec_bc5 = decode_bc5(enc_bc5, 256, 256)
    p_bc5 = psnr(rg, dec_bc5[..., :2])
    assert p_bc5 >= 38.0

    ch3 = np.rint(pfbm(gx, gy, 4, 4, 300, 4) * 255.0).astype(np.uint8)
    alpha = ch
    col = (np.stack([ch2, ch3, ch], axis=-1).astype(float) * (alpha[..., None].astype(float) / 255.0)).astype(np.uint8)
    rgba = np.concatenate([col, alpha[..., None]], axis=-1)

    enc_bc3 = encode_bc3(rgba)
    assert len(enc_bc3) == (256 // 4) * (256 // 4) * 16
    dec_bc3 = decode_bc3(enc_bc3, 256, 256)
    p_bc3_a = psnr(alpha, dec_bc3[..., 3])
    p_bc3_c = psnr(col, dec_bc3[..., :3])
    assert p_bc3_a >= 38.0
    assert p_bc3_c >= 26.0
    return True


def test_group_a_5():
    """Test A.5: Mipmap generation chains."""
    img2048 = np.zeros((2048, 2048, 3), dtype=np.uint8)
    c2048 = mip_chain(img2048, "linear")
    assert len(c2048) == 12
    assert c2048[-1].shape[:2] == (1, 1)

    img1024x512 = np.zeros((512, 1024, 4), dtype=np.uint8)
    c1024 = mip_chain(img1024x512, "linear")
    assert len(c1024) == 11
    assert c1024[-1].shape[:2] == (1, 1)

    const_img = np.full((64, 64, 3), [120, 150, 200], dtype=np.uint8)
    for mode in ["linear", "srgb"]:
        chain = mip_chain(const_img, mode)
        for lvl in chain:
            assert np.all(np.abs(lvl.astype(int) - [120, 150, 200]) <= 1)

    checker = np.zeros((64, 64, 3), dtype=np.uint8)
    checker[0::2, 0::2] = 255
    checker[1::2, 1::2] = 255
    c_srgb = mip_chain(checker, "srgb")
    val1 = c_srgb[1][0, 0]
    assert np.all(np.abs(val1.astype(int) - 188) <= 1)

    rng = Rng(12345)
    norms = np.zeros((64, 64, 2), dtype=np.uint8)
    for j in range(64):
        for i in range(64):
            nx = rng.uniform() * 0.8 - 0.4
            ny = rng.uniform() * 0.8 - 0.4
            norms[j, i, 0] = int(np.clip((nx * 0.5 + 0.5) * 255.0, 0, 255))
            norms[j, i, 1] = int(np.clip((ny * 0.5 + 0.5) * 255.0, 0, 255))

    c_norm = mip_chain(norms, "normal2")
    for l in range(1, 4):
        lvl = c_norm[l]
        x = lvl[..., 0].astype(float) / 255.0 * 2.0 - 1.0
        y = lvl[..., 1].astype(float) / 255.0 * 2.0 - 1.0
        z = np.sqrt(np.maximum(0.0, 1.0 - x * x - y * y))
        lens = np.sqrt(x * x + y * y + z * z)
        err = np.max(np.abs(lens - 1.0))
        assert err <= 0.02

    premul = np.zeros((64, 64, 4), dtype=np.uint8)
    for j in range(64):
        for i in range(64):
            a = rng.uniform()
            r = rng.uniform() * a
            g = rng.uniform() * a
            b = rng.uniform() * a
            premul[j, i] = [int(r * 255), int(g * 255), int(b * 255), int(a * 255)]

    c_premul = mip_chain(premul, "premul")
    for lvl in c_premul:
        assert np.all(lvl[..., 0] <= lvl[..., 3] + 1)
        assert np.all(lvl[..., 1] <= lvl[..., 3] + 1)
        assert np.all(lvl[..., 2] <= lvl[..., 3] + 1)
    return True


def test_group_a_6():
    """Test A.6: .tex26 container serialization and validation."""
    with tempfile.TemporaryDirectory() as td:
        tpath = os.path.join(td, "test.tex26")

        data_bc1 = []
        for l in range(3):
            lw, lh = max(1, 64 >> l), max(1, 64 >> l)
            raw = np.full((lh, lw, 3), 100 + l * 20, dtype=np.uint8)
            data_bc1.append([encode_bc1(raw)])
        spec1 = TexSpec("tex_bc1", 0, 64, 64, 1, 3, 1, data_bc1)

        data_bc5 = []
        for l in range(6):
            lw, lh = max(1, 32 >> l), max(1, 32 >> l)
            lay_list = []
            for lay in range(2):
                raw = np.full((lh, lw, 2), 50 + lay * 30 + l * 10, dtype=np.uint8)
                lay_list.append(encode_bc5(raw))
            data_bc5.append(lay_list)
        spec2 = TexSpec("tex_bc5", 3, 32, 32, 2, 6, 0, data_bc5)

        raw_rgba = np.full((8, 16, 4), [10, 20, 30, 40], dtype=np.uint8)
        spec3 = TexSpec("tex_rgba", 4, 16, 8, 1, 1, 0, [[raw_rgba.tobytes()]])

        specs = [spec1, spec2, spec3]
        dummy_glyphs = np.zeros(95 * 16, dtype=np.uint8).tobytes()

        write_tex26(tpath, specs, dummy_glyphs)
        read_back = read_tex26(tpath)

        assert read_back["glyph_count"] == 95
        assert len(read_back["textures"]) == 3

        for s_idx, spec in enumerate(specs):
            t_rec = read_back["textures"][s_idx]
            assert t_rec["data_offset"] % 64 == 0
            raw_data = bytes(t_rec["data"])

            pa = decode_texel(spec.fmt, spec.data[0][0], spec.width, spec.height, 37, 91)
            assert t_rec["probe_a"] == pa

            last_l = spec.mips - 1
            pb = decode_texel(
                spec.fmt,
                spec.data[last_l][0],
                max(1, spec.width >> last_l),
                max(1, spec.height >> last_l),
                0,
                0,
            )
            assert t_rec["probe_b"] == pb

            for l in range(spec.mips):
                lw = max(1, spec.width >> l)
                lh = max(1, spec.height >> l)
                for lay in range(spec.layers):
                    off = level_layer_offset(spec.fmt, spec.width, spec.height, spec.layers, spec.mips, l, lay)
                    sz = level_size(spec.fmt, lw, lh)
                    expected_chunk = spec.data[l][lay]
                    actual_chunk = raw_data[off : off + sz]
                    assert actual_chunk == expected_chunk

        # Corrupt one payload byte -> ValueError
        with open(tpath, "r+b") as f:
            f.seek(64)
            b = f.read(1)
            f.seek(64)
            f.write(bytes([b[0] ^ 0xFF]))
        try:
            read_tex26(tpath)
            assert False, "Should have raised ValueError"
        except ValueError:
            pass
    return True


def run_group_a():
    print("Running GROUP A tests...")
    tests = [
        ("Test A.1: Noise periodic wrap", test_group_a_1),
        ("Test A.2: Exact BC decode vectors", test_group_a_2),
        ("Test A.3: BC1 codec quality", test_group_a_3),
        ("Test A.4: BC4/BC5/BC3 codecs", test_group_a_4),
        ("Test A.5: Mipmap chains", test_group_a_5),
        ("Test A.6: .tex26 container", test_group_a_6),
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


def test_group_b_1():
    """Test B.1: Trim table validation."""
    trims_json_path = os.path.join(
        os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__)))),
        "assets", "source", "trims.json"
    )
    with open(trims_json_path, "r", encoding="utf-8") as f:
        trims_data = json.load(f)

    assert len(TRIM_SPECS) == 32
    assert len(trims_data) == 32

    for i in range(32):
        spec = TRIM_SPECS[i]
        assert spec[0] == i
        assert spec[1] == trims_data[i]["name"]

        pat_name = spec[2]
        assert hasattr(patterns, f"pat_{pat_name}"), f"Missing pattern function pat_{pat_name}"

        # Check colours in 0..255
        c1, c2 = spec[3], spec[4]
        if isinstance(c1[0], (int, float)):
            for val in c1:
                assert 0 <= val <= 255
        else:
            for rgb in c1:
                for val in rgb:
                    assert 0 <= val <= 255
        if isinstance(c2[0], (int, float)):
            for val in c2:
                assert 0 <= val <= 255
        else:
            for rgb in c2:
                for val in rgb:
                    assert 0 <= val <= 255
    return True


def test_group_b_2():
    """Test B.2: Paint trims shape, range, determinism, diversity."""
    albedos = []
    for spec in TRIM_SPECS:
        tm1 = paint_trim(spec)
        tm2 = paint_trim(spec)

        for arr, name in [
            (tm1.albedo, "albedo"), (tm1.height, "height"),
            (tm1.rough, "rough"), (tm1.metal, "metal"),
            (tm1.ao, "ao"), (tm1.emis, "emis"), (tm1.normal, "normal")
        ]:
            assert np.all(np.isfinite(arr)), f"Non-finite in {spec[1]} {name}"

        assert tm1.albedo.shape == (256, 256, 3)
        assert tm1.height.shape == (256, 256)
        assert tm1.rough.shape == (256, 256)
        assert tm1.metal.shape == (256, 256)
        assert tm1.ao.shape == (256, 256)
        assert tm1.emis.shape == (256, 256)
        assert tm1.normal.shape == (256, 256, 3)

        assert np.all(tm1.albedo >= 0.0) and np.all(tm1.albedo <= 1.0)
        assert np.all(tm1.height >= 0.0) and np.all(tm1.height <= 1.0)
        assert np.all(tm1.rough >= 0.0) and np.all(tm1.rough <= 1.0)
        assert np.all(tm1.metal >= 0.0) and np.all(tm1.metal <= 1.0)
        assert np.all(tm1.ao >= 0.0) and np.all(tm1.ao <= 1.0)
        assert np.all(tm1.emis >= 0.0) and np.all(tm1.emis <= 1.0)

        # Determinism
        assert np.array_equal(tm1.albedo, tm2.albedo)
        assert np.array_equal(tm1.height, tm2.height)
        assert np.array_equal(tm1.normal, tm2.normal)

        albedos.append(tm1.albedo)

    # Diversity: no two trims have identical albedo (mean abs difference > 0.01)
    for i in range(32):
        for j in range(i + 1, 32):
            diff = np.mean(np.abs(albedos[i] - albedos[j]))
            assert diff > 0.01, f"Trims {TRIM_SPECS[i][1]} and {TRIM_SPECS[j][1]} too similar (diff {diff})"
    return True


def test_group_b_3():
    """Test B.3: Normal map computation & properties."""
    # Synthetic ramp h = x / 255
    x = np.linspace(0, 1, 256, endpoint=False, dtype=np.float32)
    h_x = np.tile(x[None, :], (256, 1))
    norm_x = height_to_normal(h_x, 1.0)
    assert np.all(norm_x[:, 1:255, 0] < -0.05), "Interior n.x not < -0.05"
    assert np.all(np.abs(norm_x[:, 1:255, 1]) < 1e-6), "Interior n.y not < 1e-6"

    # Synthetic ramp h = y / 255 (downward)
    y = np.linspace(0, 1, 256, endpoint=False, dtype=np.float32)
    h_y = np.tile(y[:, None], (1, 256))
    norm_y = height_to_normal(h_y, 1.0)
    assert np.all(norm_y[1:255, :, 1] < -0.05), "Interior n.y not < -0.05"

    # Flat
    h_flat = np.full((256, 256), 0.5, dtype=np.float32)
    norm_flat = height_to_normal(h_flat, 1.0)
    assert np.all(norm_flat[..., 0] == 0.0)
    assert np.all(norm_flat[..., 1] == 0.0)
    assert np.all(norm_flat[..., 2] == 1.0)

    # All 32 painted normals have z >= 0.6 - 1e-6 and unit length within 1e-5
    for spec in TRIM_SPECS:
        tm = paint_trim(spec)
        assert np.all(tm.normal[..., 2] >= 0.6 - 1e-6), f"{spec[1]} normal z < 0.6"
        lens = np.sqrt(np.sum(tm.normal * tm.normal, axis=-1))
        assert np.all(np.abs(lens - 1.0) < 1e-5), f"{spec[1]} normal not unit length"
    return True


def test_group_b_4():
    """Test B.4: Material properties across trims."""
    metals = {"steel_plain", "steel_worn", "roof_metal", "gold"}
    for spec in TRIM_SPECS:
        tm = paint_trim(spec)
        mean_metal = float(np.mean(tm.metal))
        if spec[1] in metals:
            assert mean_metal > 0.7, f"{spec[1]} metal {mean_metal} not > 0.7"
        else:
            assert mean_metal < 0.15, f"{spec[1]} metal {mean_metal} not < 0.15"

        if spec[1] == "glass_window":
            mean_rough = float(np.mean(tm.rough))
            assert mean_rough < 0.2, f"glass_window rough {mean_rough} not < 0.2"
            mean_alb = np.mean(tm.albedo, axis=(0, 1))
            assert mean_alb[2] > mean_alb[0], "glass_window B not > R"

        if spec[1] == "glow_emissive":
            assert np.all(tm.emis == 1.0), "glow_emissive emis not 1.0 everywhere"
        elif spec[1] == "ice_crystal":
            mean_emis = float(np.mean(tm.emis))
            max_emis = float(np.max(tm.emis))
            assert mean_emis <= 0.15, f"ice_crystal mean emis {mean_emis} > 0.15"
            assert max_emis <= 0.25, f"ice_crystal max emis {max_emis} > 0.25"
        else:
            assert np.all(tm.emis == 0.0), f"{spec[1]} has non-zero emis"

        if spec[1] == "gold":
            mean_alb = np.mean(tm.albedo, axis=(0, 1))
            assert mean_alb[0] > mean_alb[2] + 0.25, "gold R not > B + 0.25"

        if spec[1] == "skin":
            mean_alb = np.mean(tm.albedo, axis=(0, 1))
            assert mean_alb[0] > mean_alb[2], "skin R not > B"
    return True


def test_group_b_5():
    """Test B.5: Structural features in specific tiles."""
    # hazard_stripe tile
    hazard_spec = [s for s in TRIM_SPECS if s[1] == "hazard_stripe"][0]
    tm_hazard = paint_trim(hazard_spec)
    # Check pixels (10, 10) and (50, 10)
    lum_fn = lambda c: 0.299 * c[..., 0] + 0.587 * c[..., 1] + 0.114 * c[..., 2]
    lum = lum_fn(tm_hazard.albedo)
    lum1 = lum[10, 10]
    lum2 = lum[10, 50]
    assert abs(lum1 - lum2) > 0.3, f"hazard stripe luminance diff {abs(lum1 - lum2)} not > 0.3"
    # Check parity
    par1 = ((10 + 10) // 32) % 2
    par2 = ((50 + 10) // 32) % 2
    assert par1 != par2

    # brick tile
    brick_spec = [s for s in TRIM_SPECS if s[1] == "brick"][0]
    tm_brick = paint_trim(brick_spec)
    row_indices = np.arange(256)
    mortar_rows = (row_indices % 16 == 0) | (row_indices % 16 == 1)
    body_rows = (row_indices % 16 >= 4) & (row_indices % 16 <= 13)
    mean_mortar = float(np.mean(tm_brick.height[mortar_rows, :]))
    mean_body = float(np.mean(tm_brick.height[body_rows, :]))
    assert mean_mortar < 0.3, f"brick mortar height {mean_mortar} not < 0.3"
    assert mean_body > 0.4, f"brick body height {mean_body} not > 0.4"

    # stone_block grooves
    stone_spec = [s for s in TRIM_SPECS if s[1] == "stone_block"][0]
    tm_stone = paint_trim(stone_spec)
    groove_rows = (row_indices % 64 >= 0) & (row_indices % 64 <= 2)
    other_rows = ~groove_rows
    mean_groove = float(np.mean(tm_stone.height[groove_rows, :]))
    mean_other = float(np.mean(tm_stone.height[other_rows, :]))
    assert mean_groove < mean_other - 0.2, f"stone grooves {mean_groove} not < other ({mean_other}) - 0.2"
    return True


def test_group_b_6():
    """Test B.6: Sheet assembly and layout contracts."""
    sheet = assemble_sheet()
    albedo = sheet["albedo"]
    normal_xy = sheet["normal_xy"]
    orm = sheet["orm"]
    emissive = sheet["emissive"]

    assert albedo.shape == (2048, 2048, 3) and albedo.dtype == np.uint8
    assert normal_xy.shape == (2048, 2048, 2) and normal_xy.dtype == np.uint8
    assert orm.shape == (2048, 2048, 3) and orm.dtype == np.uint8
    assert emissive.shape == (2048, 2048) and emissive.dtype == np.uint8

    # Byte at centre of every tile equals byte computed from TileMaps centre pixel
    for spec in TRIM_SPECS:
        trim_id = spec[0]
        col = trim_id % 8
        row = trim_id // 8
        x0 = col * 256
        y0 = row * 256

        tm = paint_trim(spec)
        exp_alb = np.rint(tm.albedo[128, 128] * 255.0).astype(np.uint8)
        exp_nxy = np.rint(np.clip(tm.normal[128, 128, :2] * 0.5 + 0.5, 0.0, 1.0) * 255.0).astype(np.uint8)
        exp_orm = np.array([
            np.rint(tm.ao[128, 128] * 255.0),
            np.rint(tm.rough[128, 128] * 255.0),
            np.rint(tm.metal[128, 128] * 255.0)
        ], dtype=np.uint8)
        exp_emis = np.rint(tm.emis[128, 128] * 255.0).astype(np.uint8)

        act_alb = albedo[y0 + 128, x0 + 128]
        act_nxy = normal_xy[y0 + 128, x0 + 128]
        act_orm = orm[y0 + 128, x0 + 128]
        act_emis = emissive[y0 + 128, x0 + 128]

        assert np.array_equal(act_alb, exp_alb)
        assert np.array_equal(act_nxy, exp_nxy)
        assert np.array_equal(act_orm, exp_orm)
        assert act_emis == exp_emis

    # Tiles 32..63 hold neutral values of 1.5
    for t_id in range(32, 64):
        col = t_id % 8
        row = t_id // 8
        x0 = col * 256
        y0 = row * 256
        tile_alb = albedo[y0 : y0 + 256, x0 : x0 + 256]
        tile_nxy = normal_xy[y0 : y0 + 256, x0 : x0 + 256]
        tile_orm = orm[y0 : y0 + 256, x0 : x0 + 256]
        tile_emis = emissive[y0 : y0 + 256, x0 : x0 + 256]

        assert np.all(tile_alb == 128)
        assert np.all(tile_nxy == 128)
        assert np.all(tile_orm[..., 0] == 255)
        assert np.all(tile_orm[..., 1] == 128)
        assert np.all(tile_orm[..., 2] == 0)
        assert np.all(tile_emis == 0)

    # trims.json px rect of every trim lies inside its tile
    trims_json_path = os.path.join(
        os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__)))),
        "assets", "source", "trims.json"
    )
    with open(trims_json_path, "r", encoding="utf-8") as f:
        trims_data = json.load(f)

    for item in trims_data:
        col, row = item["col"], item["row"]
        x0, y0, x1, y1 = item["px"]
        assert x0 >= col * 256
        assert y0 >= row * 256
        assert x1 <= (col + 1) * 256
        assert y1 <= (row + 1) * 256
    return True


def run_group_b():
    print("Running GROUP B tests...")
    tests = [
        ("Test B.1: Trim table validation", test_group_b_1),
        ("Test B.2: Paint trims shapes & range", test_group_b_2),
        ("Test B.3: Normal map properties", test_group_b_3),
        ("Test B.4: Material properties", test_group_b_4),
        ("Test B.5: Structural tile features", test_group_b_5),
        ("Test B.6: Sheet assembly & contracts", test_group_b_6),
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


def test_group_c_1():
    """Test C.1: Terrain layers names, shapes, dtypes, determinism."""
    expected_names = ["seabed", "beach", "grass", "forest_floor", "arid", "tundra", "rock", "cliff"]
    assert len(TERRAIN_LAYERS) == 8
    for i in range(8):
        assert TERRAIN_LAYERS[i][1] == expected_names[i]

    for i in range(8):
        alb1, nxy1, rh1 = paint_terrain_layer(i)
        alb2, nxy2, rh2 = paint_terrain_layer(i)

        assert alb1.shape == (512, 512, 3) and alb1.dtype == np.uint8
        assert nxy1.shape == (512, 512, 2) and nxy1.dtype == np.uint8
        assert rh1.shape == (512, 512, 3) and rh1.dtype == np.uint8

        assert np.array_equal(alb1, alb2)
        assert np.array_equal(nxy1, nxy2)
        assert np.array_equal(rh1, rh2)
    return True


def test_group_c_2():
    """Test C.2: Terrain layers seamless tiling."""
    for i in range(8):
        alb, nxy, rh = paint_terrain_layer(i)
        layer_name = TERRAIN_LAYERS[i][1]

        for arr, name in [(alb, "albedo"), (rh[..., 0], "height"), (nxy[..., 0], "normal_x")]:
            diff_wrap_col = np.mean(np.abs(arr[:, 511].astype(float) - arr[:, 0].astype(float)))
            diff_mid_col = np.mean(np.abs(arr[:, 255].astype(float) - arr[:, 256].astype(float)))
            assert diff_wrap_col <= 2.0 * diff_mid_col + 1e-2, (
                f"Layer {layer_name} {name} col wrap {diff_wrap_col} > 2.0 * mid ({diff_mid_col})"
            )

            diff_wrap_row = np.mean(np.abs(arr[511, :].astype(float) - arr[0, :].astype(float)))
            diff_mid_row = np.mean(np.abs(arr[255, :].astype(float) - arr[256, :].astype(float)))
            assert diff_wrap_row <= 2.0 * diff_mid_row + 1e-2, (
                f"Layer {layer_name} {name} row wrap {diff_wrap_row} > 2.0 * mid ({diff_mid_row})"
            )
    return True


def test_group_c_3():
    """Test C.3: Terrain layer colours and luminances."""
    lums = {}
    means = {}
    for i in range(8):
        alb, _, _ = paint_terrain_layer(i)
        name = TERRAIN_LAYERS[i][1]
        m = np.mean(alb, axis=(0, 1)) / 255.0
        r, g, b = float(m[0]), float(m[1]), float(m[2])
        lum = 0.299 * r + 0.587 * g + 0.114 * b
        lums[name] = lum
        means[name] = (r, g, b)

    # grass G > R and G > B
    gr_r, gr_g, gr_b = means["grass"]
    assert gr_g > gr_r and gr_g > gr_b, f"grass G not > R,B: {gr_r}, {gr_g}, {gr_b}"

    # arid R > G > B
    ar_r, ar_g, ar_b = means["arid"]
    assert ar_r > ar_g and ar_g > ar_b, f"arid R>G>B violated: {ar_r}, {ar_g}, {ar_b}"

    # tundra luminance > 0.55
    assert lums["tundra"] > 0.55, f"tundra lum {lums['tundra']} not > 0.55"

    # rock |R - B| < 0.10
    rk_r, rk_g, rk_b = means["rock"]
    assert abs(rk_r - rk_b) < 0.10, f"rock |R-B| {abs(rk_r - rk_b)} not < 0.10"

    # cliff luminance < rock luminance
    assert lums["cliff"] < lums["rock"], f"cliff lum {lums['cliff']} not < rock lum {lums['rock']}"

    # beach luminance > 0.6
    assert lums["beach"] > 0.60, f"beach lum {lums['beach']} not > 0.60"

    # seabed luminance < 0.55
    assert lums["seabed"] < 0.55, f"seabed lum {lums['seabed']} not < 0.55"

    return True


def test_group_c_4():
    """Test C.4: Height percentile span, normal validity, roughness accuracy."""
    for i in range(8):
        name = TERRAIN_LAYERS[i][1]
        alb, nxy, rh = paint_terrain_layer(i)

        h = rh[..., 0].astype(np.float64) / 255.0
        q01, q99 = np.quantile(h, [0.01, 0.99])
        span = q99 - q01
        assert span >= 0.5, f"Layer {name} height span {span} not >= 0.5"

        nx = nxy[..., 0].astype(float) / 255.0 * 2.0 - 1.0
        ny = nxy[..., 1].astype(float) / 255.0 * 2.0 - 1.0
        nz = np.sqrt(np.maximum(0.0, 1.0 - nx * nx - ny * ny))
        assert np.all(nz >= 0.6 - 1e-6), f"Layer {name} normal z < 0.6"

        lens = np.sqrt(nx * nx + ny * ny + nz * nz)
        assert np.all(np.abs(lens - 1.0) < 1e-4), f"Layer {name} normal not unit length"

        mean_rough = float(np.mean(rh[..., 1])) / 255.0
        table_rough = TERRAIN_LAYERS[i][4]
        assert abs(mean_rough - table_rough) <= 0.02, (
            f"Layer {name} mean rough {mean_rough} diff from table {table_rough} > 0.02"
        )
    return True


def test_group_c_5():
    """Test C.5: build_terrain output shapes, layer order, determinism."""
    albedos1, normals1, rhs1 = build_terrain()
    albedos2, normals2, rhs2 = build_terrain()

    assert albedos1.shape == (8, 512, 512, 3) and albedos1.dtype == np.uint8
    assert normals1.shape == (8, 512, 512, 2) and normals1.dtype == np.uint8
    assert rhs1.shape == (8, 512, 512, 3) and rhs1.dtype == np.uint8

    assert np.array_equal(albedos1, albedos2)
    assert np.array_equal(normals1, normals2)
    assert np.array_equal(rhs1, rhs2)

    for i in range(8):
        single_alb, single_nxy, single_rh = paint_terrain_layer(i)
        assert np.array_equal(albedos1[i], single_alb)
        assert np.array_equal(normals1[i], single_nxy)
        assert np.array_equal(rhs1[i], single_rh)
    return True


def run_group_c():
    print("Running GROUP C tests...")
    tests = [
        ("Test C.1: Terrain layers names & shapes", test_group_c_1),
        ("Test C.2: Terrain layers seamless tiling", test_group_c_2),
        ("Test C.3: Terrain layer colours & luminances", test_group_c_3),
        ("Test C.4: Height span & normal validity", test_group_c_4),
        ("Test C.5: build_terrain shapes & order", test_group_c_5),
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


def test_group_d_1():
    """Test D.1: VFX registry, signatures, and determinism."""
    exp_effects = [
        (0, "fire_explosion", "once"),
        (1, "smoke_puff", "once"),
        (2, "muzzle_flash", "once"),
        (3, "dust_cloud", "once"),
        (4, "spark_burst", "once"),
        (5, "shockwave_ring", "once"),
        (6, "fire_loop", "loop"),
        (7, "smoke_loop", "loop"),
        (8, "laser_impact", "once"),
        (9, "emp_pulse", "once"),
        (10, "debris_chunks", "once"),
        (11, "nuke_mushroom", "once"),
    ]
    assert len(EFFECTS) == 12
    for i in range(12):
        assert EFFECTS[i][0] == exp_effects[i][0]
        assert EFFECTS[i][1] == exp_effects[i][1]
        assert EFFECTS[i][2] == exp_effects[i][2]

    for e_id in range(12):
        f1 = render_frame(e_id, 0.3)
        f2 = render_frame(e_id, 0.3)
        assert f1.shape == (128, 128, 4) and f1.dtype == np.float32
        assert np.array_equal(f1, f2)
    return True


def test_group_d_2():
    """Test D.2: Frame ranges, premultiplication, and border zeroing."""
    for e_id in range(12):
        _, name, kind = EFFECTS[e_id]
        for f in range(16):
            t = f / 16.0 if kind == "loop" else f / 15.0
            frame = render_frame(e_id, t)
            assert np.all(np.isfinite(frame)), f"{name} f={f} non-finite"
            assert np.all(frame >= 0.0) and np.all(frame <= 1.0), f"{name} f={f} outside [0, 1]"

            a = frame[..., 3]
            for c in range(3):
                assert np.all(frame[..., c] <= a + 1e-4), f"{name} f={f} channel {c} > alpha"

            # Outermost 2 px rows/cols exactly zero
            assert np.all(frame[:2, :] == 0.0), f"{name} f={f} top 2 rows not 0"
            assert np.all(frame[-2:, :] == 0.0), f"{name} f={f} bottom 2 rows not 0"
            assert np.all(frame[:, :2] == 0.0), f"{name} f={f} left 2 cols not 0"
            assert np.all(frame[:, -2:] == 0.0), f"{name} f={f} right 2 cols not 0"
    return True


def test_group_d_3():
    """Test D.3: Alpha visibility, peak alpha sum, and coverage limits."""
    for e_id in range(12):
        _, name, kind = EFFECTS[e_id]
        max_a = 0.0
        peak_sum = 0.0
        for f in range(16):
            t = f / 16.0 if kind == "loop" else f / 15.0
            frame = render_frame(e_id, t)
            a = frame[..., 3]
            max_a = max(max_a, float(np.max(a)))
            a_sum = float(np.sum(a))
            peak_sum = max(peak_sum, a_sum)
            # In no frame is more than 90% of the pixels with alpha > 0.05
            active_ratio = float(np.mean(a > 0.05))
            assert active_ratio <= 0.90, f"{name} f={f} active ratio {active_ratio} > 0.90"

        assert max_a >= 0.6, f"{name} max alpha {max_a} not >= 0.6"
        assert peak_sum >= 400.0, f"{name} peak alpha sum {peak_sum} not >= 400"
    return True


def test_group_d_4():
    """Test D.4: One-shot timing, end fade, and consecutive frame differences."""
    for e_id in range(12):
        _, name, kind = EFFECTS[e_id]
        if kind != "once":
            continue

        frames = [render_frame(e_id, f / 15.0) for f in range(16)]
        alpha_sums = [float(np.sum(fr[..., 3])) for fr in frames]
        peak_sum = max(alpha_sums)

        # Alpha sum of t=1 frame <= 2% of peak frame's sum
        assert alpha_sums[-1] <= 0.02 * peak_sum, f"{name} end sum {alpha_sums[-1]} > 2% of peak ({peak_sum})"

        # Consecutive frames differ
        for f in range(15):
            s1 = alpha_sums[f]
            s2 = alpha_sums[f + 1]
            if s1 > 0.01 * peak_sum or s2 > 0.01 * peak_sum:
                diff = float(np.mean(np.abs(frames[f] - frames[f + 1])))
                assert diff > 0.001, f"{name} frames {f} and {f+1} diff {diff} not > 0.001"
    return True


def test_group_d_5():
    """Test D.5: Loop effects continuity, variation, and energy stability."""
    for e_id in [6, 7]:  # fire_loop, smoke_loop
        _, name, _ = EFFECTS[e_id]
        f0 = render_frame(e_id, 0.0)
        f1 = render_frame(e_id, 1.0)
        assert np.max(np.abs(f0 - f1)) <= 1e-5, f"{name} loop mismatch at 0 and 1"

        f8 = render_frame(e_id, 8.0 / 16.0)
        diff_0_8 = float(np.mean(np.abs(f0 - f8)))
        assert diff_0_8 > 0.005, f"{name} f0 and f8 too similar ({diff_0_8})"

        frames = [render_frame(e_id, f / 16.0) for f in range(16)]
        alpha_sums = [float(np.sum(fr[..., 3])) for fr in frames]
        min_sum = min(alpha_sums)
        max_sum = max(alpha_sums)
        assert min_sum >= 0.50 * max_sum, f"{name} min loop sum {min_sum} < 50% max ({max_sum})"
    return True


def test_group_d_6():
    """Test D.6: VFX atlas layout, sampling, unused cell zeroes, determinism."""
    atlas1 = assemble_vfx_atlas()
    atlas2 = assemble_vfx_atlas()

    assert atlas1.shape == (2048, 2048, 4) and atlas1.dtype == np.uint8
    assert np.array_equal(atlas1, atlas2)

    # 6 sampled (e, f) pairs
    samples = [(0, 0), (2, 5), (6, 8), (7, 15), (10, 3), (11, 12)]
    for e_id, f_id in samples:
        kind = EFFECTS[e_id][2]
        t = f_id / 16.0 if kind == "loop" else f_id / 15.0
        frame_flt = render_frame(e_id, t)
        frame_exp = np.rint(np.clip(frame_flt, 0.0, 1.0) * 255.0).astype(np.uint8)

        ox, oy = frame_origin(e_id, f_id)
        frame_act = atlas1[oy : oy + 128, ox : ox + 128]
        assert np.array_equal(frame_act, frame_exp), f"Atlas mismatch at effect {e_id} frame {f_id}"

    # Cells 12..15 all zero
    # Cell row 3 (y in 1536..2048)
    assert np.all(atlas1[1536:, :] == 0), "Cells 12..15 not all zero"
    return True


def run_group_d():
    print("Running GROUP D tests...")
    tests = [
        ("Test D.1: VFX registry & signatures", test_group_d_1),
        ("Test D.2: Frame ranges & borders", test_group_d_2),
        ("Test D.3: Visibility & coverage", test_group_d_3),
        ("Test D.4: One-shot timing & differences", test_group_d_4),
        ("Test D.5: Loop effects continuity", test_group_d_5),
        ("Test D.6: Atlas layout & sampling", test_group_d_6),
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

    print(f"GROUP D {'PASS' if passed == len(tests) else 'FAIL'} ({passed}/{len(tests)})")
    return passed, len(tests)


def test_group_e_1():
    """Test E.1: Font glyph table, strokes, and coordinate ranges."""
    assert len(font_data.FONT_GLYPHS) == 95
    for code in range(32, 127):
        assert code in font_data.FONT_GLYPHS
        strokes = font_data.FONT_GLYPHS[code]
        ch = chr(code)
        if code == 32:
            assert len(strokes) == 0
        else:
            if ch.isalnum():
                assert len(strokes) >= 1, f"Alnum {ch} has no strokes"
            for st in strokes:
                for x, y in st:
                    assert 0.0 <= x <= 4.0, f"{ch} x {x} outside [0, 4]"
                    assert -1.0 <= y <= 6.8, f"{ch} y {y} outside [-1, 6.8]"
    return True


def test_group_e_2():
    """Test E.2: Glyph rendering, metrics, diversity, border clearance."""
    cells = {}
    records = {}
    for code in range(32, 127):
        img, rec = font_data.render_glyph(code)
        assert img.shape == (48, 48, 4) and img.dtype == np.uint8
        cells[code] = img
        records[code] = rec
        if code == 32:
            assert not np.any(img[..., 0] > 127)
        else:
            active = int(np.sum(img[..., 0] > 127))
            assert active >= 20, f"Glyph {chr(code)} active pixels {active} < 20"

        # Rows/cols 0 and 47 are all 0
        assert np.all(img[0, :] == 0) and np.all(img[47, :] == 0)
        assert np.all(img[:, 0] == 0) and np.all(img[:, 47] == 0)

    # Pairwise distinct A-Z and 0-9
    alnum = [chr(c) for c in range(ord('A'), ord('Z') + 1)] + [chr(c) for c in range(ord('0'), ord('9') + 1)]
    for i in range(len(alnum)):
        for j in range(i + 1, len(alnum)):
            m1 = cells[ord(alnum[i])][..., 0] > 127
            m2 = cells[ord(alnum[j])][..., 0] > 127
            xor_cnt = int(np.sum(m1 ^ m2))
            assert xor_cnt >= 10, f"{alnum[i]} and {alnum[j]} too similar (xor {xor_cnt})"

    # Metrics
    rec_I = records[ord('I')]
    rec_W = records[ord('W')]
    adv_I = int.from_bytes(rec_I[12:14], "little")
    adv_W = int.from_bytes(rec_W[12:14], "little")
    assert adv_I < adv_W, f"adv_I {adv_I} not < adv_W {adv_W}"

    for code, rec in records.items():
        off_y = int.from_bytes(rec[10:12], "little", signed=True)
        assert off_y == 2304, f"{chr(code)} offset_y_q {off_y} != 2304"
    return True


def test_group_e_3():
    """Test E.3: Glyph stroke start sanity and cell corner zeroing."""
    for code in range(33, 127):
        img, _ = font_data.render_glyph(code)
        strokes = font_data.FONT_GLYPHS[code]
        all_x = [pt[0] for st in strokes for pt in st]
        xmin = min(all_x)
        first_pt = strokes[0][0]
        fx = 8.0 + (first_pt[0] - xmin) * font_data.S_PX
        fy = 36.0 - first_pt[1] * font_data.S_PX
        c = int(np.clip(np.floor(fx), 0, 47))
        r = int(np.clip(np.floor(fy), 0, 47))
        val = img[r, c, 0]
        assert val >= 0.6 * 255.0 - 1e-4, f"Glyph {chr(code)} start pixel val {val} < 0.6*255"
        assert img[0, 0, 0] == 0
    return True


def test_group_e_4():
    """Test E.4: MSDF core math, pseudo-distance corner, fill agreement."""
    # (a) rect(20x20 at (32, 32))
    rect_contour = [(shapes.rect(32, 32, 20, 20), False)]
    test_pts = np.array([
        [32.0, 32.0],
        [52.0, 32.0],
        [40.0, 32.0],
        [20.5, 20.5],
    ])
    vals = msdf.msdf_eval(test_pts, rect_contour, use_pseudo=params.MSDF_USE_PSEUDO)
    meds = np.median(vals[:, :3], axis=1)

    assert abs(meds[0] - 10.0) < 0.01 and abs(vals[0, 3] - 10.0) < 0.01
    assert abs(meds[1] - (-10.0)) < 0.01
    assert abs(meds[2] - 2.0) < 0.01

    # (b) if MSDF_USE_PSEUDO:
    if params.MSDF_USE_PSEUDO:
        assert abs(meds[3] - (-1.5)) < 0.35, f"pseudo median {meds[3]} diff from -1.5 > 0.35"
        assert abs(vals[3, 3] - (-2.12)) < 0.05, f"true_sd {vals[3, 3]} diff from -2.12 > 0.05"

    # (c) FILL AGREEMENT across all 32 icons
    for i in range(32):
        cnts = shapes.get_icon_contours(i)
        img = msdf.render_icon(cnts)
        alpha = img[..., 3].astype(float)
        sd = (alpha / 255.0 - 0.5) * 8.0
        mask = np.abs(sd) > 1.5
        med = np.median(img[..., :3], axis=-1)
        agree = (med > 127) == (alpha > 127)
        ratio = float(np.mean(agree[mask]))
        assert ratio >= 0.99, f"Icon {i} agreement {ratio*100:.2f}% < 99%"
    return True


def test_group_e_5():
    """Test E.5: Edge coloring rules, sharp corners, and ring orientations."""
    # Hexagon
    hex_pts = shapes.regular_polygon(0, 0, 20, 6)
    hex_masks = msdf.color_edges(hex_pts)
    assert len(hex_masks) == 6
    for i in range(6):
        assert hex_masks[i] != hex_masks[(i + 1) % 6]
    assert (np.bitwise_or.reduce(hex_masks) & 0b111) == 0b111

    # Square
    sq_pts = shapes.rect(0, 0, 20, 20)
    sq_masks = msdf.color_edges(sq_pts)
    COLOR_MASKS = [0b011, 0b110, 0b101]
    assert list(sq_masks) == [COLOR_MASKS[0], COLOR_MASKS[1], COLOR_MASKS[2], COLOR_MASKS[1]]

    # Triangle
    tri_pts = shapes.regular_polygon(0, 0, 20, 3)
    tri_masks = msdf.color_edges(tri_pts)
    assert list(tri_masks) == [COLOR_MASKS[0], COLOR_MASKS[1], COLOR_MASKS[2]]

    # Circle (48 vertices) -> all WHITE
    circ_pts = shapes.circle(0, 0, 20, 48)
    circ_masks = msdf.color_edges(circ_pts)
    assert np.all(circ_masks == 0b111)

    # Lens polygon with 2 sharp corners -> 2 runs, different masks
    arc1 = [(15.0 * np.cos(a), 8.0 * np.sin(a)) for a in np.linspace(0, np.pi, 16)]
    arc2 = [(15.0 * np.cos(a), -8.0 * np.sin(a)) for a in np.linspace(np.pi, 2 * np.pi, 16)]
    lens = np.array(arc1[:-1] + arc2[:-1])
    lens_masks = msdf.color_edges(lens)
    assert len(np.unique(lens_masks)) >= 2

    # Ring icons: hole contour CW (signed area < 0) and outer CCW (> 0)
    ring_icons = [5, 16, 18, 20, 28, 29, 30]
    for idx in ring_icons:
        cnts = shapes.get_icon_contours(idx)
        normed = msdf.make_contours(cnts)
        for pts, is_hole in normed:
            x, y = pts[:, 0], pts[:, 1]
            area = 0.5 * np.sum(x * np.roll(y, -1) - np.roll(x, -1) * y)
            if is_hole:
                assert area < 0, f"Icon {idx} hole area {area} not < 0"
            else:
                assert area > 0, f"Icon {idx} outer area {area} not > 0"
    return True


def test_group_e_6():
    """Test E.6: Atlas assembly, non-overlapping cells, hole/solid test, byte determinism."""
    atlas1, glyph_bytes1 = msdf_atlas.build_msdf_atlas()
    atlas2, glyph_bytes2 = msdf_atlas.build_msdf_atlas()

    assert atlas1.shape == (512, 1024, 4) and atlas1.dtype == np.uint8
    assert np.array_equal(atlas1, atlas2)
    assert glyph_bytes1 == glyph_bytes2
    assert len(glyph_bytes1) == 95 * 16

    solid_icons = [0, 1, 2, 3, 4, 6, 25, 31]
    hole_icons = [5, 16, 18, 20, 28, 29, 30]

    for idx in solid_icons:
        col = idx % 16
        row = idx // 16
        ox = col * 64
        oy = 384 + row * 64
        med = float(np.median(atlas1[oy + 32, ox + 32, :3]))
        assert med > 127, f"Solid icon {idx} center med {med} not > 127"

    for idx in hole_icons:
        col = idx % 16
        row = idx // 16
        ox = col * 64
        oy = 384 + row * 64
        med = float(np.median(atlas1[oy + 32, ox + 32, :3]))
        assert med < 128, f"Hole icon {idx} center med {med} not < 128"

    # Glyph cells: x in 0..576, y in 0..384; Icon cells: x in 0..1024, y in 384..512
    # Check that region x in 576..1024 and y in 0..384 is all zero
    assert np.all(atlas1[:384, 576:] == 0), "Atlas unused top-right region not zero"
    return True


def run_group_e():
    print("Running GROUP E tests...")
    tests = [
        ("Test E.1: Font glyph table & strokes", test_group_e_1),
        ("Test E.2: Glyph rendering & metrics", test_group_e_2),
        ("Test E.3: Glyph stroke start sanity", test_group_e_3),
        ("Test E.4: MSDF core math & fill agreement", test_group_e_4),
        ("Test E.5: Edge coloring & ring orientation", test_group_e_5),
        ("Test E.6: Atlas assembly & icon holes", test_group_e_6),
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

    print(f"GROUP E {'PASS' if passed == len(tests) else 'FAIL'} ({passed}/{len(tests)})")
    return passed, len(tests)





import hashlib
import subprocess
from asset_common.hashing import fnv1a64
from bake_textures import bake_all

_GROUP_F_CACHE = {}

def _get_group_f_bake():
    if "sources" not in _GROUP_F_CACHE:
        tmp = tempfile.mkdtemp(prefix="tex_bake_")
        sources = bake_all(tmp, preview=True, keep_sources=True)
        tex26_path = os.path.join(tmp, "textures_2026.tex26")
        meta = read_tex26(tex26_path)
        _GROUP_F_CACHE["tmp"] = tmp
        _GROUP_F_CACHE["sources"] = sources
        _GROUP_F_CACHE["tex26_path"] = tex26_path
        _GROUP_F_CACHE["meta"] = meta
    return _GROUP_F_CACHE


def test_group_f_1():
    """Test F.1: Full bake and container inspection."""
    bake_info = _get_group_f_bake()
    meta = bake_info["meta"]
    textures = meta["textures"]
    assert len(textures) == 9

    expected_specs = [
        ("trim_albedo", 0, 2048, 2048, 1, 12, 1),
        ("trim_normal", 3, 2048, 2048, 1, 12, 0),
        ("trim_orm", 0, 2048, 2048, 1, 12, 0),
        ("trim_emissive", 2, 2048, 2048, 1, 12, 0),
        ("terrain_albedo", 0, 512, 512, 8, 10, 1),
        ("terrain_normal", 3, 512, 512, 8, 10, 0),
        ("terrain_rh", 0, 512, 512, 8, 10, 0),
        ("vfx_atlas", 1, 2048, 2048, 1, 4, 2),
        ("msdf_atlas", 4, 1024, 512, 1, 1, 0),
    ]

    for i, (name, fmt, w, h, layers, mips, flags) in enumerate(expected_specs):
        t = textures[i]
        assert t["name"] == name
        assert t["name_hash"] == fnv1a64(name)
        assert t["format"] == fmt
        assert t["width"] == w
        assert t["height"] == h
        assert t["layers"] == layers
        assert t["mips"] == mips
        assert t["flags"] == flags

    assert meta["glyph_count"] == 95
    assert len(meta["glyphs"]) == 95


def test_group_f_2():
    """Test F.2: CLI execution determinism (sha256 bit-identical)."""
    bake_info = _get_group_f_bake()
    path1 = bake_info["tex26_path"]
    with open(path1, "rb") as f:
        h1 = hashlib.sha256(f.read()).hexdigest()

    tmp2 = tempfile.mkdtemp(prefix="tex_cli_")
    cmd = [
        sys.executable,
        os.path.join(os.path.dirname(os.path.abspath(__file__)), "bake_textures.py"),
        "--out", tmp2,
        "--no-preview"
    ]
    res = subprocess.run(cmd, capture_output=True, text=True)
    assert res.returncode == 0, f"CLI bake failed: {res.stderr}"

    path2 = os.path.join(tmp2, "textures_2026.tex26")
    with open(path2, "rb") as f:
        h2 = hashlib.sha256(f.read()).hexdigest()

    assert h1 == h2, f"Bake determinism failure: {h1} != {h2}"


def test_group_f_3():
    """Test F.3: File size (15..40 MiB), alignment, and data_bytes sums."""
    bake_info = _get_group_f_bake()
    path = bake_info["tex26_path"]
    meta = bake_info["meta"]
    size = os.path.getsize(path)

    assert 15 * 1024 * 1024 <= size <= 40 * 1024 * 1024
    assert meta["total_bytes"] == size
    assert meta["total_bytes"] % 64 == 0
    assert meta["glyph_offset"] % 64 == 0

    for i, t in enumerate(meta["textures"]):
        fmt = t["format"]
        w, h = t["width"], t["height"]
        layers, mips = t["layers"], t["mips"]
        assert t["data_offset"] % 64 == 0

        expected_bytes = 0
        for l in range(mips):
            lw = max(1, w >> l)
            lh = max(1, h >> l)
            expected_bytes += level_size(fmt, lw, lh) * layers
        assert t["data_bytes"] == expected_bytes, f"Tex {t['name']} data_bytes mismatch: {t['data_bytes']} != {expected_bytes}"


def test_group_f_4():
    """Test F.4: Decoded PSNR checks vs uncompressed sources."""
    bake_info = _get_group_f_bake()
    sources = bake_info["sources"]
    meta = bake_info["meta"]
    textures = meta["textures"]

    # trim_albedo (BC1, >= 26 dB)
    t_alb = textures[0]
    data_alb0 = t_alb["data"][: level_size(0, 2048, 2048)]
    dec_alb0 = decode_bc1(data_alb0, 2048, 2048)
    p_alb = psnr(sources["trim_albedo"], dec_alb0[..., :3])
    assert p_alb >= 26.0, f"trim_albedo PSNR {p_alb:.2f} < 26.0"

    # trim_normal (BC5, >= 34 dB)
    t_norm = textures[1]
    data_norm0 = t_norm["data"][: level_size(3, 2048, 2048)]
    dec_norm0 = decode_bc5(data_norm0, 2048, 2048)
    p_norm = psnr(sources["trim_normal"], dec_norm0[..., :2])
    assert p_norm >= 34.0, f"trim_normal PSNR {p_norm:.2f} < 34.0"

    # trim_emissive (BC4, >= 38 dB)
    t_emis = textures[3]
    data_emis0 = t_emis["data"][: level_size(2, 2048, 2048)]
    dec_emis0 = decode_bc4(data_emis0, 2048, 2048)
    p_emis = psnr(sources["trim_emissive"], dec_emis0[..., 0])
    assert p_emis >= 38.0, f"trim_emissive PSNR {p_emis:.2f} < 38.0"

    # terrain_albedo layer 0 (BC1, >= 26 dB)
    t_terr = textures[4]
    data_terr0 = t_terr["data"][: level_size(0, 512, 512)]
    dec_terr0 = decode_bc1(data_terr0, 512, 512)
    p_terr = psnr(sources["terrain_albedo"][0], dec_terr0[..., :3])
    assert p_terr >= 26.0, f"terrain_albedo[0] PSNR {p_terr:.2f} < 26.0"

    # vfx_atlas (BC3, RGB >= 26 dB, A >= 36 dB)
    t_vfx = textures[7]
    data_vfx0 = t_vfx["data"][: level_size(1, 2048, 2048)]
    dec_vfx0 = decode_bc3(data_vfx0, 2048, 2048)
    p_vfx_c = psnr(sources["vfx_atlas"][..., :3], dec_vfx0[..., :3])
    p_vfx_a = psnr(sources["vfx_atlas"][..., 3], dec_vfx0[..., 3])
    assert p_vfx_c >= 26.0, f"vfx_atlas RGB PSNR {p_vfx_c:.2f} < 26.0"
    assert p_vfx_a >= 36.0, f"vfx_atlas A PSNR {p_vfx_a:.2f} < 36.0"

    # msdf_atlas (RGBA8, bit-identical)
    t_msdf = textures[8]
    data_msdf0 = t_msdf["data"][: level_size(4, 1024, 512)]
    dec_msdf0 = decode_rgba8(data_msdf0, 1024, 512)
    assert np.array_equal(sources["msdf_atlas"], dec_msdf0)


def test_group_f_5():
    """Test F.5: Stored probes match recomputed decode_texel results."""
    bake_info = _get_group_f_bake()
    meta = bake_info["meta"]

    for t in meta["textures"]:
        fmt = t["format"]
        w, h = t["width"], t["height"]
        layers, mips = t["layers"], t["mips"]

        # Probe A at (37, 91) of mip 0 layer 0
        size_lvl0 = level_size(fmt, w, h)
        blob_a = t["data"][:size_lvl0]
        calc_probe_a = tuple(decode_texel(fmt, blob_a, w, h, 37, 91))
        assert calc_probe_a == t["probe_a"], f"Tex {t['name']} probe_a {calc_probe_a} != {t['probe_a']}"

        # Probe B at (0, 0) of mip (mips-1) layer 0
        last_lvl = mips - 1
        last_w = max(1, w >> last_lvl)
        last_h = max(1, h >> last_lvl)
        off_b = level_layer_offset(fmt, w, h, layers, mips, last_lvl, 0)
        size_last = level_size(fmt, last_w, last_h)
        blob_b = t["data"][off_b : off_b + size_last]
        calc_probe_b = tuple(decode_texel(fmt, blob_b, last_w, last_h, 0, 0))
        assert calc_probe_b == t["probe_b"], f"Tex {t['name']} probe_b {calc_probe_b} != {t['probe_b']}"


def test_group_f_6():
    """Test F.6: Preview PNG header & content, manifest JSON structure."""
    bake_info = _get_group_f_bake()
    tmp = bake_info["tmp"]

    # Preview PNG
    preview_path = os.path.join(tmp, "textures_2026.preview.png")
    assert os.path.exists(preview_path)
    from asset_common.pngio import read_png_info, decode_png_rgb
    pw, ph = read_png_info(preview_path)
    assert pw == 2048 and ph == 1024

    preview_img = decode_png_rgb(preview_path)
    left_square = preview_img[:1024, :1024]
    bg = np.array([28, 30, 36], dtype=np.uint8)
    diff_mask = np.any(left_square != bg, axis=-1)
    diff_pct = np.mean(diff_mask)
    assert diff_pct >= 0.90, f"Trim preview non-bg fraction {diff_pct:.4f} < 0.90 (>= 90% required)"

    # Manifest JSON
    manifest_path = os.path.join(tmp, "textures_2026.manifest.json")
    assert os.path.exists(manifest_path)
    with open(manifest_path, "r", encoding="utf-8") as f:
        manifest = json.load(f)

    required_keys = [
        "textures", "trim_names", "terrain_layers", "vfx_effects",
        "icon_names", "glyph_count", "terrain_roughness", "trim_max_lod"
    ]
    for k in required_keys:
        assert k in manifest, f"Missing key {k} in manifest"

    assert len(manifest["textures"]) == 9
    assert len(manifest["trim_names"]) == 32
    assert len(manifest["terrain_layers"]) == 8
    assert len(manifest["vfx_effects"]) == 12
    assert len(manifest["icon_names"]) == 32
    assert manifest["glyph_count"] == 95
    assert len(manifest["terrain_roughness"]) == 8
    assert manifest["trim_max_lod"] == 5


def run_group_f():
    print("\n--- Running Group F Tests ---")
    tests = [
        ("Test F.1: Container inspection", test_group_f_1),
        ("Test F.2: CLI determinism (sha256)", test_group_f_2),
        ("Test F.3: File size and alignment", test_group_f_3),
        ("Test F.4: Decoded PSNR checks", test_group_f_4),
        ("Test F.5: Stored probes match decoded", test_group_f_5),
        ("Test F.6: Preview PNG & manifest check", test_group_f_6),
    ]
    passed = 0
    for name, fn in tests:
        try:
            fn()
            print(f"  PASS: {name}")
            passed += 1
        except Exception as e:
            print(f"  FAIL: {name}: {e}")
            import traceback
            traceback.print_exc()

    print(f"GROUP F {'PASS' if passed == len(tests) else 'FAIL'} ({passed}/{len(tests)})")
    return passed, len(tests)


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--group", default="ALL", help="Test group: A, B, C, D, E, F, or ALL")
    args = parser.parse_args()

    target = args.group.upper()
    tot_passed = 0
    tot_tests = 0

    if target in ("A", "ALL"):
        p, t = run_group_a()
        tot_passed += p
        tot_tests += t

    if target in ("B", "ALL"):
        p, t = run_group_b()
        tot_passed += p
        tot_tests += t

    if target in ("C", "ALL"):
        p, t = run_group_c()
        tot_passed += p
        tot_tests += t

    if target in ("D", "ALL"):
        p, t = run_group_d()
        tot_passed += p
        tot_tests += t

    if target in ("E", "ALL"):
        p, t = run_group_e()
        tot_passed += p
        tot_tests += t

    if target in ("F", "ALL"):
        p, t = run_group_f()
        tot_passed += p
        tot_tests += t





    if target == "ALL":
        print(f"\nALL GROUPS {'PASS' if tot_passed == tot_tests else 'FAIL'} ({tot_passed}/{tot_tests})")

    sys.exit(0 if tot_passed == tot_tests else 1)


if __name__ == "__main__":
    main()
