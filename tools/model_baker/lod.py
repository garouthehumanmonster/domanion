"""lod.py - LOD mask computation based on relative part bounding box diagonals."""

import numpy as np

import params
from meshbuilder import Part, place


def lod_masks(parts: list[Part], lod: int) -> list[bool]:
    """Compute per-Part inclusion mask for the requested LOD (0, 1, 2)."""
    if lod == 0 or not parts:
        return [True] * len(parts)

    drop_frac = params.LOD1_DROP_FRAC if lod == 1 else params.LOD2_DROP_FRAC

    # Compute LOD0 bounding boxes for each part and for the whole model
    all_verts = []
    part_diags = []

    for p in parts:
        piece = p.make(0)
        p0 = place(piece, p.pos, p.rot_deg, mirror_z=False)
        pts = [p0.pos]
        if p.mirror_z:
            p_mir = place(piece, p.pos, p.rot_deg, mirror_z=True)
            pts.append(p_mir.pos)

        combined = np.vstack(pts) if pts else np.zeros((0, 3), dtype=np.float32)
        if len(combined) > 0:
            p0_min = np.min(p0.pos, axis=0)
            p0_max = np.max(p0.pos, axis=0)
            diag = float(np.linalg.norm(p0_max - p0_min))
            all_verts.append(combined)
        else:
            diag = 0.0
        part_diags.append(diag)

    if not all_verts:
        return [True] * len(parts)

    full_mesh_pos = np.vstack(all_verts)
    m_bmin = np.min(full_mesh_pos, axis=0)
    m_bmax = np.max(full_mesh_pos, axis=0)
    model_diag = float(np.linalg.norm(m_bmax - m_bmin))

    if model_diag <= 1e-6:
        return [True] * len(parts)

    mask = []
    for i, p in enumerate(parts):
        keep_part = (p.part == 0) or p.keep or (part_diags[i] >= drop_frac * model_diag)
        mask.append(keep_part)

    return mask
