"""shapes.py - Procedural 2D geometry definitions for the 32 tactical and UI icons."""

import numpy as np


def rect(cx, cy, w, h):
    """CCW rectangle centered at (cx, cy)."""
    hw, hh = w * 0.5, h * 0.5
    return np.array([
        [cx - hw, cy - hh],
        [cx + hw, cy - hh],
        [cx + hw, cy + hh],
        [cx - hw, cy + hh],
    ], dtype=np.float64)


def poly(points):
    """Polygon from points list."""
    return np.asarray(points, dtype=np.float64)


def rotate(points, deg):
    """Rotate points CCW by deg degrees around origin."""
    rad = np.radians(deg)
    c, s = np.cos(rad), np.sin(rad)
    rot_mat = np.array([[c, -s], [s, c]], dtype=np.float64)
    return points @ rot_mat.T


def regular_polygon(cx, cy, r, n, rot_deg=0.0):
    """Regular CCW n-gon centered at (cx, cy)."""
    angles = np.radians(rot_deg) + np.linspace(0, 2.0 * np.pi, n, endpoint=False)
    pts = np.stack([cx + r * np.cos(angles), cy + r * np.sin(angles)], axis=-1)
    return pts


def ellipse(cx, cy, rx, ry, n=48):
    """CCW ellipse centered at (cx, cy)."""
    angles = np.linspace(0, 2.0 * np.pi, n, endpoint=False)
    pts = np.stack([cx + rx * np.cos(angles), cy + ry * np.sin(angles)], axis=-1)
    return pts


def circle(cx, cy, r, n=48):
    """CCW circle centered at (cx, cy)."""
    return ellipse(cx, cy, r, r, n)


def star(cx, cy, R, r, n=5):
    """CCW star with n points, tip pointing up."""
    # Tip at angle pi/2
    angles = np.pi * 0.5 + np.linspace(0, 2.0 * np.pi, 2 * n, endpoint=False)
    rads = np.where(np.arange(2 * n) % 2 == 0, R, r)
    pts = np.stack([cx + rads * np.cos(angles), cy + rads * np.sin(angles)], axis=-1)
    return pts


def plus_shape(arm, thick):
    """12-vertex plus shape centered at origin."""
    h = thick * 0.5
    L = arm
    pts = np.array([
        [h, h], [h, L], [-h, L], [-h, h],
        [-L, h], [-L, -h], [-h, -h], [-h, -L],
        [h, -L], [h, -h], [L, -h], [L, h]
    ], dtype=np.float64)
    return pts


def ring(outer_pts, inner_pts):
    """Create a ring shape: returns tuple of (outer_pts, inner_pts)."""
    return [
        (outer_pts, False),  # outer contour
        (inner_pts, True),   # inner hole contour
    ]


def get_icon_contours(idx: int) -> list[tuple[np.ndarray, bool]]:
    """Return list of (points, is_hole) contours for the given icon index (0..31)."""
    assert 0 <= idx < 32

    if idx == 0:  # frame_friendly
        return [(rect(0, 0, 44, 30), False)]
    elif idx == 1:  # frame_hostile (diamond)
        return [(regular_polygon(0, 0, 24, 4, 0), False)]
    elif idx == 2:  # frame_neutral
        return [(rect(0, 0, 36, 36), False)]
    elif idx == 3:  # frame_unknown (octagon)
        return [(regular_polygon(0, 0, 24, 8, 22.5), False)]
    elif idx == 4:  # sym_infantry
        return [(rotate(plus_shape(16, 5), 45), False)]
    elif idx == 5:  # sym_armor
        return ring(ellipse(0, 0, 17, 11), ellipse(0, 0, 11, 5))
    elif idx == 6:  # sym_artillery
        return [(circle(0, 0, 7), False)]
    elif idx == 7:  # sym_air
        return [(poly([(-20, -4), (0, 12), (20, -4), (20, -12), (0, 4), (-20, -12)]), False)]
    elif idx == 8:  # sym_naval (two contours: trapezoid and triangle)
        return [
            (poly([(-18, -4), (18, -4), (12, -12), (-12, -12)]), False),
            (poly([(-2, 0), (-2, 16), (10, 0)]), False)
        ]
    elif idx == 9:  # sym_recon
        return [(poly([(-14, -12), (-8, -12), (14, 12), (8, 12)]), False)]
    elif idx == 10:  # sym_missile
        return [(poly([(0, 16), (8, 4), (3, 4), (3, -14), (-3, -14), (-3, 4), (-8, 4)]), False)]
    elif idx == 11:  # sym_engineer (H polygon)
        return [(poly([(-12, -12), (-6, -12), (-6, -3), (6, -3), (6, -12), (12, -12),
                       (12, 12), (6, 12), (6, 3), (-6, 3), (-6, 12), (-12, 12)]), False)]
    elif idx == 12:  # sym_logistics (triangle)
        return [(regular_polygon(0, 0, 14, 3, 90), False)]
    elif idx == 13:  # sym_drone (4 circles r4 plus center rect 8x8)
        return [
            (circle(-10, -10, 4), False),
            (circle(10, -10, 4), False),
            (circle(-10, 10, 4), False),
            (circle(10, 10, 4), False),
            (rect(0, 0, 8, 8), False),
        ]
    elif idx == 14:  # res_food (circle r14 at (0,-2) plus leaf triangle)
        return [
            (circle(0, -2, 14), False),
            (poly([(0, 13), (10, 22), (2, 22)]), False)
        ]
    elif idx == 15:  # res_timber (three rects 36x8 at y=-12, 0, 12)
        return [
            (rect(0, -12, 36, 8), False),
            (rect(0, 0, 36, 8), False),
            (rect(0, 12, 36, 8), False),
        ]
    elif idx == 16:  # res_metal
        return ring(regular_polygon(0, 0, 16, 6, 0), circle(0, 0, 6))
    elif idx == 17:  # res_wealth (star tip up)
        return [(star(0, 0, 18, 8, 5), False)]
    elif idx == 18:  # res_knowledge
        return ring(regular_polygon(0, 0, 18, 4, 0), regular_polygon(0, 0, 8, 4, 0))
    elif idx == 19:  # res_oil (teardrop)
        # tip at (0, 20), circle r12 centered at (0, -6) from angle 20 deg around to 160 deg (long way, through 270)
        angles = np.radians(np.linspace(20, -200, 16))
        pts = [(0.0, 20.0)]
        for a in angles:
            pts.append((12.0 * np.cos(a), -6.0 + 12.0 * np.sin(a)))
        return [(poly(pts), False)]
    elif idx == 20:  # res_silicon
        outer_ring = ring(rect(0, 0, 32, 32), rect(0, 0, 14, 14))
        pins = [
            (rect(0, 19, 6, 4), False),
            (rect(0, -19, 6, 4), False),
            (rect(19, 0, 4, 6), False),
            (rect(-19, 0, 4, 6), False),
        ]
        return outer_ring + pins
    elif idx == 21:  # ui_arrow_up
        return [(poly([(0, 16), (14, -8), (-14, -8)]), False)]
    elif idx == 22:  # ui_arrow_down
        return [(poly([(0, -16), (14, 8), (-14, 8)]), False)]
    elif idx == 23:  # ui_check
        return [(poly([(-18, 0), (-12, -6), (-5, -1), (12, 16), (18, 10), (-5, -13)]), False)]
    elif idx == 24:  # ui_cross
        return [(rotate(plus_shape(16, 6), 45), False)]
    elif idx == 25:  # ui_plus
        return [(plus_shape(16, 6), False)]
    elif idx == 26:  # ui_minus
        return [(rect(0, 0, 32, 6), False)]
    elif idx == 27:  # ui_play
        return [(poly([(-10, -14), (-10, 14), (16, 0)]), False)]
    elif idx == 28:  # ui_ring
        return ring(circle(0, 0, 18), circle(0, 0, 12))
    elif idx == 29:  # ui_square_ring
        return ring(rect(0, 0, 36, 36), rect(0, 0, 24, 24))
    elif idx == 30:  # ui_gear
        return ring(star(0, 0, 18, 14, 8), circle(0, 0, 6))
    elif idx == 31:  # ui_dot
        return [(circle(0, 0, 10), False)]
    else:
        raise ValueError(f"Unknown icon index {idx}")
