"""vat.py - Vertex-Animation Texture (VAT) baking and fp16 packing."""

import numpy as np

import params
from rig import Rig


def build_model_rows(rig: Rig) -> np.ndarray:
    """Generate unquantized affine matrix rows [44, 16, 3, 4] for a single model."""
    rows = np.zeros((params.FRAMES_PER_MODEL, params.MAX_PARTS, 3, 4), dtype=np.float32)

    for clip_idx, (clip_name, frame_count, _) in enumerate(params.CLIPS):
        start = params.CLIP_START[clip_idx]
        for f in range(frame_count):
            row_idx = start + f
            mats = rig.world_matrices(clip_idx, f)  # [16, 4, 4]
            rows[row_idx] = mats[:, :3, :].astype(np.float32)

    return rows


def pack_half(rows: np.ndarray) -> np.ndarray:
    """Pack [44, 16, 3, 4] matrix rows into RGBA16F texture array [44, 48, 4]."""
    vat_data = np.zeros((params.FRAMES_PER_MODEL, params.VAT_COLS, 4), dtype=np.float16)

    for p in range(params.MAX_PARTS):
        for r in range(3):
            col = p * 3 + r
            vat_data[:, col, :] = rows[:, p, r, :].astype(np.float16)

    return vat_data


def build_vat(rigs: list[Rig]) -> np.ndarray:
    """Bake full VAT texture array [44 * len(rigs), 48, 4] for all rigs in float16."""
    all_models = []
    for r in rigs:
        rows = build_model_rows(r)
        all_models.append(pack_half(rows))

    if not all_models:
        return np.zeros((0, params.VAT_COLS, 4), dtype=np.float16)

    return np.concatenate(all_models, axis=0)
