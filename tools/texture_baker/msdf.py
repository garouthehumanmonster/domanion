"""msdf.py - Multi-channel signed distance field generator."""

import os
import sys
import numpy as np

sys.path.insert(0, os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

import params

RANGE_PX = 4.0


def make_contours(raw_contours: list[tuple[np.ndarray, bool]]) -> list[tuple[np.ndarray, bool]]:
    """Normalize contour orientation: outer = CCW (area > 0), hole = CW (area < 0).

    Interior is always on the LEFT of each edge a -> b.
    """
    res = []
    for pts, is_hole in raw_contours:
        pts = np.asarray(pts, dtype=np.float64)
        x = pts[:, 0]
        y = pts[:, 1]
        area = 0.5 * np.sum(x * np.roll(y, -1) - np.roll(x, -1) * y)
        if not is_hole and area < 0:
            pts = pts[::-1]
        elif is_hole and area > 0:
            pts = pts[::-1]
        res.append((pts, is_hole))
    return res


def color_edges(points: np.ndarray) -> np.ndarray:
    """Assign channel masks to edges of a closed contour.

    A corner between consecutive edges is sharp if dot(u_{i-1}, u_i) < 0.8192 (cos 35 deg).
    Masks:
      0: 0b011 (R, G)
      1: 0b110 (G, B)
      2: 0b101 (B, R)
      WHITE: 0b111 (R, G, B)
    """
    pts = np.asarray(points, dtype=np.float64)
    N = len(pts)
    edges = np.roll(pts, -1, axis=0) - pts
    lens = np.sqrt(np.sum(edges * edges, axis=-1))
    unit_edges = edges / np.maximum(1e-8, lens[:, None])

    prev_u = np.roll(unit_edges, 1, axis=0)
    dots = np.sum(prev_u * unit_edges, axis=-1)
    sharp = dots < 0.8192
    k = int(np.sum(sharp))

    COLOR_MASKS = [0b011, 0b110, 0b101]
    WHITE_MASK = 0b111

    if k == 0:
        return np.full(N, WHITE_MASK, dtype=np.int32)
    elif k == 1:
        if N < 3:
            return np.full(N, WHITE_MASK, dtype=np.int32)
        c0 = int(np.where(sharp)[0][0])
        masks = np.zeros(N, dtype=np.int32)
        for i in range(N):
            idx = (c0 + i) % N
            grp = min(2, (i * 3) // N)
            masks[idx] = COLOR_MASKS[grp]
        return masks
    else:
        sharp_indices = np.where(sharp)[0]
        masks = np.zeros(N, dtype=np.int32)
        for j in range(k):
            start = sharp_indices[j]
            end = sharp_indices[(j + 1) % k]
            c_idx = j % 3
            if (k % 3 == 1) and (k >= 4) and (j == k - 1):
                c_idx = 1
            mask_val = COLOR_MASKS[c_idx]
            idx = start
            while idx != end:
                masks[idx] = mask_val
                idx = (idx + 1) % N
        return masks


def point_in_poly_even_odd(P: np.ndarray, all_edges: np.ndarray) -> np.ndarray:
    """Even-odd rule ray casting along +X axis to determine if points are inside."""
    Px = P[:, 0:1]
    Py = P[:, 1:2]
    Ax = all_edges[:, 0, 0]
    Ay = all_edges[:, 0, 1]
    Bx = all_edges[:, 1, 0]
    By = all_edges[:, 1, 1]

    cond1 = (Ay <= Py) & (Py < By)
    cond2 = (By <= Py) & (Py < Ay)
    cross_y = cond1 | cond2

    denom = By - Ay
    t = (Py - Ay) / np.where(np.abs(denom) < 1e-12, 1e-12, denom)
    x_int = Ax + t * (Bx - Ax)
    cross = cross_y & (x_int > Px)
    inside = (np.sum(cross, axis=1) % 2 == 1)
    return inside


def msdf_eval(points: np.ndarray, raw_contours: list[tuple[np.ndarray, bool]], use_pseudo: bool = True) -> np.ndarray:
    """Evaluate MSDF (R, G, B, true_sd) in distance units, positive inside."""
    contours = make_contours(raw_contours)

    edge_list_A = []
    edge_list_B = []
    edge_masks = []

    for pts, is_hole in contours:
        N = len(pts)
        masks = color_edges(pts)
        for i in range(N):
            edge_list_A.append(pts[i])
            edge_list_B.append(pts[(i + 1) % N])
            edge_masks.append(masks[i])

    A = np.array(edge_list_A, dtype=np.float64)  # [E, 2]
    B = np.array(edge_list_B, dtype=np.float64)  # [E, 2]
    masks = np.array(edge_masks, dtype=np.int32)  # [E]
    all_edges = np.stack([A, B], axis=1)  # [E, 2, 2]

    V = B - A  # [E, 2]
    L2 = np.sum(V * V, axis=-1)  # [E]
    L = np.sqrt(L2)  # [E]

    P = np.asarray(points, dtype=np.float64)
    N_pts = len(P)

    res = np.zeros((N_pts, 4), dtype=np.float64)

    chunk_sz = 2048
    for start in range(0, N_pts, chunk_sz):
        end = min(N_pts, start + chunk_sz)
        p_chunk = P[start:end]
        M = len(p_chunk)

        D = p_chunk[:, None, :] - A[None, :, :]
        dot = D[..., 0] * V[None, :, 0] + D[..., 1] * V[None, :, 1]
        t = dot / np.maximum(1e-12, L2[None, :])
        t_clamped = np.clip(t, 0.0, 1.0)

        Q = A[None, :, :] + t_clamped[..., None] * V[None, :, :]
        diff = p_chunk[:, None, :] - Q
        true_dist = np.sqrt(np.sum(diff * diff, axis=-1))

        cross = V[None, :, 0] * D[..., 1] - V[None, :, 1] * D[..., 0]
        perp_dist = cross / np.maximum(1e-12, L[None, :])

        # Channels 0, 1, 2
        for c in range(3):
            bit = 1 << c
            edge_idx = np.where((masks & bit) != 0)[0]
            if len(edge_idx) == 0:
                continue

            c_true_dist = true_dist[:, edge_idx]
            best_e = np.argmin(c_true_dist, axis=1)
            best_orig_e = edge_idx[best_e]

            m_idx = np.arange(M)
            b_t = t[m_idx, best_orig_e]
            b_perp = perp_dist[m_idx, best_orig_e]
            b_true = true_dist[m_idx, best_orig_e]
            b_cross = cross[m_idx, best_orig_e]

            if use_pseudo:
                val = b_perp
            else:
                inside_seg = (b_t >= 0.0) & (b_t <= 1.0)
                sign = np.where(b_cross >= 0.0, 1.0, -1.0)
                val = np.where(inside_seg, b_perp, sign * b_true)
            res[start:end, c] = val

        min_all_dist = np.min(true_dist, axis=1)
        inside = point_in_poly_even_odd(p_chunk, all_edges)
        res[start:end, 3] = np.where(inside, min_all_dist, -min_all_dist)

    return res


def render_icon(raw_contours: list[tuple[np.ndarray, bool]], use_pseudo: bool = None) -> np.ndarray:
    """Render a 64x64 icon into uint8 RGBA8 MSDF."""
    if use_pseudo is None:
        use_pseudo = params.MSDF_USE_PSEUDO

    # Translate contours by (32, 32)
    translated_contours = []
    for pts, is_hole in raw_contours:
        t_pts = np.asarray(pts, dtype=np.float64) + np.array([32.0, 32.0])
        translated_contours.append((t_pts, is_hole))

    # Grid of pixel centers in math coordinates (y-up: row r -> y = 64 - (r + 0.5))
    cols = np.arange(64, dtype=np.float64) + 0.5
    rows = np.arange(64, dtype=np.float64) + 0.5
    PX, PY = np.meshgrid(cols, 64.0 - rows)  # PX[r, c], PY[r, c]

    pts_flat = np.stack([PX.ravel(), PY.ravel()], axis=-1)  # [4096, 2]
    sd_vals = msdf_eval(pts_flat, translated_contours, use_pseudo=use_pseudo)  # [4096, 4]

    # Value encoding: v = clip(0.5 + sd_px/(2*RANGE_PX), 0, 1), byte = rint(v * 255)
    v = np.clip(0.5 + sd_vals / (2.0 * RANGE_PX), 0.0, 1.0)
    byte_vals = np.rint(v * 255.0).astype(np.uint8)

    icon_img = byte_vals.reshape(64, 64, 4)

    # Fill agreement post-process: for pixels where |true_sd| > 1.5, ensure (median > 127) == (alpha > 127)
    alpha = icon_img[..., 3]
    sd = (alpha.astype(float) / 255.0 - 0.5) * 8.0
    mask = np.abs(sd) > 1.5
    med = np.median(icon_img[..., :3], axis=-1)
    bad = mask & ((med > 127) != (alpha > 127))
    for c in range(3):
        icon_img[..., c] = np.where(bad, alpha, icon_img[..., c])

    return icon_img

