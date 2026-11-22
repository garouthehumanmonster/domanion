"""bake_models.py - CLI tool to bake all models to .mdbin, manifest JSON, and preview PNG."""

import argparse
import json
import math
import os
import sys
import time
import numpy as np

sys.path.insert(0, os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

from asset_common.hashing import fnv1a64
import catalogue
import checks
from lod import lod_masks
from meshbuilder import assemble
import mformat
import octa
import params
import preview
import vat


def bake(out_dir: str = "assets/compiled/models", no_preview: bool = False) -> None:
    t_start = time.perf_counter()
    os.makedirs(out_dir, exist_ok=True)

    # 1. Build catalogue
    t0 = time.perf_counter()
    models = catalogue.build_catalogue()
    t_cat = time.perf_counter() - t0
    print(f"[stage] catalogue {t_cat:.3f}s (collected {len(models)} models)")

    # 2. Run all 7 model checks
    t0 = time.perf_counter()
    all_checks = [
        ("M1", lambda: checks.check_m1_structure(models, expected_count=len(models))),
        ("M2", lambda: checks.check_m2_geometry(models)),
        ("M3", lambda: checks.check_m3_size_placement(models)),
        ("M4", lambda: checks.check_m4_budgets(models)),
        ("M5", lambda: checks.check_m5_animation(models)),
        ("M6", lambda: checks.check_m6_hardpoints_team(models)),
        ("M7", lambda: checks.check_m7_determinism(models)),
    ]
    failed = False
    for name, fn in all_checks:
        ok, msgs = fn()
        if not ok:
            print(f"FAILED check {name}:")
            for m in msgs:
                print("  ", m)
            failed = True
    if failed:
        print("Model checks failed. Exiting with code 3.")
        sys.exit(3)
    t_checks = time.perf_counter() - t0
    print(f"[stage] checks {t_checks:.3f}s (7/7 passed)")

    # 3. Assemble and quantize meshes
    t0 = time.perf_counter()
    all_vertices = []
    all_indices = []
    model_recs = []
    hardpoint_recs = []
    manifest_entries = {}

    curr_vtx_base = 0
    curr_idx_first = 0
    curr_hp_first = 0

    for m_idx, m in enumerate(models):
        id_hash = fnv1a64(m.id)
        vtx_base = [0, 0, 0]
        idx_first = [0, 0, 0]
        idx_count = [0, 0, 0]
        vtx_count = [0, 0, 0]

        lod_meshes = []
        for lod in range(3):
            mask = lod_masks(m.parts, lod)
            mesh = assemble(m.parts, lod, mask)
            lod_meshes.append(mesh)

        lod0_mesh = lod_meshes[0]
        bmin_m = np.min(lod0_mesh.pos, axis=0) if len(lod0_mesh.pos) > 0 else np.zeros(3)
        bmax_m = np.max(lod0_mesh.pos, axis=0) if len(lod0_mesh.pos) > 0 else np.zeros(3)

        bmin_fx = np.rint(bmin_m * 65536.0).astype(np.int32)
        bmax_fx = np.rint(bmax_m * 65536.0).astype(np.int32)

        # Max XZ radius
        if len(lod0_mesh.pos) > 0:
            rad_xz = np.max(np.sqrt(lod0_mesh.pos[:, 0] ** 2 + lod0_mesh.pos[:, 2] ** 2))
        else:
            rad_xz = 0.0
        radius_xz_fx = int(np.rint(rad_xz * 65536.0))

        # Flags: bit0 team, bit1 animated idle, bit2 scroll uv
        has_team = bool(np.any((lod0_mesh.flags & 1) != 0))
        has_scroll = bool(np.any((lod0_mesh.flags & 4) != 0))

        # Check Idle animation
        idle_animated = False
        for f in range(1, params.CLIPS[0][1]):
            mats = m.rig.world_matrices(0, f)
            diff = np.max(np.abs(mats - np.eye(4)))
            if diff > 1e-3:
                idle_animated = True
                break

        flags_byte = (1 if has_team else 0) | (2 if idle_animated else 0) | (4 if has_scroll else 0)
        part_count = len({p.part for p in m.parts})

        for lod in range(3):
            mesh = lod_meshes[lod]
            n_vtx = len(mesh.pos)
            if n_vtx > 65535:
                raise ValueError(f"{m.id} LOD{lod} vertex count {n_vtx} > 65535")

            vtx_base[lod] = curr_vtx_base
            vtx_count[lod] = n_vtx
            idx_first[lod] = curr_idx_first
            n_idx = len(mesh.tris) * 3
            idx_count[lod] = n_idx

            # Quantize vertices
            if n_vtx > 0:
                pos = mesh.pos.astype(np.float32)
                nrm_snorm = octa.encode(mesh.nrm)
                uv_unorm = octa.unorm16(mesh.uv_sheet)
                trim_id = mesh.trim.astype(np.uint8)
                v_flags = mesh.flags.astype(np.uint8)
                v_part = mesh.part.astype(np.uint16)

                vtx_struct = np.empty(n_vtx, dtype=mformat.VERTEX_DTYPE)
                vtx_struct["pos"] = pos
                vtx_struct["nrm"] = nrm_snorm
                vtx_struct["uv"] = uv_unorm
                vtx_struct["trim_id"] = trim_id
                vtx_struct["flags"] = v_flags
                vtx_struct["part"] = v_part

                all_vertices.append(vtx_struct)

            # Local indices
            if n_idx > 0:
                indices = mesh.tris.flatten().astype(np.uint16)
                all_indices.append(indices)

            curr_vtx_base += n_vtx
            curr_idx_first += n_idx

        # Hardpoints
        hp_first = curr_hp_first
        hp_count = len(m.hardpoints)
        for hp in m.hardpoints:
            hp_struct = np.empty(1, dtype=mformat.HARDPOINT_DTYPE)
            hp_struct["kind"] = hp.kind
            hp_struct["part"] = hp.part
            hp_struct["reserved"] = 0
            hp_struct["pos"] = np.rint(np.array(hp.pos, dtype=np.float64) * 65536.0).astype(np.int32)
            hardpoint_recs.append(hp_struct)
        curr_hp_first += hp_count

        vat_row_base = m_idx * params.FRAMES_PER_MODEL
        tri_count_lod0 = idx_count[0] // 3

        m_rec = np.empty(1, dtype=mformat.MODEL_DTYPE)
        m_rec["id_hash"] = id_hash
        m_rec["bmin"] = bmin_fx
        m_rec["bmax"] = bmax_fx
        m_rec["vtx_base"] = np.array(vtx_base, dtype=np.uint32)
        m_rec["idx_first"] = np.array(idx_first, dtype=np.uint32)
        m_rec["idx_count"] = np.array(idx_count, dtype=np.uint32)
        m_rec["vtx_count"] = np.array(vtx_count, dtype=np.uint16)
        m_rec["part_count"] = part_count
        m_rec["vat_row_base"] = vat_row_base
        m_rec["hp_first"] = hp_first
        m_rec["hp_count"] = hp_count
        m_rec["tri_count_lod0"] = tri_count_lod0
        m_rec["category"] = m.category
        m_rec["flags"] = flags_byte
        m_rec["radius_xz"] = radius_xz_fx
        m_rec["reserved"] = 0
        model_recs.append(m_rec)

        manifest_entries[m.id] = {
            "id_hash": f"{id_hash:016x}",
            "category": m.category,
            "tris": [int(idx_count[l] // 3) for l in range(3)],
            "vertices": [int(vtx_count[l]) for l in range(3)],
            "parts": int(part_count),
            "target_dim_m": float(m.target_dim_m),
            "bbox": {
                "min": [float(bmin_m[0]), float(bmin_m[1]), float(bmin_m[2])],
                "max": [float(bmax_m[0]), float(bmax_m[1]), float(bmax_m[2])],
            },
            "hardpoint_count": int(hp_count),
        }

    t_assemble = time.perf_counter() - t0
    print(f"[stage] assemble {t_assemble:.3f}s (vtx={curr_vtx_base}, idx={curr_idx_first})")

    # 4. Bake VAT
    t0 = time.perf_counter()
    vat_data = vat.build_vat([m.rig for m in models])
    t_vat = time.perf_counter() - t0
    print(f"[stage] vat {t_vat:.3f}s (rows={vat_data.shape[0]})")

    # 5. Write binary .mdbin
    t0 = time.perf_counter()
    all_vertices_arr = np.concatenate(all_vertices) if all_vertices else np.empty(0, dtype=mformat.VERTEX_DTYPE)
    all_indices_arr = np.concatenate(all_indices) if all_indices else np.empty(0, dtype="<u2")
    all_models_arr = np.concatenate(model_recs) if model_recs else np.empty(0, dtype=mformat.MODEL_DTYPE)
    all_hps_arr = np.concatenate(hardpoint_recs) if hardpoint_recs else np.empty(0, dtype=mformat.HARDPOINT_DTYPE)

    out_mdbin = os.path.join(out_dir, "models_2026.mdbin")
    mformat.write_models(out_mdbin, {
        "models": all_models_arr,
        "vertices": all_vertices_arr,
        "indices": all_indices_arr,
        "hardpoints": all_hps_arr,
        "vat": vat_data,
    })
    size_mb = os.path.getsize(out_mdbin) / (1024 * 1024)
    t_write = time.perf_counter() - t0
    print(f"[stage] write_mdbin {t_write:.3f}s ({size_mb:.2f} MB)")

    # 6. Write manifest JSON
    t0 = time.perf_counter()
    out_manifest = os.path.join(out_dir, "models_2026.manifest.json")
    with open(out_manifest, "w", encoding="utf-8") as f:
        json.dump(manifest_entries, f, indent=2, sort_keys=True)
    t_manifest = time.perf_counter() - t0
    print(f"[stage] manifest {t_manifest:.3f}s ({len(manifest_entries)} models)")

    # 7. Write preview PNG
    t_preview = 0.0
    if not no_preview:
        t0 = time.perf_counter()
        out_png = os.path.join(out_dir, "models_2026.preview.png")
        preview.write_preview(out_png, models)
        t_preview = time.perf_counter() - t0
        print(f"[stage] preview {t_preview:.3f}s (saved {out_png})")

    total_time = time.perf_counter() - t_start
    print(f"Bake complete in {total_time:.3f}s -> {out_dir}")


def main():
    parser = argparse.ArgumentParser(description="Model Baker CLI")
    parser.add_argument("--out", default="assets/compiled/models", help="Output directory")
    parser.add_argument("--no-preview", action="store_true", help="Skip preview rendering")
    args = parser.parse_args()

    bake(args.out, args.no_preview)


if __name__ == "__main__":
    main()
