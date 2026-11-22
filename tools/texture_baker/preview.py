"""preview.py - 2048x1024 RGB PNG preview generator for Dominion 2026 textures."""

import os
import sys
import numpy as np

sys.path.insert(0, os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

from asset_common.pngio import write_png
from terrain_tiles import TERRAIN_LAYERS


def downsample_box(img: np.ndarray, factor: int) -> np.ndarray:
    """Downsample image by integer factor using box averaging."""
    H, W = img.shape[:2]
    new_H = H // factor
    new_W = W // factor
    cropped = img[: new_H * factor, : new_W * factor]
    if img.ndim == 2:
        return cropped.reshape(new_H, factor, new_W, factor).mean(axis=(1, 3)).astype(np.uint8)
    else:
        C = img.shape[2]
        return cropped.reshape(new_H, factor, new_W, factor, C).mean(axis=(1, 3)).astype(np.uint8)


def normal_xy_to_rgb(nxy: np.ndarray) -> np.ndarray:
    """Convert normal xy (uint8 [H, W, 2]) to visual RGB (uint8 [H, W, 3]) with reconstructed z."""
    nx = nxy[..., 0].astype(np.float32) / 255.0 * 2.0 - 1.0
    ny = nxy[..., 1].astype(np.float32) / 255.0 * 2.0 - 1.0
    nz = np.sqrt(np.maximum(0.0, 1.0 - nx * nx - ny * ny))
    rgb_flt = np.stack([nx * 0.5 + 0.5, ny * 0.5 + 0.5, nz * 0.5 + 0.5], axis=-1)
    return np.rint(np.clip(rgb_flt, 0.0, 1.0) * 255.0).astype(np.uint8)


def write_preview(path: str, sources: dict):
    """Generate 2048x1024 RGB preview PNG from source textures."""
    canvas = np.full((1024, 2048, 3), [28, 30, 36], dtype=np.uint8)

    # 1. Left 1024x1024: trim albedo downsampled 2x
    trim_albedo = sources["trim_albedo"]
    canvas[:1024, :1024] = downsample_box(trim_albedo, 2)

    # 2. Top-right area:
    # (a) x 1024..1535, y 0..63: 8 terrain albedo tiles downsampled to 64x64 (8 across, 512 px)
    terrain_albedo = sources["terrain_albedo"]  # [8, 512, 512, 3]
    for i in range(8):
        tile_down = downsample_box(terrain_albedo[i], 8)  # 64x64
        canvas[0:64, 1024 + i * 64 : 1024 + (i + 1) * 64] = tile_down

    # (b) x 1024..1535, y 64..127: terrain normals visualised
    terrain_normal = sources["terrain_normal"]  # [8, 512, 512, 2]
    for i in range(8):
        norm_rgb = normal_xy_to_rgb(terrain_normal[i])
        norm_down = downsample_box(norm_rgb, 8)
        canvas[64:128, 1024 + i * 64 : 1024 + (i + 1) * 64] = norm_down

    # (c) x 1024..1535, y 128..255: MSDF atlas median channel downsampled 2x (512x256)
    msdf_img = sources["msdf_atlas"]  # [512, 1024, 4]
    med_channel = np.median(msdf_img[..., :3], axis=-1).astype(np.uint8)  # [512, 1024]
    med_down = downsample_box(med_channel, 2)  # [256, 512]
    canvas[128:256, 1024:1536] = np.stack([med_down[:128], med_down[:128], med_down[:128]], axis=-1)

    # (d) x 1536..2047, y 0..255: trim_orm downsampled 4x (512x512 cropped to top 256 rows)
    trim_orm = sources["trim_orm"]
    orm_down = downsample_box(trim_orm, 4)  # [512, 512, 3]
    canvas[0:256, 1536:2048] = orm_down[:256]

    # 3. Bottom half:
    # (a) x 1024..1535, y 512..1023: VFX atlas downsampled 4x composited over (30,30,36)
    vfx_atlas = sources["vfx_atlas"]  # [2048, 2048, 4] premultiplied
    vfx_down = downsample_box(vfx_atlas, 4)  # [512, 512, 4]
    vfx_rgb = vfx_down[..., :3].astype(np.float32) / 255.0
    vfx_a = vfx_down[..., 3].astype(np.float32) / 255.0
    bg = np.array([30.0, 30.0, 36.0], dtype=np.float32) / 255.0
    comp = vfx_rgb + bg[None, None, :] * (1.0 - vfx_a[..., None])
    canvas[512:1024, 1024:1536] = np.rint(np.clip(comp, 0.0, 1.0) * 255.0).astype(np.uint8)

    # (b) x 1536..2047, y 512..1023: trim normal visualised downsampled 4x
    trim_norm = sources["trim_normal"]
    trim_norm_rgb = normal_xy_to_rgb(trim_norm)
    canvas[512:1024, 1536:2048] = downsample_box(trim_norm_rgb, 4)

    os.makedirs(os.path.dirname(os.path.abspath(path)), exist_ok=True)
    write_png(path, canvas)
