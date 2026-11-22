"""gen_building.py - Procedural generators for 18 building models.

Models (Category 4):
  bld_city_center_a (30m), bld_city_center_b (36m), bld_city_center_c (44m),
  bld_house (9m), bld_farm (24m), bld_lumber (14m), bld_mine (14m),
  bld_market (20m), bld_barracks (24m), bld_factory (40m), bld_airfield (90m),
  bld_dock (40m), bld_library (26m), bld_tower (16m), bld_sam_site (20m),
  bld_radar (18m), bld_silo (16m), bld_refinery (36m)
"""

import math
import numpy as np

import params
from prims import box, taper_box, cylinder, sphere, wedge, prism, Piece
from meshbuilder import Part, place
from rig import Rig
from anim import sway, bob, pulse, kick, tip, sink, spin
from model import Model, Hardpoint, P


def make_building(id_str: str, target_dim: float) -> Model:
    rig = Rig()
    s = target_dim
    parts: list[Part] = []
    hardpoints: list[Hardpoint] = []

    p_main = rig.add_part("main", (0.0, 0.0, 0.0), parent=-1)

    if id_str == "bld_city_center_a":
        h = 18.0
        rig.set_ops(p_main, "Die", [sink(1, -0.45 * h), tip(2, 3.0)])
        p_flag = rig.add_part("flagpole", (0.0, 18.0, 0.0), parent=p_main)
        rig.set_ops(p_flag, "Idle", [sway(2, 3.0, 1)])
        rig.set_ops(p_flag, "Move", [sway(2, 3.0, 1)])

        parts.append(P(box, 24.0, 9.0, 20.0, part=p_main, at=(0.0, 4.5, 0.0), trim="stone_block", keep=True, name="walls"))
        parts.append(P(taper_box, 25.0, 21.0, 24.0, 2.0, 7.0, part=p_main, at=(0.0, 12.5, 0.0), trim="roof_tile", keep=True, name="roof"))
        parts.append(P(box, 24.2, 2.2, 8.0, part=p_main, at=(0.0, 5.0, 0.0), trim="cloth_tunic", team=True, keep=True, name="banner"))
        # 4 Corner towers
        for cx, cz in ((-12.0, -10.0), (-12.0, 10.0), (12.0, -10.0), (12.0, 10.0)):
            parts.append(P(cylinder, 1.4, 1.2, 12.0, part=p_main, at=(cx, 6.0, cz), trim="stone_smooth", keep=True, name="tower"))
        parts.append(P(cylinder, 0.15, 0.10, 6.0, part=p_flag, at=(0.0, 19.0, 0.0), trim="steel_plain", keep=True, name="mast"))
        parts.append(P(box, 2.2, 1.2, 0.05, part=p_flag, at=(1.1, 20.5, 0.0), trim="cloth_tunic", team=True, keep=True, name="flag"))
        hardpoints.append(Hardpoint(4, p_main, (12.0, 0.0, 0.0)))

    elif id_str == "bld_city_center_b":
        h = 28.0
        rig.set_ops(p_main, "Die", [sink(1, -0.45 * h), tip(2, 3.0)])
        p_flag = rig.add_part("flagpole", (0.0, 28.0, 0.0), parent=p_main)
        rig.set_ops(p_flag, "Idle", [sway(2, 3.0, 1)])
        rig.set_ops(p_flag, "Move", [sway(2, 3.0, 1)])

        parts.append(P(box, 28.0, 12.0, 24.0, part=p_main, at=(0.0, 6.0, 0.0), trim="brick", keep=True, name="base"))
        parts.append(P(cylinder, 4.5, 4.0, 11.2, part=p_main, at=(0.0, 17.6, 0.0), trim="stone_smooth", keep=True, name="tower"))
        # Lift sphere by 0.5m so it doesn't touch cylinder top seam
        parts.append(P(sphere, 4.0, part=p_main, at=(0.0, 24.2, 0.0), trim="roof_metal", keep=True, name="dome"))
        parts.append(P(box, 28.2, 2.2, 24.2, part=p_main, at=(0.0, 11.0, 0.0), trim="cloth_tunic", team=True, keep=True, name="team_band"))
        parts.append(P(cylinder, 0.15, 0.10, 5.0, part=p_flag, at=(0.0, 28.5, 0.0), trim="steel_plain", keep=True, name="mast"))
        parts.append(P(box, 2.2, 1.2, 0.05, part=p_flag, at=(1.1, 29.5, 0.0), trim="cloth_tunic", team=True, keep=True, name="flag"))
        hardpoints.append(Hardpoint(4, p_main, (14.0, 0.0, 0.0)))

    elif id_str == "bld_city_center_c":
        h = 42.0
        rig.set_ops(p_main, "Die", [sink(1, -0.45 * h), tip(2, 3.0)])
        p_dish = rig.add_part("dish", (4.0, 36.0, 4.0), parent=p_main)
        rig.set_ops(p_dish, "Idle", [spin(1, 1)])
        rig.set_ops(p_dish, "Move", [spin(1, 1)])

        parts.append(P(box, 22.0, 34.0, 22.0, part=p_main, at=(0.0, 17.0, 0.0), trim="concrete_panel", keep=True, name="tower"))
        parts.append(P(box, 22.2, 28.0, 18.0, part=p_main, at=(0.0, 17.0, 0.0), trim="glass_window", keep=True, name="glass"))
        parts.append(P(box, 22.4, 3.5, 22.4, part=p_main, at=(0.0, 31.0, 0.0), trim="cloth_tunic", team=True, keep=True, name="team_stripe"))
        # 4 Corner columns
        for cx, cz in ((-10.5, -10.5), (-10.5, 10.5), (10.5, -10.5), (10.5, 10.5)):
            parts.append(P(cylinder, 1.0, 1.0, 34.0, part=p_main, at=(cx, 17.0, cz), trim="steel_plain", keep=True, name="col"))
        parts.append(P(cylinder, 0.4, 0.1, 8.0, part=p_main, at=(-4.0, 38.0, -4.0), trim="steel_plain", keep=True, name="antenna"))
        parts.append(P(cylinder, 2.2, 2.2, 0.5, part=p_dish, at=(4.0, 36.0, 4.0), trim="hazard_stripe", keep=True, name="dish"))
        hardpoints.append(Hardpoint(4, p_main, (11.0, 0.0, 0.0)))

    elif id_str == "bld_house":
        h = 7.5
        rig.set_ops(p_main, "Die", [sink(1, -0.45 * h), tip(2, 3.0)])
        parts.append(P(box, 7.5, 3.8, 6.0, part=p_main, at=(0.0, 1.9, 0.0), trim="wood_planks", keep=True, name="walls"))
        parts.append(P(taper_box, 8.0, 6.5, 4.0, 2.0, 2.8, part=p_main, at=(0.0, 5.2, 0.0), trim="roof_tile", keep=True, name="roof"))
        parts.append(P(box, 0.8, 2.5, 0.8, part=p_main, at=(-2.0, 5.5, 1.5), trim="brick", keep=False, name="chimney"))
        parts.append(P(box, 1.8, 2.2, 6.1, part=p_main, at=(1.5, 1.5, 0.0), trim="cloth_tunic", team=True, keep=True, name="team_door"))
        # 4 Porch columns
        for px, pz in ((2.5, 2.5), (2.5, -2.5), (3.6, 2.5), (3.6, -2.5)):
            parts.append(P(cylinder, 0.15, 0.15, 3.0, part=p_main, at=(px, 1.5, pz), trim="wood_log", keep=True, name="porch_col"))

    elif id_str == "bld_farm":
        h = 14.0
        rig.set_ops(p_main, "Die", [sink(1, -0.45 * h), tip(2, 3.0)])
        p_blades = rig.add_part("blades", (-4.0, 12.0, 4.6), parent=p_main)
        rig.set_ops(p_blades, "Idle", [spin(0, 1)])
        rig.set_ops(p_blades, "Move", [spin(0, 1)])

        parts.append(P(box, 12.0, 7.0, 9.0, part=p_main, at=(-4.0, 3.5, 0.0), trim="wood_planks", keep=True, name="barn"))
        parts.append(P(taper_box, 12.5, 9.5, 12.5, 2.0, 4.5, part=p_main, at=(-4.0, 9.25, 0.0), trim="roof_tile", keep=True, name="roof"))
        parts.append(P(box, 10.0, 0.1, 18.0, part=p_main, at=(6.0, 0.05, 0.0), trim="grass_tuft", keep=True, name="field"))
        parts.append(P(box, 12.2, 1.6, 9.2, part=p_main, at=(-4.0, 3.5, 0.0), trim="cloth_tunic", team=True, keep=True, name="team_stripe"))
        # Grain silo
        parts.append(P(cylinder, 2.0, 2.0, 9.0, part=p_main, at=(-11.0, 4.5, -2.0), trim="concrete", keep=True, name="silo"))
        parts.append(P(sphere, 2.0, part=p_main, at=(-11.0, 9.2, -2.0), trim="roof_metal", keep=True, name="silo_dome"))
        parts.append(P(box, 0.1, 8.0, 0.6, part=p_blades, at=(-4.0, 12.0, 4.6), trim="steel_plain", keep=True, name="blade1"))
        parts.append(P(box, 0.1, 0.6, 8.0, part=p_blades, at=(-4.0, 12.0, 4.6), trim="steel_plain", keep=True, name="blade2"))

    elif id_str == "bld_lumber":
        h = 8.0
        rig.set_ops(p_main, "Die", [sink(1, -0.45 * h), tip(2, 3.0)])
        parts.append(P(box, 9.0, 4.5, 7.0, part=p_main, at=(-1.5, 2.25, 0.0), trim="wood_planks", keep=True, name="mill"))
        parts.append(P(wedge, 7.5, 2.8, 9.5, part=p_main, at=(-1.5, 5.9, 0.0), rot=(0.0, 90.0, 0.0), trim="roof_tile", keep=True, name="roof"))
        parts.append(P(box, 9.2, 1.2, 4.0, part=p_main, at=(-1.5, 3.0, 0.0), trim="cloth_tunic", team=True, keep=True, name="team_stripe"))
        # 4 Log piles
        for ly, lz in ((0.5, -1.5), (0.5, 1.5), (1.4, 0.0), (0.5, 0.0)):
            parts.append(P(cylinder, 0.4, 0.4, 5.0, part=p_main, at=(4.0, ly, lz), rot=(90.0, 0.0, 0.0), trim="wood_log", keep=False, name="logs"))

    elif id_str == "bld_mine":
        h = 13.0
        rig.set_ops(p_main, "Die", [sink(1, -0.45 * h), tip(2, 3.0)])
        parts.append(P(taper_box, 7.5, 7.5, 3.5, 3.5, 11.0, part=p_main, at=(0.0, 5.5, 0.0), trim="steel_plain", keep=True, name="headframe"))
        parts.append(P(cylinder, 1.8, 1.8, 0.5, part=p_main, at=(0.0, 11.5, 0.0), trim="steel_worn", keep=True, name="wheel"))
        parts.append(P(box, 5.0, 3.5, 4.0, part=p_main, at=(2.5, 1.75, 0.0), trim="stone_block", keep=True, name="shed"))
        parts.append(P(box, 7.7, 1.4, 7.7, part=p_main, at=(0.0, 4.0, 0.0), trim="cloth_tunic", team=True, keep=True, name="team_stripe"))
        # 4 Support legs
        for cx, cz in ((-3.2, -3.2), (-3.2, 3.2), (3.2, -3.2), (3.2, 3.2)):
            parts.append(P(cylinder, 0.35, 0.25, 11.0, part=p_main, at=(cx, 5.5, cz), trim="steel_worn", keep=True, name="pillar"))

    elif id_str == "bld_market":
        h = 7.0
        rig.set_ops(p_main, "Die", [sink(1, -0.45 * h), tip(2, 3.0)])
        p_decor = rig.add_part("decor", (0.0, 0.0, 0.0), parent=p_main)

        parts.append(P(box, 18.0, 0.2, 18.0, part=p_main, at=(0.0, 0.1, 0.0), trim="stone_smooth", keep=True, name="plaza"))
        # 4 Stalls
        for sx, sz in ((-4.5, -4.5), (-4.5, 4.5), (4.5, -4.5), (4.5, 4.5)):
            parts.append(P(box, 4.5, 2.5, 3.5, part=p_main, at=(sx, 1.25, sz), trim="wood_planks", keep=True, name="stall"))
            parts.append(P(wedge, 3.6, 1.4, 4.6, part=p_main, at=(sx, 3.2, sz), rot=(0.0, 90.0, 0.0), trim="cloth_tunic", team=True, keep=True, name="awning"))
        # Center plaza fountain / columns on p_decor (drops on LOD2)
        for fx, fz in ((-1.5, 0.0), (1.5, 0.0), (0.0, -1.5), (0.0, 1.5)):
            parts.append(P(cylinder, 0.3, 0.3, 3.5, part=p_decor, at=(fx, 1.75, fz), trim="stone_smooth", keep=False, name="fountain_col"))

    elif id_str == "bld_barracks":
        h = 10.0
        rig.set_ops(p_main, "Die", [sink(1, -0.45 * h), tip(2, 3.0)])
        # NO Idle animation ops (bld_barracks has no Idle motion)
        parts.append(P(box, 20.0, 6.5, 12.0, part=p_main, at=(0.0, 3.25, 0.0), trim="brick", keep=True, name="dorm"))
        parts.append(P(taper_box, 20.8, 12.8, 18.0, 8.0, 3.2, part=p_main, at=(0.0, 8.1, 0.0), trim="roof_metal", keep=True, name="roof"))
        parts.append(P(box, 20.2, 1.5, 12.2, part=p_main, at=(0.0, 4.0, 0.0), trim="cloth_tunic", team=True, keep=True, name="team_stripe"))
        # 4 Corner defense pillars
        for bx, bz in ((-9.5, -5.5), (-9.5, 5.5), (9.5, -5.5), (9.5, 5.5)):
            parts.append(P(cylinder, 0.8, 0.8, 7.5, part=p_main, at=(bx, 3.75, bz), trim="concrete", keep=True, name="pillar"))
        hardpoints.append(Hardpoint(4, p_main, (10.0, 0.0, 0.0)))

    elif id_str == "bld_factory":
        h = 24.0
        rig.set_ops(p_main, "Die", [sink(1, -0.45 * h), tip(2, 3.0)])
        parts.append(P(box, 32.0, 11.0, 18.0, part=p_main, at=(0.0, 5.5, 0.0), trim="brick", keep=True, name="hall"))
        parts.append(P(box, 32.8, 2.5, 18.8, part=p_main, at=(0.0, 12.25, 0.0), trim="roof_metal", keep=True, name="roof"))
        parts.append(P(cylinder, 1.6, 1.1, 22.0, part=p_main, at=(-11.0, 11.0, 5.0), trim="concrete", keep=True, name="stack"))
        parts.append(P(box, 32.2, 2.2, 7.0, part=p_main, at=(0.0, 6.0, 0.0), trim="cloth_tunic", team=True, keep=True, name="team_stripe"))
        # 3 Roof ventilation cylinders
        for vx in (-5.0, 0.0, 5.0):
            parts.append(P(cylinder, 1.0, 1.0, 2.5, part=p_main, at=(vx, 14.75, 0.0), trim="steel_worn", keep=False, name="vent"))
        hardpoints.append(Hardpoint(4, p_main, (16.0, 0.0, 0.0)))

    elif id_str == "bld_airfield":
        h = 22.0
        rig.set_ops(p_main, "Die", [sink(1, -0.45 * h), tip(2, 3.0)])
        parts.append(P(box, 82.0, 0.3, 24.0, part=p_main, at=(0.0, 0.15, 0.0), trim="concrete_panel", keep=True, name="runway"))
        parts.append(P(box, 70.0, 0.35, 4.0, part=p_main, at=(0.0, 0.18, 0.0), trim="cloth_tunic", team=True, keep=True, name="runway_team"))
        parts.append(P(taper_box, 24.0, 18.0, 22.0, 16.0, 9.0, part=p_main, at=(-16.0, 4.5, 16.0), trim="roof_metal", keep=True, name="hangar"))
        parts.append(P(box, 6.0, 20.0, 6.0, part=p_main, at=(20.0, 10.0, 16.0), trim="concrete", keep=True, name="tower"))
        parts.append(P(box, 7.2, 3.5, 7.2, part=p_main, at=(20.0, 20.0, 16.0), trim="glass_window", keep=True, name="cab"))
        # 4 Fuel storage tanks
        for tx in (-26.0, -22.0, -18.0, -14.0):
            parts.append(P(cylinder, 1.8, 1.8, 5.0, part=p_main, at=(tx, 2.5, 26.0), trim="steel_plain", keep=False, name="tank"))
        hardpoints.append(Hardpoint(4, p_main, (41.0, 0.0, 0.0)))

    elif id_str == "bld_dock":
        h = 16.0
        rig.set_ops(p_main, "Die", [sink(1, -0.45 * h), tip(2, 3.0)])
        p_crane = rig.add_part("crane", (10.0, 2.0, 4.0), parent=p_main)
        rig.set_ops(p_crane, "Idle", [sway(1, 15.0, 1)])
        rig.set_ops(p_crane, "Move", [sway(1, 15.0, 1)])

        parts.append(P(box, 36.0, 2.0, 16.0, part=p_main, at=(0.0, 1.0, 0.0), trim="concrete", keep=True, name="pier"))
        parts.append(P(box, 18.0, 7.5, 11.0, part=p_main, at=(-8.0, 5.75, 0.0), trim="roof_metal", keep=True, name="warehouse"))
        parts.append(P(box, 18.2, 1.6, 11.2, part=p_main, at=(-8.0, 5.5, 0.0), trim="cloth_tunic", team=True, keep=True, name="team_stripe"))
        parts.append(P(cylinder, 0.8, 0.6, 6.0, part=p_crane, at=(10.0, 5.0, 4.0), trim="steel_plain", keep=True, name="crane_body"))
        parts.append(P(box, 8.0, 0.6, 0.6, part=p_crane, at=(12.0, 8.0, 4.0), trim="hazard_stripe", keep=True, name="crane_arm"))
        # 4 Mooring pilings
        for px, pz in ((-16.0, 8.5), (-6.0, 8.5), (6.0, 8.5), (16.0, 8.5)):
            parts.append(P(cylinder, 0.5, 0.5, 3.5, part=p_main, at=(px, 1.75, pz), trim="wood_log", keep=False, name="piling"))
        hardpoints.append(Hardpoint(4, p_main, (18.0, 0.0, 0.0)))

    elif id_str == "bld_library":
        h = 17.0
        rig.set_ops(p_main, "Die", [sink(1, -0.45 * h), tip(2, 3.0)])
        parts.append(P(box, 22.0, 2.0, 18.0, part=p_main, at=(0.0, 1.0, 0.0), trim="stone_smooth", keep=True, name="base"))
        parts.append(P(box, 18.0, 8.0, 15.0, part=p_main, at=(0.0, 6.0, 0.0), trim="stone_block", keep=True, name="hall"))
        parts.append(P(sphere, 5.5, part=p_main, at=(0.0, 12.0, 0.0), trim="roof_metal", keep=True, name="dome"))
        # Columns
        for cz in (-6.0, -2.0, 2.0, 6.0):
            parts.append(P(cylinder, 0.45, 0.45, 7.5, part=p_main, at=(9.5, 5.75, cz), trim="stone_smooth", keep=False, name="col"))
        parts.append(P(box, 18.2, 1.8, 4.0, part=p_main, at=(0.0, 8.5, 5.5), trim="cloth_tunic", team=True, keep=True, name="team_frieze"))

    elif id_str == "bld_tower":
        h = 16.0
        rig.set_ops(p_main, "Die", [sink(1, -0.45 * h), tip(2, 3.0)])
        parts.append(P(cylinder, 2.5, 2.0, 14.5, part=p_main, at=(0.0, 7.25, 0.0), trim="stone_block", keep=True, name="tower_body"))
        parts.append(P(cylinder, 2.7, 2.7, 1.5, part=p_main, at=(0.0, 15.25, 0.0), trim="stone_smooth", keep=True, name="battlement"))
        parts.append(P(box, 1.4, 5.0, 0.1, part=p_main, at=(0.0, 8.5, 2.2), trim="cloth_tunic", team=True, keep=True, name="banner"))
        # 3 Buttress pillars offset radially
        for rot_y in (0.0, 120.0, 240.0):
            rad = math.radians(rot_y)
            bx = 2.4 * math.cos(rad)
            bz = 2.4 * math.sin(rad)
            parts.append(P(cylinder, 0.6, 0.4, 8.0, part=p_main, at=(bx, 4.0, bz), trim="stone_smooth", keep=False, name="buttress"))
        hardpoints.append(Hardpoint(0, p_main, (0.0, 16.0, 0.0)))

    elif id_str == "bld_sam_site":
        h = 6.0
        rig.set_ops(p_main, "Die", [sink(1, -0.45 * h), tip(2, 3.0)])
        parts.append(P(box, 16.0, 2.0, 16.0, part=p_main, at=(0.0, 1.0, 0.0), trim="concrete", keep=True, name="bunker"))
        parts.append(P(box, 16.2, 0.8, 16.2, part=p_main, at=(0.0, 1.4, 0.0), trim="cloth_tunic", team=True, keep=True, name="team_stripe"))
        # Radar dome on bunker
        parts.append(P(sphere, 2.5, part=p_main, at=(0.0, 3.5, 0.0), trim="roof_metal", keep=True, name="radar_dome"))
        for sx, sz in ((-4.0, 0.0), (0.0, -4.0), (4.0, 2.0)):
            parts.append(P(box, 1.2, 1.2, 5.5, part=p_main, at=(sx, 3.2, sz), rot=(-35.0, 0.0, 0.0), trim="armor_grey", keep=True, name="launcher"))
        hardpoints.append(Hardpoint(0, p_main, (2.0, 4.0, 0.0)))

    elif id_str == "bld_radar":
        h = 16.0
        rig.set_ops(p_main, "Die", [sink(1, -0.45 * h), tip(2, 3.0)])
        p_dish = rig.add_part("dish", (0.0, 12.5, 0.0), parent=p_main)
        rig.set_ops(p_dish, "Idle", [spin(1, 1)])
        rig.set_ops(p_dish, "Move", [spin(1, 1)])

        parts.append(P(box, 14.0, 4.0, 14.0, part=p_main, at=(0.0, 2.0, 0.0), trim="concrete", keep=True, name="base"))
        parts.append(P(box, 14.2, 1.2, 14.2, part=p_main, at=(0.0, 3.0, 0.0), trim="cloth_tunic", team=True, keep=True, name="team_stripe"))
        parts.append(P(cylinder, 1.2, 0.8, 7.0, part=p_main, at=(0.0, 7.5, 0.0), trim="steel_plain", keep=True, name="mast"))
        parts.append(P(box, 1.2, 2.5, 6.0, part=p_dish, at=(0.0, 12.5, 0.0), trim="hazard_stripe", keep=True, name="dish"))
        # 3 Corner generator cylinders
        for gx, gz in ((-5.0, -5.0), (-5.0, 5.0), (5.0, -5.0)):
            parts.append(P(cylinder, 0.8, 0.8, 2.0, part=p_main, at=(gx, 5.0, gz), trim="steel_worn", keep=False, name="gen"))

    elif id_str == "bld_silo":
        h = 15.0
        rig.set_ops(p_main, "Die", [sink(1, -0.45 * h), tip(2, 3.0)])
        parts.append(P(cylinder, 3.5, 3.5, 11.2, part=p_main, at=(-3.8, 5.6, 0.0), trim="concrete", keep=True, name="silo1"))
        parts.append(P(cylinder, 3.5, 3.5, 11.2, part=p_main, at=(3.8, 5.6, 0.0), trim="concrete", keep=True, name="silo2"))
        # Lift domes so they do not share circular seam
        parts.append(P(sphere, 3.5, part=p_main, at=(-3.8, 12.2, 0.0), trim="roof_metal", keep=True, name="dome1"))
        parts.append(P(sphere, 3.5, part=p_main, at=(3.8, 12.2, 0.0), trim="roof_metal", keep=True, name="dome2"))
        parts.append(P(box, 14.5, 1.6, 7.2, part=p_main, at=(0.0, 6.0, 0.0), trim="cloth_tunic", team=True, keep=True, name="team_band"))
        parts.append(P(box, 2.0, 0.4, 2.0, part=p_main, at=(0.0, 12.5, 0.0), trim="hazard_stripe", keep=True, name="hatch"))

    elif id_str == "bld_refinery":
        h = 32.0
        rig.set_ops(p_main, "Die", [sink(1, -0.45 * h), tip(2, 3.0)])
        p_pump = rig.add_part("pumpjack", (-6.0, 1.0, 0.0), parent=p_main)
        rig.set_ops(p_pump, "Idle", [sway(2, 20.0, 1)])
        rig.set_ops(p_pump, "Move", [sway(2, 20.0, 1)])

        # Tanks
        parts.append(P(cylinder, 4.5, 4.5, 7.5, part=p_main, at=(8.0, 3.75, -6.5), trim="steel_plain", keep=True, name="tank1"))
        parts.append(P(cylinder, 4.5, 4.5, 7.5, part=p_main, at=(8.0, 3.75, 6.5), trim="steel_plain", keep=True, name="tank2"))
        parts.append(P(cylinder, 1.5, 1.3, 26.0, part=p_main, at=(-8.0, 13.0, -6.0), trim="steel_plain", keep=True, name="column"))
        parts.append(P(cylinder, 1.1, 0.8, 30.0, part=p_main, at=(-8.0, 15.0, 6.0), trim="brick", keep=True, name="stack"))
        parts.append(P(box, 9.2, 1.8, 4.0, part=p_main, at=(8.0, 4.0, 0.0), trim="cloth_tunic", team=True, keep=True, name="team_band"))
        parts.append(P(box, 5.0, 0.8, 0.8, part=p_pump, at=(-6.0, 4.0, 0.0), trim="steel_worn", keep=True, name="beam"))

    return Model(id_str, category=4, target_dim_m=target_dim, parts=parts, rig=rig, hardpoints=hardpoints)
