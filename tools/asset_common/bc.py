"""bc.py - Fast vectorized BC1, BC3, BC4, BC5 codecs and quality metrics."""

import math
import struct
import numpy as np


def pack_565(rgb):
    """Pack RGB [..., 3] uint8/int32 to uint16 565."""
    r = np.asarray(rgb[..., 0], dtype=np.int32)
    g = np.asarray(rgb[..., 1], dtype=np.int32)
    b = np.asarray(rgb[..., 2], dtype=np.int32)
    q5_r = (r * 31 + 127) // 255
    q6_g = (g * 63 + 127) // 255
    q5_b = (b * 31 + 127) // 255
    return ((q5_r << 11) | (q6_g << 5) | q5_b).astype(np.uint16)


def unpack_565(c):
    """Unpack uint16/uint32 565 to RGB [..., 3] uint8."""
    c = np.asarray(c, dtype=np.uint32)
    r5 = (c >> 11) & 0x1F
    g6 = (c >> 5) & 0x3F
    b5 = c & 0x1F
    r8 = (r5 << 3) | (r5 >> 2)
    g8 = (g6 << 2) | (g6 >> 4)
    b8 = (b5 << 3) | (b5 >> 2)
    return np.stack([r8, g8, b8], axis=-1).astype(np.uint8)


def _pad_image(img, block_size=4):
    """Pad image height and width to multiples of block_size by edge replication."""
    h, w = img.shape[:2]
    pad_h = (block_size - (h % block_size)) % block_size
    pad_w = (block_size - (w % block_size)) % block_size
    if pad_h == 0 and pad_w == 0:
        return img, h, w
    padding = [(0, pad_h), (0, pad_w)]
    if img.ndim == 3:
        padding.append((0, 0))
    return np.pad(img, padding, mode="edge"), h, w


def encode_bc1(rgb):
    """Encode uint8 RGB image [H, W, 3] to BC1 bytes."""
    padded, _, _ = _pad_image(np.asarray(rgb, dtype=np.uint8), 4)
    ph, pw = padded.shape[:2]
    bx = pw // 4
    by = ph // 4
    blocks = padded.reshape(by, 4, bx, 4, 3).transpose(0, 2, 1, 3, 4).reshape(-1, 16, 3)
    n_blocks = len(blocks)

    mn = np.min(blocks, axis=1).astype(np.int32)
    mx = np.max(blocks, axis=1).astype(np.int32)
    d = (mx - mn) >> 4
    hi = mx - d
    lo = mn + d

    c0 = pack_565(hi)
    c1 = pack_565(lo)
    swap = c0 < c1
    if np.any(swap):
        c0 = c0.copy()
        c1 = c1.copy()
        c0[swap], c1[swap] = c1[swap], c0[swap]

    p0 = unpack_565(c0).astype(np.int32)
    p1 = unpack_565(c1).astype(np.int32)
    p2 = (2 * p0 + p1 + 1) // 3
    p3 = (p0 + 2 * p1 + 1) // 3
    pal = np.stack([p0, p1, p2, p3], axis=1)  # [N, 4, 3]

    diff = blocks[:, :, None, :].astype(np.int32) - pal[:, None, :, :]
    err = np.sum(diff * diff, axis=-1)  # [N, 16, 4]
    best_idx = np.argmin(err, axis=-1).astype(np.uint32)

    equal_mask = c0 == c1
    if np.any(equal_mask):
        best_idx[equal_mask] = 0

    shifts = np.arange(0, 32, 2, dtype=np.uint32)
    idx_packed = np.sum(best_idx << shifts[None, :], axis=1, dtype=np.uint32)

    dtype = np.dtype([("c0", "<u2"), ("c1", "<u2"), ("idx", "<u4")])
    out = np.empty(n_blocks, dtype=dtype)
    out["c0"] = c0
    out["c1"] = c1
    out["idx"] = idx_packed
    return out.tobytes()


def encode_bc4_arr(v):
    """Encode uint8 2D array [H, W] to BC4 structured array."""
    padded, _, _ = _pad_image(np.asarray(v, dtype=np.uint8), 4)
    ph, pw = padded.shape[:2]
    bx = pw // 4
    by = ph // 4
    blocks = padded.reshape(by, 4, bx, 4).transpose(0, 2, 1, 3).reshape(-1, 16)
    n_blocks = len(blocks)

    r0 = np.max(blocks, axis=1).astype(np.int32)
    r1 = np.min(blocks, axis=1).astype(np.int32)

    pal = np.zeros((n_blocks, 8), dtype=np.int32)
    pal[:, 0] = r0
    pal[:, 1] = r1
    for i in range(1, 7):
        pal[:, 1 + i] = ((7 - i) * r0 + i * r1 + 3) // 7

    diff = np.abs(blocks[:, :, None].astype(np.int32) - pal[:, None, :])
    best_idx = np.argmin(diff, axis=-1).astype(np.uint64)

    equal_mask = r0 == r1
    if np.any(equal_mask):
        best_idx[equal_mask] = 0

    shifts = np.arange(16, dtype=np.uint64) * 3
    idx_64 = np.sum(best_idx << shifts[None, :], axis=1, dtype=np.uint64)

    dtype = np.dtype([("r0", "u1"), ("r1", "u1"), ("idx_low", "<u4"), ("idx_high", "<u2")])
    out = np.empty(n_blocks, dtype=dtype)
    out["r0"] = r0.astype(np.uint8)
    out["r1"] = r1.astype(np.uint8)
    out["idx_low"] = idx_64.astype(np.uint32)
    out["idx_high"] = (idx_64 >> np.uint64(32)).astype(np.uint16)
    return out


def encode_bc4(v):
    """Encode uint8 array [H, W] to BC4 bytes."""
    return encode_bc4_arr(v).tobytes()


def encode_bc5(rg):
    """Encode uint8 RG array [H, W, 2] to BC5 bytes."""
    rg = np.asarray(rg, dtype=np.uint8)
    r_blocks = encode_bc4_arr(rg[..., 0])
    g_blocks = encode_bc4_arr(rg[..., 1])
    n_blocks = len(r_blocks)

    bc4_dt = r_blocks.dtype
    bc5_dt = np.dtype([("r", bc4_dt), ("g", bc4_dt)])
    out = np.empty(n_blocks, dtype=bc5_dt)
    out["r"] = r_blocks
    out["g"] = g_blocks
    return out.tobytes()


def encode_bc3(rgba):
    """Encode uint8 RGBA array [H, W, 4] to BC3 bytes (alpha BC4 + rgb BC1)."""
    rgba = np.asarray(rgba, dtype=np.uint8)
    a_blocks = encode_bc4_arr(rgba[..., 3])

    # Color via BC1
    padded, _, _ = _pad_image(rgba[..., :3], 4)
    ph, pw = padded.shape[:2]
    bx = pw // 4
    by = ph // 4
    blocks = padded.reshape(by, 4, bx, 4, 3).transpose(0, 2, 1, 3, 4).reshape(-1, 16, 3)
    n_blocks = len(blocks)

    mn = np.min(blocks, axis=1).astype(np.int32)
    mx = np.max(blocks, axis=1).astype(np.int32)
    d = (mx - mn) >> 4
    hi = mx - d
    lo = mn + d

    c0 = pack_565(hi)
    c1 = pack_565(lo)
    swap = c0 < c1
    if np.any(swap):
        c0 = c0.copy()
        c1 = c1.copy()
        c0[swap], c1[swap] = c1[swap], c0[swap]

    p0 = unpack_565(c0).astype(np.int32)
    p1 = unpack_565(c1).astype(np.int32)
    p2 = (2 * p0 + p1 + 1) // 3
    p3 = (p0 + 2 * p1 + 1) // 3
    pal = np.stack([p0, p1, p2, p3], axis=1)

    diff = blocks[:, :, None, :].astype(np.int32) - pal[:, None, :, :]
    err = np.sum(diff * diff, axis=-1)
    best_idx = np.argmin(err, axis=-1).astype(np.uint32)

    equal_mask = c0 == c1
    if np.any(equal_mask):
        best_idx[equal_mask] = 0

    shifts = np.arange(0, 32, 2, dtype=np.uint32)
    idx_packed = np.sum(best_idx << shifts[None, :], axis=1, dtype=np.uint32)

    bc1_dt = np.dtype([("c0", "<u2"), ("c1", "<u2"), ("idx", "<u4")])
    rgb_blocks = np.empty(n_blocks, dtype=bc1_dt)
    rgb_blocks["c0"] = c0
    rgb_blocks["c1"] = c1
    rgb_blocks["idx"] = idx_packed

    bc3_dt = np.dtype([("alpha", a_blocks.dtype), ("color", bc1_dt)])
    out = np.empty(n_blocks, dtype=bc3_dt)
    out["alpha"] = a_blocks
    out["color"] = rgb_blocks
    return out.tobytes()


def decode_bc1_blocks_arr(data, n_blocks, force_4color=False):
    """Decode raw bytes to array of shape [n_blocks, 16, 4] RGBA."""
    dt = np.dtype([("c0", "<u2"), ("c1", "<u2"), ("idx", "<u4")])
    blocks = np.frombuffer(data, dtype=dt, count=n_blocks)
    c0 = blocks["c0"]
    c1 = blocks["c1"]
    idx = blocks["idx"]

    p0 = unpack_565(c0).astype(np.int32)
    p1 = unpack_565(c1).astype(np.int32)

    # 4-colour mode where c0 > c1 or force_4color
    p2_4 = (2 * p0 + p1 + 1) // 3
    p3_4 = (p0 + 2 * p1 + 1) // 3

    # 3-colour mode
    p2_3 = (p0 + p1) // 2

    is_4 = (c0 > c1) | force_4color
    p2 = np.where(is_4[:, None], p2_4, p2_3)
    p3 = np.where(is_4[:, None], p3_4, 0)
    a3 = np.where(is_4, 255, 0).astype(np.uint8)

    # Palettes: [N, 4, 4]
    a_full = np.full(n_blocks, 255, dtype=np.uint8)
    pal0 = np.concatenate([p0.astype(np.uint8), a_full[:, None]], axis=-1)
    pal1 = np.concatenate([p1.astype(np.uint8), a_full[:, None]], axis=-1)
    pal2 = np.concatenate([p2.astype(np.uint8), a_full[:, None]], axis=-1)
    pal3 = np.concatenate([p3.astype(np.uint8), a3[:, None]], axis=-1)
    pal = np.stack([pal0, pal1, pal2, pal3], axis=1)

    shifts = np.arange(0, 32, 2, dtype=np.uint32)
    indices = ((idx[:, None] >> shifts[None, :]) & np.uint32(3)).astype(np.int64)
    # Gather palette entries: [N, 16, 4]
    rows = np.arange(n_blocks)[:, None]
    return pal[rows, indices]


def decode_bc1(data, w, h):
    """Decode BC1 bytes to uint8 RGBA array [h, w, 4]."""
    bx = max(1, (w + 3) // 4)
    by = max(1, (h + 3) // 4)
    n_blocks = bx * by
    pixels = decode_bc1_blocks_arr(data, n_blocks, force_4color=False)
    img = pixels.reshape(by, bx, 4, 4, 4).transpose(0, 2, 1, 3, 4).reshape(by * 4, bx * 4, 4)
    return img[:h, :w, :].copy()


def decode_bc4_blocks_arr(data, n_blocks):
    """Decode raw BC4 bytes to array of shape [n_blocks, 16] uint8."""
    dt = np.dtype([("r0", "u1"), ("r1", "u1"), ("idx_low", "<u4"), ("idx_high", "<u2")])
    blocks = np.frombuffer(data, dtype=dt, count=n_blocks)
    r0 = blocks["r0"].astype(np.int32)
    r1 = blocks["r1"].astype(np.int32)
    idx_64 = blocks["idx_low"].astype(np.uint64) | (blocks["idx_high"].astype(np.uint64) << np.uint64(32))

    pal = np.zeros((n_blocks, 8), dtype=np.uint8)
    pal[:, 0] = r0.astype(np.uint8)
    pal[:, 1] = r1.astype(np.uint8)

    mode8 = r0 > r1
    # Mode 8
    for i in range(1, 7):
        v = ((7 - i) * r0 + i * r1 + 3) // 7
        pal[:, 1 + i] = np.where(mode8, v, pal[:, 1 + i])

    # Mode 6
    for i in range(1, 5):
        v = ((5 - i) * r0 + i * r1 + 2) // 5
        pal[:, 1 + i] = np.where(~mode8, v, pal[:, 1 + i])
    pal[:, 6] = np.where(~mode8, 0, pal[:, 6])
    pal[:, 7] = np.where(~mode8, 255, pal[:, 7])

    shifts = np.arange(16, dtype=np.uint64) * 3
    indices = ((idx_64[:, None] >> shifts[None, :]) & np.uint64(7)).astype(np.int64)
    rows = np.arange(n_blocks)[:, None]
    return pal[rows, indices]


def decode_bc4(data, w, h):
    """Decode BC4 bytes to uint8 RGBA array [h, w, 4] with (v, 0, 0, 255)."""
    bx = max(1, (w + 3) // 4)
    by = max(1, (h + 3) // 4)
    n_blocks = bx * by
    r_vals = decode_bc4_blocks_arr(data, n_blocks)
    img_r = r_vals.reshape(by, bx, 4, 4).transpose(0, 2, 1, 3).reshape(by * 4, bx * 4)
    cropped = img_r[:h, :w]
    out = np.zeros((h, w, 4), dtype=np.uint8)
    out[..., 0] = cropped
    out[..., 3] = 255
    return out


def decode_bc5(data, w, h):
    """Decode BC5 bytes to uint8 RGBA array [h, w, 4] with (R, G, 0, 255)."""
    bx = max(1, (w + 3) // 4)
    by = max(1, (h + 3) // 4)
    n_blocks = bx * by
    dt = np.dtype([("r", "V8"), ("g", "V8")])
    arr = np.frombuffer(data, dtype=dt, count=n_blocks)

    r_vals = decode_bc4_blocks_arr(arr["r"].tobytes(), n_blocks)
    g_vals = decode_bc4_blocks_arr(arr["g"].tobytes(), n_blocks)

    img_r = r_vals.reshape(by, bx, 4, 4).transpose(0, 2, 1, 3).reshape(by * 4, bx * 4)[:h, :w]
    img_g = g_vals.reshape(by, bx, 4, 4).transpose(0, 2, 1, 3).reshape(by * 4, bx * 4)[:h, :w]

    out = np.zeros((h, w, 4), dtype=np.uint8)
    out[..., 0] = img_r
    out[..., 1] = img_g
    out[..., 3] = 255
    return out


def decode_bc3(data, w, h):
    """Decode BC3 bytes to uint8 RGBA array [h, w, 4] (RGB from BC1 4-colour, A from BC4)."""
    bx = max(1, (w + 3) // 4)
    by = max(1, (h + 3) // 4)
    n_blocks = bx * by
    dt = np.dtype([("a", "V8"), ("c", "V8")])
    arr = np.frombuffer(data, dtype=dt, count=n_blocks)

    a_vals = decode_bc4_blocks_arr(arr["a"].tobytes(), n_blocks)
    c_pixels = decode_bc1_blocks_arr(arr["c"].tobytes(), n_blocks, force_4color=True)

    img_a = a_vals.reshape(by, bx, 4, 4).transpose(0, 2, 1, 3).reshape(by * 4, bx * 4)[:h, :w]
    img_c = c_pixels.reshape(by, bx, 4, 4, 4).transpose(0, 2, 1, 3, 4).reshape(by * 4, bx * 4, 4)[:h, :w, :3]

    out = np.empty((h, w, 4), dtype=np.uint8)
    out[..., :3] = img_c
    out[..., 3] = img_a
    return out


def decode_rgba8(data, w, h):
    """Decode uncompressed RGBA8 bytes to [h, w, 4] array."""
    return np.frombuffer(data, dtype=np.uint8, count=w * h * 4).reshape(h, w, 4).copy()


def decode_texel(fmt, data, w, h, x, y):
    """Decode a single texel at (x, y) from binary data without decoding full image."""
    x = max(0, min(int(x), int(w) - 1))
    y = max(0, min(int(y), int(h) - 1))
    if fmt == 4:  # RGBA8
        offset = (int(y) * int(w) + int(x)) * 4
        return tuple(int(b) for b in data[offset : offset + 4])

    bx = int(x) // 4
    by = int(y) // 4
    blocks_x = max(1, (int(w) + 3) // 4)
    b_idx = by * blocks_x + bx
    lx = int(x) % 4
    ly = int(y) % 4
    k = ly * 4 + lx

    if fmt == 0:  # BC1
        off = b_idx * 8
        c0, c1, idx = struct.unpack("<HHI", data[off : off + 8])
        r5_0 = (c0 >> 11) & 0x1F
        g6_0 = (c0 >> 5) & 0x3F
        b5_0 = c0 & 0x1F
        p0 = [(r5_0 << 3) | (r5_0 >> 2), (g6_0 << 2) | (g6_0 >> 4), (b5_0 << 3) | (b5_0 >> 2), 255]

        r5_1 = (c1 >> 11) & 0x1F
        g6_1 = (c1 >> 5) & 0x3F
        b5_1 = c1 & 0x1F
        p1 = [(r5_1 << 3) | (r5_1 >> 2), (g6_1 << 2) | (g6_1 >> 4), (b5_1 << 3) | (b5_1 >> 2), 255]

        if c0 > c1:
            p2 = [(2 * p0[c] + p1[c] + 1) // 3 for c in range(3)] + [255]
            p3 = [(p0[c] + 2 * p1[c] + 1) // 3 for c in range(3)] + [255]
        else:
            p2 = [(p0[c] + p1[c]) // 2 for c in range(3)] + [255]
            p3 = [0, 0, 0, 0]
        pal = [p0, p1, p2, p3]
        pidx = (idx >> (2 * k)) & 0x3
        return tuple(pal[pidx])

    if fmt == 2:  # BC4
        off = b_idx * 8
        r0, r1 = data[off], data[off + 1]
        idx_val = int.from_bytes(data[off + 2 : off + 8], "little")
        pal = [0] * 8
        pal[0] = r0
        pal[1] = r1
        if r0 > r1:
            for i in range(1, 7):
                pal[1 + i] = ((7 - i) * r0 + i * r1 + 3) // 7
        else:
            for i in range(1, 5):
                pal[1 + i] = ((5 - i) * r0 + i * r1 + 2) // 5
            pal[6] = 0
            pal[7] = 255
        pidx = (idx_val >> (3 * k)) & 0x7
        return (pal[pidx], 0, 0, 255)

    if fmt == 3:  # BC5
        off = b_idx * 16
        # R channel
        r0, r1 = data[off], data[off + 1]
        idx_r = int.from_bytes(data[off + 2 : off + 8], "little")
        pal_r = [0] * 8
        pal_r[0] = r0
        pal_r[1] = r1
        if r0 > r1:
            for i in range(1, 7):
                pal_r[1 + i] = ((7 - i) * r0 + i * r1 + 3) // 7
        else:
            for i in range(1, 5):
                pal_r[1 + i] = ((5 - i) * r0 + i * r1 + 2) // 5
            pal_r[6] = 0
            pal_r[7] = 255
        pr = pal_r[(idx_r >> (3 * k)) & 0x7]

        # G channel
        g0, g1 = data[off + 8], data[off + 9]
        idx_g = int.from_bytes(data[off + 10 : off + 16], "little")
        pal_g = [0] * 8
        pal_g[0] = g0
        pal_g[1] = g1
        if g0 > g1:
            for i in range(1, 7):
                pal_g[1 + i] = ((7 - i) * g0 + i * g1 + 3) // 7
        else:
            for i in range(1, 5):
                pal_g[1 + i] = ((5 - i) * g0 + i * g1 + 2) // 5
            pal_g[6] = 0
            pal_g[7] = 255
        pg = pal_g[(idx_g >> (3 * k)) & 0x7]
        return (pr, pg, 0, 255)

    if fmt == 1:  # BC3
        off = b_idx * 16
        # Alpha channel (BC4)
        a0, a1 = data[off], data[off + 1]
        idx_a = int.from_bytes(data[off + 2 : off + 8], "little")
        pal_a = [0] * 8
        pal_a[0] = a0
        pal_a[1] = a1
        if a0 > a1:
            for i in range(1, 7):
                pal_a[1 + i] = ((7 - i) * a0 + i * a1 + 3) // 7
        else:
            for i in range(1, 5):
                pal_a[1 + i] = ((5 - i) * a0 + i * a1 + 2) // 5
            pal_a[6] = 0
            pal_a[7] = 255
        pa = pal_a[(idx_a >> (3 * k)) & 0x7]

        # Color channel (BC1 in 4-colour mode)
        c0, c1, idx_c = struct.unpack("<HHI", data[off + 8 : off + 16])
        r5_0 = (c0 >> 11) & 0x1F
        g6_0 = (c0 >> 5) & 0x3F
        b5_0 = c0 & 0x1F
        p0 = [(r5_0 << 3) | (r5_0 >> 2), (g6_0 << 2) | (g6_0 >> 4), (b5_0 << 3) | (b5_0 >> 2)]
        r5_1 = (c1 >> 11) & 0x1F
        g6_1 = (c1 >> 5) & 0x3F
        b5_1 = c1 & 0x1F
        p1 = [(r5_1 << 3) | (r5_1 >> 2), (g6_1 << 2) | (g6_1 >> 4), (b5_1 << 3) | (b5_1 >> 2)]
        p2 = [(2 * p0[c] + p1[c] + 1) // 3 for c in range(3)]
        p3 = [(p0[c] + 2 * p1[c] + 1) // 3 for c in range(3)]
        pal_c = [p0, p1, p2, p3]
        pc = pal_c[(idx_c >> (2 * k)) & 0x3]
        return (pc[0], pc[1], pc[2], pa)

    raise ValueError(f"Unknown format {fmt}")


def block_bytes(fmt):
    """Return size in bytes of a single 4x4 block for format."""
    if fmt in (0, 2):
        return 8
    if fmt in (1, 3):
        return 16
    if fmt == 4:
        return 4
    raise ValueError(f"Unknown format {fmt}")


def level_size(fmt, w, h):
    """Compute data byte size for a 2D mip level."""
    if fmt == 4:
        return int(w) * int(h) * 4
    bx = max(1, (int(w) + 3) // 4)
    by = max(1, (int(h) + 3) // 4)
    return bx * by * block_bytes(fmt)


def psnr(a, b):
    """Peak Signal-to-Noise Ratio (dB) across all channels."""
    diff = np.asarray(a, dtype=np.float64) - np.asarray(b, dtype=np.float64)
    mse = np.mean(diff * diff)
    if mse == 0.0:
        return 99.0
    return 10.0 * math.log10((255.0 * 255.0) / mse)
