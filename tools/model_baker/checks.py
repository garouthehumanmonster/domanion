"""checks.py - The 7 Model Validation Checks (M1 to M7)."""

import math
import re
import numpy as np

import params
from meshbuilder import assemble
from lod import lod_masks
from model import Model
import vat


def _signed_vol(v: np.ndarray, t: np.ndarray) -> float:
    v0 = v[t[:, 0]]
    v1 = v[t[:, 1]]
    v2 = v[t[:, 2]]
    return float(np.sum(v0 * np.cross(v1, v2)) / 6.0)


def _check_closed(pos: np.ndarray, tris: np.ndarray) -> tuple[bool, str]:
    key_map = {}
    remap = []
    for p in pos:
        key = (round(float(p[0]), 5), round(float(p[1]), 5), round(float(p[2]), 5))
        if key not in key_map:
            key_map[key] = len(key_map)
        remap.append(key_map[key])
    remap = np.array(remap)
    w_tris = remap[tris]
    edges = {}
    for tri in w_tris:
        for i in range(3):
            u, v = tri[i], tri[(i + 1) % 3]
            if u == v:
                return False, "Degenerate triangle"
            edge = tuple(sorted((u, v)))
            edges[edge] = edges.get(edge, 0) + 1
    for edge, count in edges.items():
        if count != 2:
            return False, f"Edge {edge} shared by {count} triangles (expected 2)"
    return True, "OK"


def check_m1_structure(models: list[Model], expected_count: int | None = None) -> tuple[bool, list[str]]:
    """M1 structure: counts, unique ids, lowercase [a-z0-9_], part limits, rig validation."""
    msgs = []
    if expected_count is not None and len(models) != expected_count:
        msgs.append(f"Model count {len(models)} != expected {expected_count}")

    id_set = set()
    id_pattern = re.compile(r"^[a-z0-9_]+$")

    for m in models:
        if m.id in id_set:
            msgs.append(f"Duplicate model id: {m.id}")
        id_set.add(m.id)

        if not id_pattern.match(m.id):
            msgs.append(f"Model id {m.id} does not match ^[a-z0-9_]+$")

        part_indices = {p.part for p in m.parts}
        part_count = len(part_indices)
        if part_count > params.MAX_PARTS:
            msgs.append(f"{m.id}: part count {part_count} > {params.MAX_PARTS}")
        if part_indices != set(range(part_count)):
            msgs.append(f"{m.id}: part indices not contiguous from 0: {part_indices}")

        try:
            m.rig.validate()
        except Exception as e:
            msgs.append(f"{m.id}: Rig.validate() failed: {e}")

    return len(msgs) == 0, msgs


def check_m2_geometry(models: list[Model]) -> tuple[bool, list[str]]:
    """M2 geometry (LOD0): finite, unit normals, per-part signed vol > 0 & closed, min tri area."""
    msgs = []
    for m in models:
        mesh = assemble(m.parts, lod=0)
        if not (np.isfinite(mesh.pos).all() and np.isfinite(mesh.nrm).all() and np.isfinite(mesh.uv_sheet).all()):
            msgs.append(f"{m.id}: Non-finite positions, normals, or UVs")
            continue

        norms = np.linalg.norm(mesh.nrm, axis=1)
        if np.max(np.abs(norms - 1.0)) > 1e-3:
            msgs.append(f"{m.id}: Non-unit normal found (max dev {np.max(np.abs(norms - 1.0))})")

        part_indices = sorted(list({p.part for p in m.parts}))
        part_count = len(part_indices)

        for p_idx in part_indices:
            # Triangles where vertex 0 belongs to this part
            tri_mask = (mesh.part[mesh.tris[:, 0]] == p_idx)
            part_tris = mesh.tris[tri_mask]
            if len(part_tris) == 0:
                msgs.append(f"{m.id} part {p_idx}: 0 triangles")
                continue

            # Vertex subset for this part
            used_v_indices = np.unique(part_tris)
            remap = {orig: new for new, orig in enumerate(used_v_indices)}
            local_pos = mesh.pos[used_v_indices]
            local_tris = np.array([[remap[idx] for idx in tri] for tri in part_tris], dtype=np.int32)

            vol = _signed_vol(local_pos, local_tris)
            if vol <= 0.0:
                msgs.append(f"{m.id} part {p_idx}: signed volume <= 0 ({vol:.6f})")

            ok_closed, err_msg = _check_closed(local_pos, local_tris)
            if not ok_closed:
                msgs.append(f"{m.id} part {p_idx}: not closed: {err_msg}")

            v0 = local_pos[local_tris[:, 0]]
            v1 = local_pos[local_tris[:, 1]]
            v2 = local_pos[local_tris[:, 2]]
            areas = 0.5 * np.linalg.norm(np.cross(v1 - v0, v2 - v0), axis=1)
            min_area = float(np.min(areas))
            if min_area <= 1e-10:
                msgs.append(f"{m.id} part {p_idx}: min tri area {min_area} <= 1e-10")

        if np.any(mesh.part >= part_count):
            msgs.append(f"{m.id}: vertex part index >= part_count")

    return len(msgs) == 0, msgs


def check_m3_size_placement(models: list[Model]) -> tuple[bool, list[str]]:
    """M3 size and placement: largest dimension, ground/naval y extents, aspect ratios."""
    msgs = []
    for m in models:
        mesh = assemble(m.parts, lod=0)
        bmin = np.min(mesh.pos, axis=0)
        bmax = np.max(mesh.pos, axis=0)
        extents = bmax - bmin
        largest_dim = float(np.max(extents))

        # Largest AABB dimension within +-25%
        lo = 0.75 * m.target_dim_m
        hi = 1.25 * m.target_dim_m
        if not (lo <= largest_dim <= hi):
            msgs.append(f"{m.id}: largest dim {largest_dim:.3f}m not in [{lo:.3f}, {hi:.3f}] for target {m.target_dim_m}m")

        # Ground categories: min y in [-0.05, 0.10]
        if m.category in (params.CAT_INFANTRY, params.CAT_VEHICLE, params.CAT_BUILDING, params.CAT_PROP, params.CAT_RESOURCE):
            if not (-0.05 <= bmin[1] <= 0.10):
                msgs.append(f"{m.id} (cat {m.category}): ground min y {bmin[1]:.4f} not in [-0.05, 0.10]")

        # Naval: min y in [-14, -0.3] and max y > 0.5
        if m.category == params.CAT_NAVAL:
            if not (-14.0 <= bmin[1] <= -0.30):
                msgs.append(f"{m.id} (naval): min y {bmin[1]:.4f} not in [-14, -0.3]")
            if bmax[1] <= 0.5:
                msgs.append(f"{m.id} (naval): max y {bmax[1]:.4f} <= 0.5")

        # Vehicle, Air, Naval, Projectile: X extent >= 0.9 * Z extent
        if m.category in (params.CAT_VEHICLE, params.CAT_AIR, params.CAT_NAVAL, params.CAT_PROJECTILE):
            x_ext = extents[0]
            z_ext = extents[2]
            if x_ext < 0.9 * z_ext:
                msgs.append(f"{m.id}: X extent {x_ext:.3f} < 0.9 * Z extent {z_ext:.3f}")

    return len(msgs) == 0, msgs


def check_m4_budgets(models: list[Model]) -> tuple[bool, list[str]]:
    """M4 budgets: triangle ranges per category, LOD ratios, max vertex counts."""
    msgs = []
    for m in models:
        tris_per_lod = []
        for lod in range(3):
            mask = lod_masks(m.parts, lod)
            mesh = assemble(m.parts, lod=lod, mask=mask)
            tri_count = len(mesh.tris)
            vtx_count = len(mesh.pos)
            tris_per_lod.append(tri_count)

            if tri_count < 1:
                msgs.append(f"{m.id} LOD{lod}: 0 triangles")
            if vtx_count > params.MAX_VERTICES_PER_LOD:
                msgs.append(f"{m.id} LOD{lod}: vertex count {vtx_count} > {params.MAX_VERTICES_PER_LOD}")

        lod0_tris, lod1_tris, lod2_tris = tris_per_lod

        # Check category budget on LOD0
        tri_min, tri_max = params.CATEGORY_TRIS_RANGE[m.category]
        if not (tri_min <= lod0_tris <= tri_max):
            msgs.append(f"{m.id} LOD0 tris {lod0_tris} not in budget [{tri_min}, {tri_max}] for cat {m.category}")

        # LOD monotonicity
        if lod1_tris > lod0_tris:
            msgs.append(f"{m.id}: LOD1 tris {lod1_tris} > LOD0 {lod0_tris}")
        if lod2_tris > lod1_tris:
            msgs.append(f"{m.id}: LOD2 tris {lod2_tris} > LOD1 {lod1_tris}")

        # Ratio rule when LOD0 > 150
        if lod0_tris > params.LOD_RATIO_THRESHOLD_TRIS:
            max_lod1 = int(params.LOD_RATIO_LOD1_MAX * lod0_tris)
            max_lod2 = int(params.LOD_RATIO_LOD2_MAX * lod0_tris)
            if lod1_tris > max_lod1:
                msgs.append(f"{m.id}: LOD1 tris {lod1_tris} > 0.85*LOD0 ({max_lod1})")
            if lod2_tris > max_lod2:
                msgs.append(f"{m.id}: LOD2 tris {lod2_tris} > 0.60*LOD0 ({max_lod2})")

    return len(msgs) == 0, msgs


def check_m5_animation(models: list[Model]) -> tuple[bool, list[str]]:
    """M5 animation: frame 0 identity, finite matrices, rotation determinant == 1, idle motion."""
    msgs = []
    for m in models:
        for clip_idx in range(4):
            clip_name, frame_count, _ = params.CLIPS[clip_idx]
            # Frame 0 of every clip identity for all parts
            mats0 = m.rig.world_matrices(clip_idx, 0)
            for p in range(params.MAX_PARTS):
                if not np.allclose(mats0[p], np.eye(4), atol=1e-9):
                    msgs.append(f"{m.id}: Clip {clip_name} frame 0 part {p} not identity")

            # Check all frames finite and rotation determinant
            for f in range(frame_count):
                mats = m.rig.world_matrices(clip_idx, f)
                if not np.isfinite(mats).all():
                    msgs.append(f"{m.id}: Non-finite matrices in clip {clip_name} frame {f}")
                for p in range(params.MAX_PARTS):
                    rot_sub = mats[p, :3, :3]
                    det = float(np.linalg.det(rot_sub))
                    if abs(det - 1.0) > 1e-6:
                        msgs.append(f"{m.id}: Det(rot) {det:.6f} != 1 in {clip_name} f{f} part {p}")

    return len(msgs) == 0, msgs


def check_m6_hardpoints_team(models: list[Model]) -> tuple[bool, list[str]]:
    """M6 hardpoints and team colour: bounds, required hardpoints, team colour area share."""
    msgs = []
    for m in models:
        mesh = assemble(m.parts, lod=0)
        bmin = np.min(mesh.pos, axis=0) - 1.0
        bmax = np.max(mesh.pos, axis=0) + 1.0
        part_count = len({p.part for p in m.parts})

        hp_kinds = [hp.kind for hp in m.hardpoints]

        for hp in m.hardpoints:
            if hp.part >= part_count:
                msgs.append(f"{m.id}: hardpoint part {hp.part} >= {part_count}")
            pos = np.array(hp.pos, dtype=np.float32)
            if not (np.all(pos >= bmin) and np.all(pos <= bmax)):
                msgs.append(f"{m.id}: hardpoint pos {hp.pos} out of expanded AABB")

        # Specific required hardpoint checks
        # Infantry: Muzzle except worker & drone_op
        if m.id.startswith("inf_") and m.id not in ("inf_worker", "inf_drone_op"):
            if params.HP_MUZZLE not in hp_kinds:
                msgs.append(f"{m.id}: missing required Muzzle hardpoint")

        # Vehicles: Muzzle except ew, truck, ugv
        if m.category == params.CAT_VEHICLE and m.id not in ("veh_ew", "veh_truck", "veh_ugv"):
            if params.HP_MUZZLE not in hp_kinds:
                msgs.append(f"{m.id}: missing required Muzzle hardpoint")
        if m.id == "veh_sam":
            if params.HP_LASER not in hp_kinds:
                msgs.append("veh_sam: missing required Laser hardpoint")

        # Air: Muzzle except fpv
        if m.category == params.CAT_AIR and m.id != "air_fpv":
            if params.HP_MUZZLE not in hp_kinds:
                msgs.append(f"{m.id}: missing required Muzzle hardpoint")

        # Naval: all have Muzzle
        if m.category == params.CAT_NAVAL:
            if params.HP_MUZZLE not in hp_kinds:
                msgs.append(f"{m.id}: missing required Muzzle hardpoint")

        # Buildings: bld_tower and bld_sam_site get Muzzle
        if m.id in ("bld_tower", "bld_sam_site"):
            if params.HP_MUZZLE not in hp_kinds:
                msgs.append(f"{m.id}: missing required Muzzle hardpoint")

        # Production buildings: Hatch at (len_x/2, 0, 0)
        if m.id in ("bld_city_center_a", "bld_city_center_b", "bld_city_center_c", "bld_barracks", "bld_factory", "bld_airfield", "bld_dock"):
            if params.HP_HATCH not in hp_kinds:
                msgs.append(f"{m.id}: missing required Hatch hardpoint")

        # Team colour share
        # Categories 0-4: in [TEAM_AREA_MIN, TEAM_AREA_MAX]
        # Categories 5-7: 0
        v0 = mesh.pos[mesh.tris[:, 0]]
        v1 = mesh.pos[mesh.tris[:, 1]]
        v2 = mesh.pos[mesh.tris[:, 2]]
        tri_areas = 0.5 * np.linalg.norm(np.cross(v1 - v0, v2 - v0), axis=1)
        total_area = float(np.sum(tri_areas))

        # Triangle is team-coloured if its vertices carry FLAG_TEAM_COLOR
        team_tri_mask = (mesh.flags[mesh.tris[:, 0]] & params.FLAG_TEAM_COLOR) != 0
        team_area = float(np.sum(tri_areas[team_tri_mask]))
        team_share = (team_area / total_area) if total_area > 0 else 0.0

        if m.category in (0, 1, 2, 3, 4):
            if not (params.TEAM_AREA_MIN <= team_share <= params.TEAM_AREA_MAX):
                msgs.append(f"{m.id} (cat {m.category}): team colour share {team_share:.4f} not in [{params.TEAM_AREA_MIN}, {params.TEAM_AREA_MAX}]")
        else:
            if np.any((mesh.flags & params.FLAG_TEAM_COLOR) != 0):
                msgs.append(f"{m.id} (cat {m.category}): static prop/res/proj has team-coloured vertices")

    return len(msgs) == 0, msgs


def check_m7_determinism(models: list[Model]) -> tuple[bool, list[str]]:
    """M7 determinism: assembling twice gives byte-identical mesh and VAT data."""
    msgs = []
    for m in models:
        # First build
        mesh_0_a = assemble(m.parts, lod=0)
        vat_a = vat.build_model_rows(m.rig)

        # Second build
        mesh_0_b = assemble(m.parts, lod=0)
        vat_b = vat.build_model_rows(m.rig)

        if not np.array_equal(mesh_0_a.pos, mesh_0_b.pos):
            msgs.append(f"{m.id}: non-deterministic mesh positions")
        if not np.array_equal(mesh_0_a.tris, mesh_0_b.tris):
            msgs.append(f"{m.id}: non-deterministic mesh triangles")
        if not np.array_equal(vat_a, vat_b):
            msgs.append(f"{m.id}: non-deterministic VAT rows")

    return len(msgs) == 0, msgs
