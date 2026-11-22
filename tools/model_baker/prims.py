"""prims.py - Procedural geometric 3D primitives with LOD support."""

from dataclasses import dataclass
import math
import numpy as np

import params
from asset_common.rng import Rng


@dataclass
class Piece:
    pos: np.ndarray   # float32 [n, 3]
    nrm: np.ndarray   # float32 [n, 3]
    uv: np.ndarray    # float32 [n, 2] in [0, 1]
    tris: np.ndarray  # int32 [m, 3] (CCW outward)


def _make_quad(v0, v1, v2, v3, normal):
    verts = np.array([v0, v1, v2, v3], dtype=np.float32)
    gn = np.cross(verts[1] - verts[0], verts[2] - verts[0])
    if np.dot(gn, normal) < 0:
        verts = np.array([v0, v3, v2, v1], dtype=np.float32)
        gn = np.cross(verts[1] - verts[0], verts[2] - verts[0])
    norm_len = np.linalg.norm(gn)
    if norm_len > 1e-9:
        gn /= norm_len
    norms = np.tile(gn, (4, 1)).astype(np.float32)

    # Planar UV fit
    edge0 = verts[1] - verts[0]
    na = np.linalg.norm(edge0)
    a = edge0 / (na if na > 1e-9 else 1.0)
    b = np.cross(gn, a)
    nb = np.linalg.norm(b)
    b = b / (nb if nb > 1e-9 else 1.0)

    pu = np.dot(verts - verts[0], a)
    pv = np.dot(verts - verts[0], b)
    du = np.max(pu) - np.min(pu)
    dv = np.max(pv) - np.min(pv)
    u = (pu - np.min(pu)) / (du if du > 1e-9 else 1.0)
    v = (pv - np.min(pv)) / (dv if dv > 1e-9 else 1.0)
    uv = np.stack([u, v], axis=1).astype(np.float32)
    tris = np.array([[0, 1, 2], [0, 2, 3]], dtype=np.int32)
    return verts, norms, uv, tris


def _make_tri(v0, v1, v2, normal):
    verts = np.array([v0, v1, v2], dtype=np.float32)
    gn = np.cross(verts[1] - verts[0], verts[2] - verts[0])
    if np.dot(gn, normal) < 0:
        verts = np.array([v0, v2, v1], dtype=np.float32)
        gn = np.cross(verts[1] - verts[0], verts[2] - verts[0])
    norm_len = np.linalg.norm(gn)
    if norm_len > 1e-9:
        gn /= norm_len
    norms = np.tile(gn, (3, 1)).astype(np.float32)

    edge0 = verts[1] - verts[0]
    na = np.linalg.norm(edge0)
    a = edge0 / (na if na > 1e-9 else 1.0)
    b = np.cross(gn, a)
    nb = np.linalg.norm(b)
    b = b / (nb if nb > 1e-9 else 1.0)

    pu = np.dot(verts - verts[0], a)
    pv = np.dot(verts - verts[0], b)
    du = np.max(pu) - np.min(pu)
    dv = np.max(pv) - np.min(pv)
    u = (pu - np.min(pu)) / (du if du > 1e-9 else 1.0)
    v = (pv - np.min(pv)) / (dv if dv > 1e-9 else 1.0)
    uv = np.stack([u, v], axis=1).astype(np.float32)
    tris = np.array([[0, 1, 2]], dtype=np.int32)
    return verts, norms, uv, tris


def box(sx: float, sy: float, sz: float) -> Piece:
    """Watertight box centered at the origin."""
    hx, hy, hz = sx / 2.0, sy / 2.0, sz / 2.0
    faces = [
        # +X
        ((hx, -hy, -hz), (hx, hy, -hz), (hx, hy, hz), (hx, -hy, hz), (1, 0, 0)),
        # -X
        ((-hx, -hy, hz), (-hx, hy, hz), (-hx, hy, -hz), (-hx, -hy, -hz), (-1, 0, 0)),
        # +Y
        ((-hx, hy, -hz), (-hx, hy, hz), (hx, hy, hz), (hx, hy, -hz), (0, 1, 0)),
        # -Y
        ((-hx, -hy, hz), (-hx, -hy, -hz), (hx, -hy, -hz), (hx, -hy, hz), (0, -1, 0)),
        # +Z
        ((hx, -hy, hz), (hx, hy, hz), (-hx, hy, hz), (-hx, -hy, hz), (0, 0, 1)),
        # -Z
        ((-hx, -hy, -hz), (-hx, hy, -hz), (hx, hy, -hz), (hx, -hy, -hz), (0, 0, -1)),
    ]
    all_v, all_n, all_uv, all_t = [], [], [], []
    v_off = 0
    for v0, v1, v2, v3, norm in faces:
        vs, ns, uvs, ts = _make_quad(v0, v1, v2, v3, norm)
        all_v.append(vs)
        all_n.append(ns)
        all_uv.append(uvs)
        all_t.append(ts + v_off)
        v_off += len(vs)

    return Piece(
        np.vstack(all_v), np.vstack(all_n),
        np.vstack(all_uv), np.vstack(all_t)
    )


def taper_box(bx: float, bz: float, tx: float, tz: float, h: float, dx: float = 0.0) -> Piece:
    """Tapered box with bottom rect bx x bz and top rect tx x tz."""
    b_hx, b_hz = bx / 2.0, bz / 2.0
    t_hx, t_hz = tx / 2.0, tz / 2.0
    hy = h / 2.0

    b_ne = (b_hx, -hy, -b_hz)
    b_se = (b_hx, -hy, b_hz)
    b_sw = (-b_hx, -hy, b_hz)
    b_nw = (-b_hx, -hy, -b_hz)

    t_ne = (dx + t_hx, hy, -t_hz)
    t_se = (dx + t_hx, hy, t_hz)
    t_sw = (dx - t_hx, hy, t_hz)
    t_nw = (dx - t_hx, hy, -t_hz)

    faces = [
        (t_nw, t_sw, t_se, t_ne, (0, 1, 0)),
        (b_nw, b_ne, b_se, b_sw, (0, -1, 0)),
        (b_ne, t_ne, t_se, b_se, (1, 0, 0)),
        (b_sw, t_sw, t_nw, b_nw, (-1, 0, 0)),
        (b_se, t_se, t_sw, b_sw, (0, 0, 1)),
        (b_nw, t_nw, t_ne, b_ne, (0, 0, -1)),
    ]
    all_v, all_n, all_uv, all_t = [], [], [], []
    v_off = 0
    for v0, v1, v2, v3, norm in faces:
        vs, ns, uvs, ts = _make_quad(v0, v1, v2, v3, norm)
        all_v.append(vs)
        all_n.append(ns)
        all_uv.append(uvs)
        all_t.append(ts + v_off)
        v_off += len(vs)

    return Piece(
        np.vstack(all_v), np.vstack(all_n),
        np.vstack(all_uv), np.vstack(all_t)
    )


def cylinder(r0: float, r1: float, h: float, lod: int = 0, caps: tuple[bool, bool] = (True, True)) -> Piece:
    """Cylinder or cone along Y axis with smooth normals and disc caps."""
    segs = params.CYL_SEGS[lod]
    r0 = float(r0)
    r1 = float(r1)
    h = float(h)
    hy = h / 2.0

    all_v, all_n, all_uv, all_t = [], [], [], []
    v_off = 0

    def side_n(t):
        n = np.array([h * math.cos(t), r0 - r1, h * math.sin(t)], dtype=np.float32)
        norm = np.linalg.norm(n)
        return n / (norm if norm > 1e-9 else 1.0)

    # 1. Sides
    if r0 > 0.0 and r1 > 0.0:
        for i in range(segs):
            t0 = 2.0 * math.pi * i / segs
            t1 = 2.0 * math.pi * (i + 1) / segs

            p0_bot = np.array([r0 * math.cos(t0), -hy, r0 * math.sin(t0)], dtype=np.float32)
            p1_bot = np.array([r0 * math.cos(t1), -hy, r0 * math.sin(t1)], dtype=np.float32)
            p1_top = np.array([r1 * math.cos(t1), hy, r1 * math.sin(t1)], dtype=np.float32)
            p0_top = np.array([r1 * math.cos(t0), hy, r1 * math.sin(t0)], dtype=np.float32)

            n0 = side_n(t0)
            n1 = side_n(t1)

            u0 = i / segs
            u1 = (i + 1) / segs

            all_v.extend([p0_bot, p0_top, p1_top, p1_bot])
            all_n.extend([n0, n0, n1, n1])
            all_uv.extend([
                np.array([u0, 0.0], dtype=np.float32),
                np.array([u0, 1.0], dtype=np.float32),
                np.array([u1, 1.0], dtype=np.float32),
                np.array([u1, 0.0], dtype=np.float32),
            ])
            all_t.append([v_off, v_off + 1, v_off + 2])
            all_t.append([v_off, v_off + 2, v_off + 3])
            v_off += 4
    elif r1 == 0.0:
        # Top apex
        for i in range(segs):
            t0 = 2.0 * math.pi * i / segs
            t1 = 2.0 * math.pi * (i + 1) / segs
            tmid = 0.5 * (t0 + t1)

            p0_bot = np.array([r0 * math.cos(t0), -hy, r0 * math.sin(t0)], dtype=np.float32)
            p1_bot = np.array([r0 * math.cos(t1), -hy, r0 * math.sin(t1)], dtype=np.float32)
            p_apex = np.array([0.0, hy, 0.0], dtype=np.float32)

            n0 = side_n(t0)
            n1 = side_n(t1)
            napex = side_n(tmid)

            u0 = i / segs
            u1 = (i + 1) / segs

            all_v.extend([p0_bot, p_apex, p1_bot])
            all_n.extend([n0, napex, n1])
            all_uv.extend([
                np.array([u0, 0.0], dtype=np.float32),
                np.array([0.5 * (u0 + u1), 1.0], dtype=np.float32),
                np.array([u1, 0.0], dtype=np.float32),
            ])
            all_t.append([v_off, v_off + 1, v_off + 2])
            v_off += 3
    elif r0 == 0.0:
        # Bottom apex
        for i in range(segs):
            t0 = 2.0 * math.pi * i / segs
            t1 = 2.0 * math.pi * (i + 1) / segs
            tmid = 0.5 * (t0 + t1)

            p_apex = np.array([0.0, -hy, 0.0], dtype=np.float32)
            p0_top = np.array([r1 * math.cos(t0), hy, r1 * math.sin(t0)], dtype=np.float32)
            p1_top = np.array([r1 * math.cos(t1), hy, r1 * math.sin(t1)], dtype=np.float32)

            napex = side_n(tmid)
            n0 = side_n(t0)
            n1 = side_n(t1)

            u0 = i / segs
            u1 = (i + 1) / segs

            all_v.extend([p_apex, p0_top, p1_top])
            all_n.extend([napex, n0, n1])
            all_uv.extend([
                np.array([0.5 * (u0 + u1), 0.0], dtype=np.float32),
                np.array([u0, 1.0], dtype=np.float32),
                np.array([u1, 1.0], dtype=np.float32),
            ])
            all_t.append([v_off, v_off + 1, v_off + 2])
            v_off += 3

    # 2. Bottom Cap
    if caps[0] and r0 > 0.0:
        base = v_off
        n_down = np.array([0.0, -1.0, 0.0], dtype=np.float32)
        for i in range(segs):
            t = 2.0 * math.pi * i / segs
            all_v.append(np.array([r0 * math.cos(t), -hy, r0 * math.sin(t)], dtype=np.float32))
            all_n.append(n_down)
            all_uv.append(np.array([0.5 + 0.5 * math.cos(t), 0.5 + 0.5 * math.sin(t)], dtype=np.float32))
        v_off += segs
        for i in range(1, segs - 1):
            all_t.append([base, base + i, base + i + 1])

    # 3. Top Cap
    if caps[1] and r1 > 0.0:
        base = v_off
        n_up = np.array([0.0, 1.0, 0.0], dtype=np.float32)
        for i in range(segs):
            t = 2.0 * math.pi * i / segs
            all_v.append(np.array([r1 * math.cos(t), hy, r1 * math.sin(t)], dtype=np.float32))
            all_n.append(n_up)
            all_uv.append(np.array([0.5 + 0.5 * math.cos(t), 0.5 + 0.5 * math.sin(t)], dtype=np.float32))
        v_off += segs
        for i in range(1, segs - 1):
            all_t.append([base, base + i + 1, base + i])

    return Piece(
        np.array(all_v, dtype=np.float32),
        np.array(all_n, dtype=np.float32),
        np.array(all_uv, dtype=np.float32),
        np.array(all_t, dtype=np.int32),
    )


def sphere(r: float, lod: int = 0, flat: bool = False, jitter: tuple[float, int] | None = None) -> Piece:
    """UV sphere with pole fans and optional flat shading / radial jitter."""
    lat, lon = params.SPHERE_SEGS[lod]
    r = float(r)

    def get_vert(i, j):
        theta = math.pi * i / lat
        phi = 2.0 * math.pi * j / lon
        y = r * math.cos(theta)
        r_ring = r * math.sin(theta)
        x = r_ring * math.cos(phi)
        z = r_ring * math.sin(phi)
        u = j / lon
        v = i / lat
        return np.array([x, y, z], dtype=np.float32), np.array([u, v], dtype=np.float32)

    tri_verts = []
    tri_uvs = []

    # Top fan
    for j in range(lon):
        p_top = np.array([0.0, r, 0.0], dtype=np.float32)
        uv_top = np.array([(j + 0.5) / lon, 0.0], dtype=np.float32)
        v1, uv1 = get_vert(1, j + 1)
        v2, uv2 = get_vert(1, j)
        tri_verts.append((p_top, v1, v2))
        tri_uvs.append((uv_top, uv1, uv2))

    # Middle quads
    for i in range(1, lat - 1):
        for j in range(lon):
            v_ij, uv_ij = get_vert(i, j)
            v_ij1, uv_ij1 = get_vert(i, j + 1)
            v_i1j1, uv_i1j1 = get_vert(i + 1, j + 1)
            v_i1j, uv_i1j = get_vert(i + 1, j)

            tri_verts.append((v_ij, v_ij1, v_i1j1))
            tri_uvs.append((uv_ij, uv_ij1, uv_i1j1))

            tri_verts.append((v_ij, v_i1j1, v_i1j))
            tri_uvs.append((uv_ij, uv_i1j1, uv_i1j))

    # Bottom fan
    for j in range(lon):
        p_bot = np.array([0.0, -r, 0.0], dtype=np.float32)
        uv_bot = np.array([(j + 0.5) / lon, 1.0], dtype=np.float32)
        v1, uv1 = get_vert(lat - 1, j)
        v2, uv2 = get_vert(lat - 1, j + 1)
        tri_verts.append((p_bot, v1, v2))
        tri_uvs.append((uv_bot, uv1, uv2))

    if jitter is not None:
        amp, seed = jitter
        rng = Rng(seed)
        p1 = rng.uniform() * 2.0 * math.pi
        p2 = rng.uniform() * 2.0 * math.pi
        p3 = rng.uniform() * 2.0 * math.pi

        def disp(v):
            dist = float(np.linalg.norm(v))
            if dist < 1e-9:
                return v
            dx, dy, dz = v[0] / dist, v[1] / dist, v[2] / dist
            f = math.sin(3.1 * dx + p1) * math.cos(2.7 * dy + p2) * math.sin(3.7 * dz + p3)
            return (v / dist) * (r * (1.0 + amp * f))

        tri_verts = [tuple(disp(v) for v in tri) for tri in tri_verts]

    pos_list, nrm_list, uv_list, tris_list = [], [], [], []
    v_idx = 0
    for tri, uvs in zip(tri_verts, tri_uvs):
        v0, v1, v2 = tri
        gn = np.cross(v1 - v0, v2 - v0)
        norm_gn = np.linalg.norm(gn)
        gn = gn / (norm_gn if norm_gn > 1e-9 else 1.0)
        for k in range(3):
            pos_list.append(tri[k])
            uv_list.append(uvs[k])
            if flat:
                nrm_list.append(gn)
            else:
                nv = tri[k] / np.linalg.norm(tri[k])
                nrm_list.append(nv)
        tris_list.append([v_idx, v_idx + 1, v_idx + 2])
        v_idx += 3

    return Piece(
        np.array(pos_list, dtype=np.float32),
        np.array(nrm_list, dtype=np.float32),
        np.array(uv_list, dtype=np.float32),
        np.array(tris_list, dtype=np.int32),
    )


def wedge(sx: float, sy: float, sz: float) -> Piece:
    """Right-triangle prism extruded along Z."""
    hx, hy, hz = sx / 2.0, sy / 2.0, sz / 2.0
    v0 = (-hx, -hy)
    v1 = (hx, -hy)
    v2 = (hx, hy)

    parts = [
        # +Z cap
        _make_tri((v0[0], v0[1], hz), (v1[0], v1[1], hz), (v2[0], v2[1], hz), (0, 0, 1)),
        # -Z cap
        _make_tri((v0[0], v0[1], -hz), (v2[0], v2[1], -hz), (v1[0], v1[1], -hz), (0, 0, -1)),
        # Bottom face
        _make_quad((v0[0], -hy, -hz), (v0[0], -hy, hz), (v1[0], -hy, hz), (v1[0], -hy, -hz), (0, -1, 0)),
        # Right face
        _make_quad((v1[0], -hy, -hz), (v1[0], -hy, hz), (v2[0], hy, hz), (v2[0], hy, -hz), (1, 0, 0)),
    ]
    # Hypotenuse face
    n_hyp = np.array([-sy, sx, 0.0], dtype=np.float32)
    n_hyp /= np.linalg.norm(n_hyp)
    parts.append(_make_quad((v2[0], hy, -hz), (v2[0], hy, hz), (v0[0], -hy, hz), (v0[0], -hy, -hz), n_hyp))

    all_v, all_n, all_uv, all_t = [], [], [], []
    v_off = 0
    for vs, ns, uvs, ts in parts:
        all_v.append(vs)
        all_n.append(ns)
        all_uv.append(uvs)
        all_t.append(ts + v_off)
        v_off += len(vs)

    return Piece(
        np.vstack(all_v), np.vstack(all_n),
        np.vstack(all_uv), np.vstack(all_t)
    )


def prism(poly: list[tuple[float, float]], h: float) -> Piece:
    """Convex polygon in XZ plane extruded from -h/2 to +h/2 along Y."""
    poly = [np.array([p[0], p[1]], dtype=np.float32) for p in poly]
    n = len(poly)
    if n < 3:
        raise ValueError("Polygon must have >= 3 points")

    # Check convexity & winding
    cross_sum = 0.0
    crosses = []
    for i in range(n):
        p0 = poly[i]
        p1 = poly[(i + 1) % n]
        p2 = poly[(i + 2) % n]
        d1 = p1 - p0
        d2 = p2 - p1
        c = d1[1] * d2[0] - d1[0] * d2[1]
        crosses.append(c)
        cross_sum += c

    if all(c < 0.0 for c in crosses):
        poly = poly[::-1]
    elif not all(c > 0.0 for c in crosses):
        raise AssertionError("Polygon is not strictly convex")

    hy = h / 2.0
    all_v, all_n, all_uv, all_t = [], [], [], []
    v_off = 0

    # Top face (+Y)
    top_v = [np.array([p[0], hy, p[1]], dtype=np.float32) for p in poly]
    n_up = np.array([0.0, 1.0, 0.0], dtype=np.float32)
    pts = np.array(top_v)
    pu = pts[:, 0]
    pv = pts[:, 2]
    du = np.max(pu) - np.min(pu)
    dv = np.max(pv) - np.min(pv)
    top_uv = np.stack([
        (pu - np.min(pu)) / (du if du > 1e-9 else 1.0),
        (pv - np.min(pv)) / (dv if dv > 1e-9 else 1.0),
    ], axis=1).astype(np.float32)

    base_top = v_off
    all_v.extend(top_v)
    all_n.extend([n_up] * n)
    all_uv.extend(top_uv)
    v_off += n
    for i in range(1, n - 1):
        all_t.append([base_top, base_top + i, base_top + i + 1])

    # Bottom face (-Y)
    bot_v = [np.array([p[0], -hy, p[1]], dtype=np.float32) for p in poly]
    n_down = np.array([0.0, -1.0, 0.0], dtype=np.float32)
    base_bot = v_off
    all_v.extend(bot_v)
    all_n.extend([n_down] * n)
    all_uv.extend(top_uv)
    v_off += n
    for i in range(1, n - 1):
        all_t.append([base_bot, base_bot + i + 1, base_bot + i])

    # Sides
    for i in range(n):
        p0 = poly[i]
        p1 = poly[(i + 1) % n]
        v0 = np.array([p0[0], -hy, p0[1]], dtype=np.float32)
        v1 = np.array([p1[0], -hy, p1[1]], dtype=np.float32)
        v2 = np.array([p1[0], hy, p1[1]], dtype=np.float32)
        v3 = np.array([p0[0], hy, p0[1]], dtype=np.float32)

        d = p1 - p0
        side_n = np.array([-d[1], 0.0, d[0]], dtype=np.float32)
        side_n /= np.linalg.norm(side_n)

        side_verts = [v0, v1, v2, v3]
        u = [0.0, 1.0, 1.0, 0.0]
        v = [0.0, 0.0, 1.0, 1.0]
        base_side = v_off
        all_v.extend(side_verts)
        all_n.extend([side_n] * 4)
        all_uv.extend([np.array([u[k], v[k]], dtype=np.float32) for k in range(4)])
        v_off += 4
        all_t.append([base_side, base_side + 1, base_side + 2])
        all_t.append([base_side, base_side + 2, base_side + 3])

    return Piece(
        np.array(all_v, dtype=np.float32),
        np.array(all_n, dtype=np.float32),
        np.array(all_uv, dtype=np.float32),
        np.array(all_t, dtype=np.int32),
    )
