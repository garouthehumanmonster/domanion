"""preview.py - Software orthographic contact sheet rasterizer for 3D model previews."""

import math
import os
import sys
import numpy as np

sys.path.insert(0, os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
from asset_common.pngio import write_png
from meshbuilder import assemble
from model import Model
import trims

SHEET_COLS = 12
SHEET_ROWS = 6
TILE_SIZE = 160
IMG_W = SHEET_COLS * TILE_SIZE  # 1920
IMG_H = SHEET_ROWS * TILE_SIZE  # 960

BG_COLOR = np.array([28, 30, 36], dtype=np.uint8)
BORDER_COLOR = np.array([18, 20, 24], dtype=np.uint8)
TEAM_COLOR = np.array([200, 40, 40], dtype=np.float32)

YAW_RAD = math.radians(35.0)
PITCH_RAD = math.radians(30.0)

# Camera rotation matrix: Rx(pitch) @ Ry(yaw)
Ry = np.array([
    [math.cos(YAW_RAD), 0.0, math.sin(YAW_RAD)],
    [0.0, 1.0, 0.0],
    [-math.sin(YAW_RAD), 0.0, math.cos(YAW_RAD)],
], dtype=np.float64)

Rx = np.array([
    [1.0, 0.0, 0.0],
    [0.0, math.cos(PITCH_RAD), -math.sin(PITCH_RAD)],
    [0.0, math.sin(PITCH_RAD), math.cos(PITCH_RAD)],
], dtype=np.float64)

ROT_CAM = Rx @ Ry

# Light direction in world space
LIGHT_DIR = np.array([-0.4, 0.8, 0.45], dtype=np.float64)
LIGHT_DIR /= np.linalg.norm(LIGHT_DIR)


def _render_tile(
    model: Model,
    img: np.ndarray,
    zbuf: np.ndarray,
    tile_col: int,
    tile_row: int,
) -> None:
    mesh = assemble(model.parts, lod=0)
    if len(mesh.tris) == 0:
        return

    # Model center
    bmin = np.min(mesh.pos, axis=0)
    bmax = np.max(mesh.pos, axis=0)
    center = (bmin + bmax) * 0.5

    # Center vertices and transform to camera space
    v_centered = mesh.pos.astype(np.float64) - center
    v_cam = (ROT_CAM @ v_centered.T).T

    # World space geometric normals
    v0_w = mesh.pos[mesh.tris[:, 0]].astype(np.float64)
    v1_w = mesh.pos[mesh.tris[:, 1]].astype(np.float64)
    v2_w = mesh.pos[mesh.tris[:, 2]].astype(np.float64)
    gn_w = np.cross(v1_w - v0_w, v2_w - v0_w)
    gn_norm = np.linalg.norm(gn_w, axis=1, keepdims=True)
    gn_norm = np.where(gn_norm == 0.0, 1.0, gn_norm)
    gn_w /= gn_norm

    # Transform normals to camera space for back-face culling
    gn_cam = (ROT_CAM @ gn_w.T).T
    front_facing = gn_cam[:, 2] > 0.0

    # Scale to fit into 144x144 px centered in 160x160 tile
    x_cam = v_cam[:, 0]
    y_cam = v_cam[:, 1]
    extent_x = np.max(x_cam) - np.min(x_cam)
    extent_y = np.max(y_cam) - np.min(y_cam)
    max_extent = max(float(extent_x), float(extent_y), 1e-4)
    scale = 140.0 / max_extent

    mid_x = (np.max(x_cam) + np.min(x_cam)) * 0.5
    mid_y = (np.max(y_cam) + np.min(y_cam)) * 0.5

    tile_cx = tile_col * TILE_SIZE + 80.0
    tile_cy = tile_row * TILE_SIZE + 80.0

    # Screen pixel coordinates (y is inverted)
    sx = tile_cx + (x_cam - mid_x) * scale
    sy = tile_cy - (y_cam - mid_y) * scale
    sz = v_cam[:, 2]

    # Pre-calculate triangle colors
    # diffuse = max(0, dot(gn_w, LIGHT_DIR))
    diffuse = np.maximum(0.0, np.sum(gn_w * LIGHT_DIR, axis=1))
    shade = 0.35 + 0.65 * diffuse  # [m]

    trim_ids = mesh.trim[mesh.tris[:, 0]]
    flags = mesh.flags[mesh.tris[:, 0]]
    is_team = (flags & 1) != 0

    tile_x0 = tile_col * TILE_SIZE
    tile_x1 = tile_x0 + TILE_SIZE
    tile_y0 = tile_row * TILE_SIZE
    tile_y1 = tile_y0 + TILE_SIZE

    # Rasterize visible triangles
    for tri_idx in np.where(front_facing)[0]:
        i0, i1, i2 = mesh.tris[tri_idx]
        p0x, p0y, p0z = sx[i0], sy[i0], sz[i0]
        p1x, p1y, p1z = sx[i1], sy[i1], sz[i1]
        p2x, p2y, p2z = sx[i2], sy[i2], sz[i2]

        min_x = max(tile_x0 + 1, int(np.floor(min(p0x, p1x, p2x))))
        max_x = min(tile_x1 - 2, int(np.ceil(max(p0x, p1x, p2x))))
        min_y = max(tile_y0 + 1, int(np.floor(min(p0y, p1y, p2y))))
        max_y = min(tile_y1 - 2, int(np.ceil(max(p0y, p1y, p2y))))

        if min_x > max_x or min_y > max_y:
            continue

        denom = (p1y - p2y) * (p0x - p2x) + (p2x - p1x) * (p0y - p2y)
        if abs(denom) < 1e-9:
            continue

        xs = np.arange(min_x, max_x + 1)
        ys = np.arange(min_y, max_y + 1)
        X, Y = np.meshgrid(xs, ys)

        u = ((p1y - p2y) * (X - p2x) + (p2x - p1x) * (Y - p2y)) / denom
        v = ((p2y - p0y) * (X - p2x) + (p0x - p2x) * (Y - p2y)) / denom
        w = 1.0 - u - v

        inside = (u >= 0.0) & (v >= 0.0) & (w >= 0.0)
        if not np.any(inside):
            continue

        z_interp = u * p0z + v * p1z + w * p2z

        sub_z = zbuf[min_y:max_y + 1, min_x:max_x + 1]
        update_mask = inside & (z_interp > sub_z)
        if not np.any(update_mask):
            continue

        sub_z[update_mask] = z_interp[update_mask]

        # Calculate pixel RGB
        t_id = int(trim_ids[tri_idx])
        base_rgb = np.array(trims.PREVIEW_RGB[t_id], dtype=np.float32)
        s_val = float(shade[tri_idx])
        color = base_rgb * s_val

        if is_team[tri_idx]:
            color = 0.5 * color + 0.5 * TEAM_COLOR

        color_uint8 = np.clip(color, 0.0, 255.0).astype(np.uint8)
        img[min_y:max_y + 1, min_x:max_x + 1][update_mask] = color_uint8


def write_preview(path_png: str, models: list[Model]) -> None:
    """Render a 1920x960 orthographic contact sheet of all models."""
    img = np.zeros((IMG_H, IMG_W, 3), dtype=np.uint8)
    img[:, :] = BG_COLOR
    zbuf = np.full((IMG_H, IMG_W), -1e9, dtype=np.float32)

    # Draw grid borders
    for c in range(SHEET_COLS):
        for r in range(SHEET_ROWS):
            x0 = c * TILE_SIZE
            x1 = x0 + TILE_SIZE
            y0 = r * TILE_SIZE
            y1 = y0 + TILE_SIZE
            img[y0, x0:x1] = BORDER_COLOR
            img[y1 - 1, x0:x1] = BORDER_COLOR
            img[y0:y1, x0] = BORDER_COLOR
            img[y0:y1, x1 - 1] = BORDER_COLOR

    for idx, m in enumerate(models):
        if idx >= SHEET_COLS * SHEET_ROWS:
            break
        c = idx % SHEET_COLS
        r = idx // SHEET_COLS
        _render_tile(m, img, zbuf, c, r)

    os.makedirs(os.path.dirname(os.path.abspath(path_png)), exist_ok=True)
    write_png(path_png, img)
