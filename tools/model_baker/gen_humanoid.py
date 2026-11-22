"""gen_humanoid.py - Procedural generator for 9 infantry models."""

from dataclasses import dataclass
import math
import numpy as np

import params
from prims import box, taper_box, cylinder, sphere, wedge, Piece
from meshbuilder import Part, place
from rig import Rig
from anim import sway, bob, pulse, kick, tip, sink
from model import Model, Hardpoint, P


@dataclass
class HumanoidSpec:
    id: str
    target_dim_m: float
    weapon_kind: str
    headgear: str
    extra: str
    torso_trim: str
    limbs_trim: str = "cloth_dark"
    skin_trim: str = "skin"
    helmet_trim: str = "steel_plain"
    weapon_trim: str = "steel_plain"


def make_humanoid(spec: HumanoidSpec) -> Model:
    """Build a humanoid character model, rig, and hardpoints."""
    # Scale factor (default height 1.8m)
    s = 1.0

    # Rig setup
    rig = Rig()
    # 0: torso (pivot at base of torso)
    p_torso = rig.add_part("torso", (0.0, 0.85 * s, 0.0), parent=-1)
    # 1: leg_l (pivot at left hip)
    p_leg_l = rig.add_part("leg_l", (0.0, 0.85 * s, 0.11 * s), parent=p_torso)
    # 2: leg_r (pivot at right hip)
    p_leg_r = rig.add_part("leg_r", (0.0, 0.85 * s, -0.11 * s), parent=p_torso)
    # 3: arm_l (pivot at left shoulder)
    p_arm_l = rig.add_part("arm_l", (0.0, 1.45 * s, 0.29 * s), parent=p_torso)
    # 4: arm_r (pivot at right shoulder)
    p_arm_r = rig.add_part("arm_r", (0.0, 1.45 * s, -0.29 * s), parent=p_torso)
    # 5: weapon (attached to right arm)
    p_weapon = rig.add_part("weapon", (0.2 * s, 1.14 * s, -0.29 * s), parent=p_arm_r)
    # 6: head (attached to torso)
    p_head = rig.add_part("head", (0.0, 1.50 * s, 0.0), parent=p_torso)

    has_extra = spec.extra != "none"
    p_extra = -1
    if has_extra:
        # Shield attaches to left arm; backpack/quiver attaches to torso
        parent_extra = p_arm_l if "shield" in spec.extra else p_torso
        p_extra = rig.add_part("extra", (0.0, 1.25 * s, 0.0), parent=parent_extra)

    # Assign Animations
    rig.set_ops(p_torso, "Idle", [bob(1, 0.012 * s, 1)])
    rig.set_ops(p_arm_l, "Idle", [sway(2, 2.0, 1)])
    rig.set_ops(p_arm_r, "Idle", [sway(2, -2.0, 1)])

    rig.set_ops(p_leg_l, "Move", [sway(2, 35.0, 1)])
    rig.set_ops(p_leg_r, "Move", [sway(2, -35.0, 1)])
    rig.set_ops(p_arm_l, "Move", [sway(2, -25.0, 1)])
    rig.set_ops(p_arm_r, "Move", [sway(2, 25.0, 1)])
    rig.set_ops(p_torso, "Move", [bob(1, 0.04 * s, 2)])

    wk = spec.weapon_kind
    if wk == "spear":
        rig.set_ops(p_weapon, "Attack", [kick(0, 0.7 * s)])
    elif wk == "sword":
        rig.set_ops(p_arm_r, "Attack", [pulse(2, -80.0)])
    elif wk in ("bow", "musket", "rifle", "rifle_short"):
        rig.set_ops(p_weapon, "Attack", [kick(0, -0.05 * s)])
        rig.set_ops(p_arm_r, "Attack", [pulse(2, 4.0)])
    elif wk == "launcher":
        rig.set_ops(p_weapon, "Attack", [kick(0, -0.1 * s)])
    elif wk == "tool_pick":
        rig.set_ops(p_arm_r, "Attack", [pulse(2, -70.0)])

    rig.set_ops(p_torso, "Die", [tip(2, 90.0), sink(1, -0.08 * s)])

    # Geometry Construction
    parts = []

    # Part 0: Torso (taper_box at y=1.16)
    # Surface area ~1.2m^2 for team color share
    parts.append(P(
        taper_box, 0.30 * s, 0.46 * s, 0.28 * s, 0.50 * s, 0.62 * s,
        part=p_torso, at=(0.0, 1.16 * s, 0.0), trim=spec.torso_trim, team=True, keep=True, name="torso"
    ))

    # Part 1 & 2: Legs (boxes 0.16 x 0.85 x 0.18 at y=0.425)
    parts.append(P(
        box, 0.16 * s, 0.85 * s, 0.18 * s,
        part=p_leg_l, at=(0.0, 0.425 * s, 0.11 * s), trim=spec.limbs_trim, keep=True, name="leg_l"
    ))
    parts.append(P(
        box, 0.16 * s, 0.85 * s, 0.18 * s,
        part=p_leg_r, at=(0.0, 0.425 * s, -0.11 * s), trim=spec.limbs_trim, keep=True, name="leg_r"
    ))

    # Part 3 & 4: Arms (boxes 0.12 x 0.62 x 0.12 at y=1.14)
    parts.append(P(
        box, 0.12 * s, 0.62 * s, 0.12 * s,
        part=p_arm_l, at=(0.0, 1.14 * s, 0.29 * s), trim=spec.limbs_trim, keep=True, name="arm_l"
    ))
    parts.append(P(
        box, 0.12 * s, 0.62 * s, 0.12 * s,
        part=p_arm_r, at=(0.0, 1.14 * s, -0.29 * s), trim=spec.limbs_trim, keep=True, name="arm_r"
    ))

    # Part 6: Head & Headgear
    def make_head(lod: int) -> Piece:
        pieces = []
        # Base head sphere
        head_sphere = sphere(0.11 * s, lod=lod, flat=False)
        pieces.append(head_sphere)

        # Headgear piece
        hg = spec.headgear
        if hg == "helmet_bronze":
            # Cone helmet
            cone_h = cylinder(0.13 * s, 0.0, 0.12 * s, lod=lod)
            placed_cone = place(cone_h, (0.0, 0.06 * s, 0.0))
            pieces.append(placed_cone)
        elif hg == "hood":
            hood_b = taper_box(0.24 * s, 0.24 * s, 0.20 * s, 0.20 * s, 0.22 * s)
            pieces.append(hood_b)
        elif hg == "helmet_crest":
            crest_b = box(0.24 * s, 0.08 * s, 0.04 * s)
            placed_c = place(crest_b, (0.0, 0.12 * s, 0.0))
            pieces.append(placed_c)
        elif hg == "hat_tricorn":
            tri_b = box(0.32 * s, 0.04 * s, 0.32 * s)
            placed_tri = place(tri_b, (0.0, 0.06 * s, 0.0))
            pieces.append(placed_tri)
        elif hg in ("helmet_modern", "helmet_nvg"):
            dome = cylinder(0.13 * s, 0.11 * s, 0.10 * s, lod=lod)
            placed_d = place(dome, (0.0, 0.06 * s, 0.0))
            pieces.append(placed_d)
            if hg == "helmet_nvg":
                nvg = box(0.08 * s, 0.04 * s, 0.08 * s)
                placed_nvg = place(nvg, (0.13 * s, 0.02 * s, 0.0))
                pieces.append(placed_nvg)
        elif hg == "hat_straw":
            straw = cylinder(0.25 * s, 0.08 * s, 0.08 * s, lod=lod)
            placed_s = place(straw, (0.0, 0.06 * s, 0.0))
            pieces.append(placed_s)

        # Merge pieces into single head Piece
        all_v = [p.pos for p in pieces]
        all_n = [p.nrm for p in pieces]
        all_uv = [p.uv for p in pieces]
        all_t = []
        off = 0
        for p in pieces:
            all_t.append(p.tris + off)
            off += len(p.pos)

        return Piece(
            np.vstack(all_v), np.vstack(all_n),
            np.vstack(all_uv), np.vstack(all_t)
        )

    parts.append(Part(
        part=p_head,
        make=make_head,
        pos=(0.0, 1.62 * s, 0.0),
        trim=spec.skin_trim,
        keep=True,
        name="head"
    ))

    # Part 5: Weapon & Muzzle
    hardpoints = []
    muzzle_pos = None

    def make_weapon(lod: int) -> Piece:
        pieces = []
        if wk == "spear":
            # Cylinder along X
            shaft = cylinder(0.025 * s, 0.025 * s, 2.1 * s, lod=lod)
            shaft_x = place(shaft, (0.8 * s, 0.0, 0.0), rot_deg=(0.0, 0.0, 90.0))
            tip_cone = cylinder(0.04 * s, 0.0, 0.20 * s, lod=lod)
            tip_x = place(tip_cone, (1.95 * s, 0.0, 0.0), rot_deg=(0.0, 0.0, -90.0))
            pieces.extend([shaft_x, tip_x])
        elif wk == "bow":
            # 3 curved boxes
            b1 = box(0.04 * s, 0.30 * s, 0.03 * s)
            b2 = place(box(0.03 * s, 0.25 * s, 0.03 * s), (0.05 * s, 0.25 * s, 0.0), rot_deg=(0, 0, -20))
            b3 = place(box(0.03 * s, 0.25 * s, 0.03 * s), (0.05 * s, -0.25 * s, 0.0), rot_deg=(0, 0, 20))
            pieces.extend([b1, b2, b3])
        elif wk == "sword":
            blade = box(0.04 * s, 0.70 * s, 0.015 * s)
            blade_p = place(blade, (0.1 * s, 0.30 * s, 0.0), rot_deg=(0, 0, -60))
            guard = box(0.06 * s, 0.03 * s, 0.16 * s)
            pieces.extend([blade_p, guard])
        elif wk in ("musket", "rifle", "rifle_short"):
            stock_len = 0.5 * s if wk != "rifle_short" else 0.35 * s
            barrel_len = 0.6 * s if wk == "rifle_short" else (0.9 * s if wk == "rifle" else 1.1 * s)
            stock = box(stock_len, 0.08 * s, 0.04 * s)
            barrel = cylinder(0.025 * s, 0.025 * s, barrel_len, lod=lod)
            barrel_x = place(barrel, (stock_len / 2.0 + barrel_len / 2.0, 0.03 * s, 0.0), rot_deg=(0, 0, 90))
            pieces.extend([stock, barrel_x])
        elif wk == "launcher":
            tube = cylinder(0.08 * s, 0.08 * s, 1.0 * s, lod=lod)
            tube_x = place(tube, (0.2 * s, 0.2 * s, 0.0), rot_deg=(0, 0, 90))
            pieces.append(tube_x)
        elif wk == "tool_pick":
            handle = cylinder(0.03 * s, 0.03 * s, 0.8 * s, lod=lod)
            head_p = box(0.35 * s, 0.08 * s, 0.05 * s)
            head_placed = place(head_p, (0.0, 0.35 * s, 0.0))
            pieces.extend([handle, head_placed])
        elif wk == "controller":
            cbox = box(0.18 * s, 0.05 * s, 0.12 * s)
            ant = cylinder(0.01 * s, 0.01 * s, 0.25 * s, lod=lod)
            ant_p = place(ant, (0.06 * s, 0.12 * s, 0.0))
            pieces.extend([cbox, ant_p])

        all_v = [p.pos for p in pieces]
        all_n = [p.nrm for p in pieces]
        all_uv = [p.uv for p in pieces]
        all_t = []
        off = 0
        for p in pieces:
            all_t.append(p.tris + off)
            off += len(p.pos)

        return Piece(
            np.vstack(all_v), np.vstack(all_n),
            np.vstack(all_uv), np.vstack(all_t)
        )

    parts.append(Part(
        part=p_weapon,
        make=make_weapon,
        pos=(0.2 * s, 1.14 * s, -0.29 * s),
        trim=spec.weapon_trim,
        keep=True,
        name="weapon"
    ))

    # Determine muzzle position for hardpoints
    if wk == "spear":
        muzzle_pos = (2.2 * s, 1.14 * s, -0.29 * s)
    elif wk == "bow":
        muzzle_pos = (0.35 * s, 1.14 * s, -0.29 * s)
    elif wk == "sword":
        muzzle_pos = (0.7 * s, 1.45 * s, -0.29 * s)
    elif wk in ("musket", "rifle", "rifle_short"):
        x_front = 1.3 * s if wk == "musket" else (1.1 * s if wk == "rifle" else 0.85 * s)
        muzzle_pos = (x_front, 1.17 * s, -0.29 * s)
    elif wk == "launcher":
        muzzle_pos = (0.9 * s, 1.34 * s, -0.29 * s)

    if muzzle_pos is not None:
        hardpoints.append(Hardpoint(kind=params.HP_MUZZLE, part=p_weapon, pos=muzzle_pos))

    # Part 7: Extra (Shield or Pack)
    if has_extra:
        def make_extra(lod: int) -> Piece:
            if spec.extra == "shield_round":
                return cylinder(0.24 * s, 0.24 * s, 0.04 * s, lod=lod)
            elif spec.extra == "shield_kite":
                return taper_box(0.04 * s, 0.30 * s, 0.04 * s, 0.16 * s, 0.55 * s)
            elif spec.extra == "pack":
                return box(0.18 * s, 0.28 * s, 0.26 * s)
            elif spec.extra == "quiver":
                return cylinder(0.06 * s, 0.06 * s, 0.45 * s, lod=lod)
            return box(0.1 * s, 0.1 * s, 0.1 * s)

        extra_pos = (0.0, 1.14 * s, 0.38 * s) if "shield" in spec.extra else (-0.22 * s, 1.25 * s, 0.0)
        extra_rot = (90, 0, 0) if spec.extra == "shield_round" else (0, 0, 0)
        parts.append(Part(
            part=p_extra,
            make=make_extra,
            pos=extra_pos,
            rot_deg=extra_rot,
            trim=spec.weapon_trim if "shield" in spec.extra else spec.limbs_trim,
            keep=False,
            name="extra"
        ))

    return Model(
        id=spec.id,
        category=params.CAT_INFANTRY,
        target_dim_m=spec.target_dim_m,
        parts=parts,
        rig=rig,
        hardpoints=hardpoints,
    )
