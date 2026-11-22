"""meshbuilder.py - Transform, place, and assemble Parts into composite Meshes."""

from dataclasses import dataclass, field
import math
from typing import Callable
import numpy as np

import params
from prims import Piece
import trims


def make_rotation_matrix(rot_deg: tuple[float, float, float]) -> np.ndarray:
    """Compute R = Ry(ry) @ Rx(rx) @ Rz(rz) for column vectors."""
    rx, ry, rz = [math.radians(a) for a in rot_deg]
    Rz = np.array([
        [math.cos(rz), -math.sin(rz), 0.0],
        [math.sin(rz),  math.cos(rz), 0.0],
        [0.0,           0.0,          1.0],
    ], dtype=np.float64)
    Rx = np.array([
        [1.0, 0.0,           0.0],
        [0.0, math.cos(rx), -math.sin(rx)],
        [0.0, math.sin(rx),  math.cos(rx)],
    ], dtype=np.float64)
    Ry = np.array([
        [ math.cos(ry), 0.0, math.sin(ry)],
        [ 0.0,          1.0, 0.0],
        [-math.sin(ry), 0.0, math.cos(ry)],
    ], dtype=np.float64)
    return (Ry @ Rx @ Rz).astype(np.float32)


def place(
    piece: Piece,
    pos: tuple[float, float, float] | np.ndarray,
    rot_deg: tuple[float, float, float] = (0.0, 0.0, 0.0),
    mirror_z: bool = False,
) -> Piece:
    """Rotate and translate a Piece, with optional post-transform Z-mirroring."""
    R = make_rotation_matrix(rot_deg)
    pos_vec = np.asarray(pos, dtype=np.float32).reshape(1, 3)

    p_trans = (piece.pos @ R.T) + pos_vec
    n_trans = piece.nrm @ R.T
    tris = piece.tris.copy()

    if mirror_z:
        p_trans = p_trans.copy()
        n_trans = n_trans.copy()
        p_trans[:, 2] = -p_trans[:, 2]
        n_trans[:, 2] = -n_trans[:, 2]
        tris = tris[:, [0, 2, 1]].copy()

    return Piece(p_trans, n_trans, piece.uv.copy(), tris)


@dataclass
class Part:
    part: int
    make: Callable[[int], Piece]
    pos: tuple[float, float, float] = (0.0, 0.0, 0.0)
    rot_deg: tuple[float, float, float] = (0.0, 0.0, 0.0)
    mirror_z: bool = False
    trim: str = "steel_plain"
    team: bool = False
    emissive: bool = False
    scroll: bool = False
    keep: bool = False
    name: str = ""


@dataclass
class Mesh:
    pos: np.ndarray       # float32 [N, 3]
    nrm: np.ndarray       # float32 [N, 3]
    uv_sheet: np.ndarray  # float32 [N, 2]
    trim: np.ndarray      # uint8 [N]
    flags: np.ndarray     # uint8 [N]
    part: np.ndarray      # uint16 [N]
    tris: np.ndarray      # uint32 [M, 3]


def assemble(parts: list[Part], lod: int, mask: list[bool] | None = None) -> Mesh:
    """Assemble a list of Parts into a unified single-mesh structure."""
    all_pos = []
    all_nrm = []
    all_uv = []
    all_trim = []
    all_flags = []
    all_part = []
    all_tris = []
    v_offset = 0

    for i, p in enumerate(parts):
        if mask is not None and not mask[i]:
            continue

        pieces_to_emit = []
        base_piece = p.make(lod)
        pieces_to_emit.append(place(base_piece, p.pos, p.rot_deg, mirror_z=False))
        if p.mirror_z:
            pieces_to_emit.append(place(base_piece, p.pos, p.rot_deg, mirror_z=True))

        trim_idx = trims.trim_id(p.trim)
        u0, v0, u1, v1 = trims.trim_rect_uv(trim_idx)

        flags_val = 0
        if p.team:
            flags_val |= params.FLAG_TEAM_COLOR
        if p.emissive:
            flags_val |= params.FLAG_EMISSIVE
        if p.scroll:
            flags_val |= params.FLAG_SCROLL_UV

        for pc in pieces_to_emit:
            n_verts = len(pc.pos)
            if n_verts == 0:
                continue

            # Convert local UV to sheet UV
            uv_sheet = np.empty_like(pc.uv)
            uv_sheet[:, 0] = u0 + pc.uv[:, 0] * (u1 - u0)
            uv_sheet[:, 1] = v0 + pc.uv[:, 1] * (v1 - v0)

            all_pos.append(pc.pos)
            all_nrm.append(pc.nrm)
            all_uv.append(uv_sheet)
            all_trim.append(np.full(n_verts, trim_idx, dtype=np.uint8))
            all_flags.append(np.full(n_verts, flags_val, dtype=np.uint8))
            all_part.append(np.full(n_verts, p.part, dtype=np.uint16))
            all_tris.append(pc.tris.astype(np.uint32) + v_offset)

            v_offset += n_verts

    if not all_pos:
        return Mesh(
            np.zeros((0, 3), dtype=np.float32),
            np.zeros((0, 3), dtype=np.float32),
            np.zeros((0, 2), dtype=np.float32),
            np.zeros(0, dtype=np.uint8),
            np.zeros(0, dtype=np.uint8),
            np.zeros(0, dtype=np.uint16),
            np.zeros((0, 3), dtype=np.uint32),
        )

    return Mesh(
        pos=np.vstack(all_pos),
        nrm=np.vstack(all_nrm),
        uv_sheet=np.vstack(all_uv),
        trim=np.concatenate(all_trim),
        flags=np.concatenate(all_flags),
        part=np.concatenate(all_part),
        tris=np.vstack(all_tris),
    )
