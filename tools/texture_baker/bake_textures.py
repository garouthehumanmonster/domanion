"""bake_textures.py - CLI pipeline and asset baker for Dominion 2026 textures."""

import argparse
import json
import os
import sys
import time
import numpy as np

sys.path.insert(0, os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

from asset_common.hashing import fnv1a64
from asset_common.mips import mip_chain
from asset_common.bc import encode_bc1, encode_bc3, encode_bc4, encode_bc5
from asset_common.texformat import TexSpec, write_tex26

from trim_table import TRIM_SPECS
from trim_paint import assemble_sheet
from terrain_tiles import build_terrain, TERRAIN_LAYERS
from vfx import assemble_vfx_atlas, frame_origin, VFX_INFO
from msdf_atlas import build_msdf_atlas
from preview import write_preview


def bake_all(out_dir: str, preview: bool = True, keep_sources: bool = False) -> dict:
    """Run all texture baking stages and write textures_2026.tex26, manifest, and preview."""
    os.makedirs(out_dir, exist_ok=True)
    sources = {}

    # Stage 1: trims
    t0 = time.time()
    trim_sheet = assemble_sheet()
    sources["trim_albedo"] = trim_sheet["albedo"]
    sources["trim_normal"] = trim_sheet["normal_xy"]
    sources["trim_orm"] = trim_sheet["orm"]
    sources["trim_emissive"] = trim_sheet["emissive"]
    print(f"[stage] trims {time.time() - t0:.2f}s")

    # Stage 2: terrain
    t0 = time.time()
    terr_alb, terr_norm, terr_rh = build_terrain()
    sources["terrain_albedo"] = terr_alb
    sources["terrain_normal"] = terr_norm
    sources["terrain_rh"] = terr_rh
    print(f"[stage] terrain {time.time() - t0:.2f}s")

    # Stage 3: vfx
    t0 = time.time()
    sources["vfx_atlas"] = assemble_vfx_atlas()
    print(f"[stage] vfx {time.time() - t0:.2f}s")

    # Stage 4: msdf
    t0 = time.time()
    msdf_img, glyph_bytes = build_msdf_atlas()
    sources["msdf_atlas"] = msdf_img
    print(f"[stage] msdf {time.time() - t0:.2f}s")

    # Stage 5: encode + mips
    t0 = time.time()
    specs = []

    # 0: trim_albedo (BC1, 2048x2048, 1 layer, 12 mips, flags=1)
    chain = mip_chain(sources["trim_albedo"], "srgb")
    data_trim_alb = [[encode_bc1(lvl)] for lvl in chain]
    specs.append(TexSpec("trim_albedo", 0, 2048, 2048, 1, 12, 1, data_trim_alb))

    # 1: trim_normal (BC5, 2048x2048, 1 layer, 12 mips, flags=0)
    chain = mip_chain(sources["trim_normal"], "normal2")
    data_trim_norm = [[encode_bc5(lvl)] for lvl in chain]
    specs.append(TexSpec("trim_normal", 3, 2048, 2048, 1, 12, 0, data_trim_norm))

    # 2: trim_orm (BC1, 2048x2048, 1 layer, 12 mips, flags=0)
    chain = mip_chain(sources["trim_orm"], "linear")
    data_trim_orm = [[encode_bc1(lvl)] for lvl in chain]
    specs.append(TexSpec("trim_orm", 0, 2048, 2048, 1, 12, 0, data_trim_orm))

    # 3: trim_emissive (BC4, 2048x2048, 1 layer, 12 mips, flags=0)
    chain = mip_chain(sources["trim_emissive"], "linear")
    data_trim_emis = [[encode_bc4(lvl)] for lvl in chain]
    specs.append(TexSpec("trim_emissive", 2, 2048, 2048, 1, 12, 0, data_trim_emis))

    # 4: terrain_albedo (BC1, 512x512, 8 layers, 10 mips, flags=1)
    terr_alb_chains = [mip_chain(terr_alb[i], "srgb") for i in range(8)]
    data_terr_alb = []
    for l in range(10):
        data_terr_alb.append([encode_bc1(terr_alb_chains[lay][l]) for lay in range(8)])
    specs.append(TexSpec("terrain_albedo", 0, 512, 512, 8, 10, 1, data_terr_alb))

    # 5: terrain_normal (BC5, 512x512, 8 layers, 10 mips, flags=0)
    terr_norm_chains = [mip_chain(terr_norm[i], "normal2") for i in range(8)]
    data_terr_norm = []
    for l in range(10):
        data_terr_norm.append([encode_bc5(terr_norm_chains[lay][l]) for lay in range(8)])
    specs.append(TexSpec("terrain_normal", 3, 512, 512, 8, 10, 0, data_terr_norm))

    # 6: terrain_rh (BC1, 512x512, 8 layers, 10 mips, flags=0)
    terr_rh_chains = [mip_chain(terr_rh[i], "linear") for i in range(8)]
    data_terr_rh = []
    for l in range(10):
        data_terr_rh.append([encode_bc1(terr_rh_chains[lay][l]) for lay in range(8)])
    specs.append(TexSpec("terrain_rh", 0, 512, 512, 8, 10, 0, data_terr_rh))

    # 7: vfx_atlas (BC3, 2048x2048, 1 layer, 4 mips, flags=2)
    chain = mip_chain(sources["vfx_atlas"], "premul")[:4]
    data_vfx = [[encode_bc3(lvl)] for lvl in chain]
    specs.append(TexSpec("vfx_atlas", 1, 2048, 2048, 1, 4, 2, data_vfx))

    # 8: msdf_atlas (RGBA8, 1024x512, 1 layer, 1 mip, flags=0)
    data_msdf = [[sources["msdf_atlas"].tobytes()]]
    specs.append(TexSpec("msdf_atlas", 4, 1024, 512, 1, 1, 0, data_msdf))

    print(f"[stage] encode+mips {time.time() - t0:.2f}s")

    # Stage 6: write_tex26
    t0 = time.time()
    tex26_path = os.path.join(out_dir, "textures_2026.tex26")
    write_tex26(tex26_path, specs, glyph_bytes)
    print(f"[stage] write_tex26 {time.time() - t0:.2f}s")

    # Stage 7: manifest json
    t0 = time.time()
    manifest_path = os.path.join(out_dir, "textures_2026.manifest.json")
    from asset_common.texformat import read_tex26
    meta = read_tex26(tex26_path)

    tex_list = []
    for t_rec in meta["textures"]:
        tex_list.append({
            "name": t_rec["name"],
            "hash_hex": f"0x{t_rec['name_hash']:016X}",
            "format": t_rec["format"],
            "width": t_rec["width"],
            "height": t_rec["height"],
            "layers": t_rec["layers"],
            "mips": t_rec["mips"],
            "flags": t_rec["flags"],
            "data_bytes": t_rec["data_bytes"],
            "probe_a": list(t_rec["probe_a"]),
            "probe_b": list(t_rec["probe_b"]),
        })

    from shapes import get_icon_contours
    icon_names = [
        "frame_friendly", "frame_hostile", "frame_neutral", "frame_unknown",
        "sym_infantry", "sym_armor", "sym_artillery", "sym_air",
        "sym_naval", "sym_recon", "sym_missile", "sym_engineer",
        "sym_logistics", "sym_drone", "res_food", "res_timber",
        "res_metal", "res_wealth", "res_knowledge", "res_oil",
        "res_silicon", "ui_arrow_up", "ui_arrow_down", "ui_check",
        "ui_cross", "ui_plus", "ui_minus", "ui_play",
        "ui_ring", "ui_square_ring", "ui_gear", "ui_dot"
    ]

    vfx_effects_manifest = []
    for i, (name, is_loop, _) in enumerate(VFX_INFO):
        ox, oy = frame_origin(i, 0)
        vfx_effects_manifest.append({
            "name": name,
            "loop": is_loop,
            "cell_origin": [ox, oy],
        })

    manifest = {
        "textures": tex_list,
        "trim_names": [spec[1] for spec in TRIM_SPECS],
        "terrain_layers": [lay[1] for lay in TERRAIN_LAYERS],
        "vfx_effects": vfx_effects_manifest,
        "icon_names": icon_names,
        "glyph_count": 95,
        "terrain_roughness": [float(lay[4]) for lay in TERRAIN_LAYERS],
        "trim_max_lod": 5,
    }

    with open(manifest_path, "w", encoding="utf-8") as f:
        json.dump(manifest, f, indent=2, sort_keys=True)
    print(f"[stage] manifest {time.time() - t0:.2f}s")

    # Stage 8: preview
    if preview:
        t0 = time.time()
        preview_path = os.path.join(out_dir, "textures_2026.preview.png")
        write_preview(preview_path, sources)
        print(f"[stage] preview {time.time() - t0:.2f}s")

    if keep_sources:
        return sources
    return {}


def main():
    parser = argparse.ArgumentParser(description="Dominion 2026 Texture Baker")
    parser.add_argument("--out", default="assets/compiled/textures", help="Output directory")
    parser.add_argument("--no-preview", action="store_true", help="Disable preview PNG generation")
    args = parser.parse_args()

    try:
        bake_all(args.out, preview=not args.no_preview, keep_sources=False)
        sys.exit(0)
    except Exception as e:
        print(f"Bake failed: {e}", file=sys.stderr)
        import traceback
        traceback.print_exc()
        sys.exit(3)


if __name__ == "__main__":
    main()
