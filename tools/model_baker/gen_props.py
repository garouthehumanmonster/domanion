"""gen_props.py - Procedural generators for Props (4), Resources (7), and Projectiles (6).

Props (Cat 5):
  prop_tree_oak (9m), prop_tree_pine (12m), prop_tree_palm (10m), prop_rock (3.5m)

Resources (Cat 6):
  res_berry_bush (1.6m), res_herd (2.0m), res_gold (3.0m), res_iron (3.2m),
  res_oil (6.0m), res_silicon (3.0m), res_relic (4.0m)

Projectiles (Cat 7):
  proj_arrow (0.9m), proj_shell (0.8m), proj_rocket (2.0m), proj_missile (4.5m),
  proj_hypersonic (6.0m), proj_bomb (2.2m)
"""

import math
import numpy as np

import params
from prims import box, taper_box, cylinder, sphere, wedge, prism, Piece
from meshbuilder import Part, place
from rig import Rig
from anim import sway, bob, pulse, kick, tip, sink, spin
from model import Model, Hardpoint, P


def make_prop(id_str: str, target_dim: float) -> Model:
    rig = Rig()
    s = target_dim
    parts: list[Part] = []
    p_main = rig.add_part("main", (0.0, 0.0, 0.0), parent=-1)

    if id_str == "prop_tree_oak":
        p_foliage = rig.add_part("foliage", (0.0, 4.0, 0.0), parent=p_main)
        rig.set_ops(p_foliage, "Idle", [sway(2, 1.5, 1)])

        parts.append(P(cylinder, 0.5, 0.35, 4.5, part=p_main, at=(0.0, 2.25, 0.0), trim="wood_log", keep=True, name="trunk"))
        parts.append(P(sphere, 2.8, part=p_foliage, at=(0.0, 5.8, 0.0), trim="foliage_leaf", keep=True, name="canopy"))
        parts.append(P(sphere, 1.8, part=p_foliage, at=(1.2, 5.2, 0.8), trim="foliage_leaf", keep=False, name="canopy2"))

    elif id_str == "prop_tree_pine":
        p_foliage = rig.add_part("foliage", (0.0, 3.5, 0.0), parent=p_main)
        rig.set_ops(p_foliage, "Idle", [sway(2, 1.5, 1)])

        parts.append(P(cylinder, 0.45, 0.25, 6.0, part=p_main, at=(0.0, 3.0, 0.0), trim="wood_log", keep=True, name="trunk"))
        # 3 Tiered foliage cones
        parts.append(P(cylinder, 3.2, 0.0, 3.5, part=p_foliage, at=(0.0, 5.0, 0.0), trim="foliage_leaf", keep=True, name="cone1"))
        parts.append(P(cylinder, 2.4, 0.0, 3.2, part=p_foliage, at=(0.0, 7.5, 0.0), trim="foliage_leaf", keep=True, name="cone2"))
        parts.append(P(cylinder, 1.6, 0.0, 2.8, part=p_foliage, at=(0.0, 9.8, 0.0), trim="foliage_leaf", keep=True, name="cone3"))

    elif id_str == "prop_tree_palm":
        p_foliage = rig.add_part("foliage", (0.5, 8.5, 0.0), parent=p_main)
        rig.set_ops(p_foliage, "Idle", [sway(2, 1.5, 1)])

        parts.append(P(cylinder, 0.4, 0.25, 8.5, part=p_main, at=(0.25, 4.25, 0.0), rot=(0.0, 0.0, -4.0), trim="bark", keep=True, name="trunk"))
        for rot_y in (0.0, 60.0, 120.0, 180.0, 240.0, 300.0):
            parts.append(P(wedge, 0.4, 0.08, 2.6, part=p_foliage, at=(0.5, 8.5, 0.0), rot=(15.0, rot_y, 0.0), trim="foliage_leaf", keep=True, name="frond"))

    elif id_str == "prop_rock":
        def make_rock(lod: int) -> Piece:
            base = sphere(1.5, lod=lod, flat=True, jitter=(0.22, 1337))
            pos = base.pos.copy()
            pos[:, 1] *= 0.65
            # Shift so ground min y is exactly around 0.0
            pos[:, 1] -= np.min(pos[:, 1])
            return Piece(pos, base.nrm.copy(), base.uv.copy(), base.tris.copy())

        parts.append(Part(part=p_main, make=make_rock, pos=(0.0, 0.0, 0.0), rot_deg=(0.0, 0.0, 0.0), mirror_z=False, trim="stone_smooth", keep=True, name="rock"))

    return Model(id_str, category=5, target_dim_m=target_dim, parts=parts, rig=rig, hardpoints=[])


def make_resource(id_str: str, target_dim: float) -> Model:
    rig = Rig()
    s = target_dim
    parts: list[Part] = []
    p_main = rig.add_part("main", (0.0, 0.0, 0.0), parent=-1)

    if id_str == "res_berry_bush":
        parts.append(P(sphere, 0.60, part=p_main, at=(0.0, 0.60, 0.0), trim="foliage_leaf", keep=True, name="center"))
        parts.append(P(sphere, 0.42, part=p_main, at=(0.35, 0.45, 0.25), trim="foliage_leaf", keep=False, name="lobe1"))
        parts.append(P(sphere, 0.42, part=p_main, at=(-0.30, 0.45, -0.25), trim="foliage_leaf", keep=False, name="lobe2"))
        # Berries as small boxes
        for bx, by, bz in ((0.2, 0.7, 0.4), (-0.2, 0.6, 0.4), (0.0, 0.8, -0.3)):
            parts.append(P(box, 0.12, 0.12, 0.12, part=p_main, at=(bx, by, bz), trim="cloth_dark", keep=False, name="berry"))

    elif id_str == "res_herd":
        # Deer: body, 4 legs, head
        p_head = rig.add_part("head", (0.75, 1.4, 0.0), parent=p_main)
        rig.set_ops(p_head, "Idle", [sway(2, 8.0, 1)])

        parts.append(P(box, 1.5, 0.65, 0.55, part=p_main, at=(0.0, 1.15, 0.0), trim="leather", keep=True, name="body"))
        # 4 Legs
        for lx, lz in ((-0.55, -0.20), (-0.55, 0.20), (0.55, -0.20), (0.55, 0.20)):
            parts.append(P(box, 0.15, 0.85, 0.15, part=p_main, at=(lx, 0.425, lz), trim="leather", keep=True, name="leg"))
        # Head & neck
        parts.append(P(taper_box, 0.45, 0.30, 0.28, 0.22, 0.70, part=p_head, at=(0.75, 1.45, 0.0), dx=0.20, trim="leather", keep=True, name="head"))

    elif id_str == "res_gold":
        # Rock with gold ingots / crystals
        parts.append(P(sphere, 1.2, part=p_main, at=(0.0, 1.2, 0.0), trim="stone_smooth", keep=True, name="rock_base"))
        for gx, gy, gz in ((0.5, 1.4, 0.4), (-0.4, 1.3, -0.3), (0.1, 1.8, 0.1)):
            parts.append(P(box, 0.45, 0.35, 0.45, part=p_main, at=(gx, gy, gz), rot=(15.0, 30.0, 10.0), trim="gold", keep=True, name="nugget"))

    elif id_str == "res_iron":
        # Rock with iron / steel crystals
        parts.append(P(sphere, 1.3, part=p_main, at=(0.0, 1.3, 0.0), trim="stone_block", keep=True, name="rock_base"))
        for ix, iy, iz in ((0.6, 1.5, -0.3), (-0.5, 1.4, 0.4), (0.0, 1.9, 0.0)):
            parts.append(P(cylinder, 0.25, 0.0, 0.8, part=p_main, at=(ix, iy, iz), rot=(20.0, 45.0, 0.0), trim="steel_worn", keep=True, name="spike"))

    elif id_str == "res_oil":
        # Oil seep: dark flat cylinder + derrick + animated pumpjack (part 1)
        p_pump = rig.add_part("pumpjack", (0.0, 1.5, 0.0), parent=p_main)
        rig.set_ops(p_pump, "Idle", [sway(2, 20.0, 1)])

        parts.append(P(cylinder, 2.8, 2.8, 0.2, part=p_main, at=(0.0, 0.1, 0.0), trim="oil_dark", keep=True, name="slick"))
        parts.append(P(taper_box, 2.2, 2.2, 1.0, 1.0, 3.8, part=p_main, at=(0.0, 2.0, 0.0), trim="steel_plain", keep=True, name="derrick"))
        parts.append(P(box, 2.4, 0.4, 0.4, part=p_pump, at=(0.0, 3.6, 0.0), trim="steel_worn", keep=True, name="pump_beam"))

    elif id_str == "res_silicon":
        # Cluster of crystals in ice_crystal trim
        parts.append(P(box, 1.6, 0.3, 1.6, part=p_main, at=(0.0, 0.15, 0.0), trim="stone_smooth", keep=True, name="ground"))
        for cx, cz, h, r in ((0.0, 0.0, 2.6, 0.35), (0.5, 0.4, 2.0, 0.28), (-0.4, 0.5, 1.8, 0.25), (0.3, -0.5, 2.2, 0.30)):
            parts.append(P(cylinder, r, 0.0, h, part=p_main, at=(cx, h * 0.5, cz), rot=(cx * 8.0, 0.0, cz * 8.0), trim="ice_crystal", keep=True, name="crystal"))

    elif id_str == "res_relic":
        # Ancient obelisk + floating emissive sphere
        p_orb = rig.add_part("orb", (0.0, 3.6, 0.0), parent=p_main)
        rig.set_ops(p_orb, "Idle", [bob(1, 0.1, 1)])

        parts.append(P(box, 2.0, 0.6, 2.0, part=p_main, at=(0.0, 0.3, 0.0), trim="stone_smooth", keep=True, name="pedestal"))
        poly = [(-0.4, -0.4), (0.4, -0.4), (0.4, 0.4), (-0.4, 0.4)]
        parts.append(P(prism, poly, 2.6, part=p_main, at=(0.0, 1.9, 0.0), trim="stone_smooth", keep=True, name="obelisk"))
        parts.append(P(sphere, 0.35, part=p_orb, at=(0.0, 3.6, 0.0), trim="glow_emissive", emissive=True, keep=True, name="floating_orb"))

    return Model(id_str, category=6, target_dim_m=target_dim, parts=parts, rig=rig, hardpoints=[])


def make_projectile(id_str: str, target_dim: float) -> Model:
    rig = Rig()
    s = target_dim
    parts: list[Part] = []
    p_main = rig.add_part("main", (0.0, 0.0, 0.0), parent=-1)

    if id_str == "proj_arrow":
        # Thin cylinder + cone tip + 3 fins
        length = s * 0.95
        r = 0.015
        parts.append(P(cylinder, r, r, length * 0.85, part=p_main, at=(-length * 0.05, 0.0, 0.0), rot=(0.0, 0.0, -90.0), trim="wood_log", keep=True, name="shaft"))
        parts.append(P(cylinder, r * 1.8, 0.0, length * 0.15, part=p_main, at=(length * 0.42, 0.0, 0.0), rot=(0.0, 0.0, -90.0), trim="steel_plain", keep=True, name="tip"))
        parts.append(P(box, length * 0.15, 0.06, 0.005, part=p_main, at=(-length * 0.40, 0.0, 0.0), trim="cloth_dark", keep=True, name="fin1"))
        parts.append(P(box, length * 0.15, 0.005, 0.06, part=p_main, at=(-length * 0.40, 0.0, 0.0), trim="cloth_dark", keep=True, name="fin2"))

    elif id_str == "proj_shell":
        # Artillery shell: cylinder + cone nose
        length = s * 0.95
        r = length * 0.16
        parts.append(P(cylinder, r, r, length * 0.65, part=p_main, at=(-length * 0.15, 0.0, 0.0), rot=(0.0, 0.0, -90.0), trim="steel_worn", keep=True, name="body"))
        parts.append(P(cylinder, r, 0.0, length * 0.35, part=p_main, at=(length * 0.32, 0.0, 0.0), rot=(0.0, 0.0, -90.0), trim="steel_worn", keep=True, name="nose"))

    elif id_str == "proj_rocket":
        length = s * 0.95
        r = length * 0.06
        parts.append(P(cylinder, r, r, length * 0.75, part=p_main, at=(-length * 0.10, 0.0, 0.0), rot=(0.0, 0.0, -90.0), trim="steel_plain", keep=True, name="body"))
        parts.append(P(cylinder, r, 0.0, length * 0.25, part=p_main, at=(length * 0.37, 0.0, 0.0), rot=(0.0, 0.0, -90.0), trim="steel_plain", keep=True, name="nose"))
        parts.append(P(box, length * 0.18, length * 0.22, 0.01, part=p_main, at=(-length * 0.38, 0.0, 0.0), trim="steel_plain", keep=True, name="fin1"))
        parts.append(P(box, length * 0.18, 0.01, length * 0.22, part=p_main, at=(-length * 0.38, 0.0, 0.0), trim="steel_plain", keep=True, name="fin2"))

    elif id_str == "proj_missile":
        length = s * 0.95
        r = length * 0.05
        parts.append(P(cylinder, r, r, length * 0.75, part=p_main, at=(-length * 0.10, 0.0, 0.0), rot=(0.0, 0.0, -90.0), trim="armor_grey", keep=True, name="body"))
        parts.append(P(cylinder, r, 0.0, length * 0.25, part=p_main, at=(length * 0.37, 0.0, 0.0), rot=(0.0, 0.0, -90.0), trim="armor_grey", keep=True, name="radome"))
        parts.append(P(box, length * 0.16, length * 0.20, 0.015, part=p_main, at=(-length * 0.38, 0.0, 0.0), trim="armor_grey", keep=True, name="fin1"))
        parts.append(P(box, length * 0.16, 0.015, length * 0.20, part=p_main, at=(-length * 0.38, 0.0, 0.0), trim="armor_grey", keep=True, name="fin2"))
        # Emissive exhaust
        parts.append(P(cylinder, r * 0.8, r * 0.8, length * 0.05, part=p_main, at=(-length * 0.48, 0.0, 0.0), rot=(0.0, 0.0, -90.0), trim="glow_emissive", emissive=True, keep=True, name="exhaust"))

    elif id_str == "proj_hypersonic":
        length = s * 0.98
        r = length * 0.04
        # Slender cone-cylinder waverider
        parts.append(P(cylinder, r * 0.5, r, length * 0.50, part=p_main, at=(-length * 0.20, 0.0, 0.0), rot=(0.0, 0.0, -90.0), trim="armor_grey", keep=True, name="body"))
        parts.append(P(cylinder, r, 0.0, length * 0.48, part=p_main, at=(length * 0.25, 0.0, 0.0), rot=(0.0, 0.0, -90.0), trim="armor_grey", keep=True, name="nose"))
        parts.append(P(wedge, length * 0.22, 0.02, length * 0.16, part=p_main, at=(-length * 0.25, 0.0, 0.0), rot=(0.0, 0.0, 180.0), trim="steel_worn", keep=True, name="wings"))

    elif id_str == "proj_bomb":
        length = s * 0.95
        r = length * 0.14
        parts.append(P(cylinder, r, r, length * 0.65, part=p_main, at=(-length * 0.05, 0.0, 0.0), rot=(0.0, 0.0, -90.0), trim="armor_olive", keep=True, name="body"))
        parts.append(P(cylinder, r, 0.0, length * 0.25, part=p_main, at=(length * 0.35, 0.0, 0.0), rot=(0.0, 0.0, -90.0), trim="armor_olive", keep=True, name="nose"))
        parts.append(P(box, length * 0.20, length * 0.28, 0.02, part=p_main, at=(-length * 0.35, 0.0, 0.0), trim="steel_worn", keep=True, name="fin1"))
        parts.append(P(box, length * 0.20, 0.02, length * 0.28, part=p_main, at=(-length * 0.35, 0.0, 0.0), trim="steel_worn", keep=True, name="fin2"))

    return Model(id_str, category=7, target_dim_m=target_dim, parts=parts, rig=rig, hardpoints=[])
