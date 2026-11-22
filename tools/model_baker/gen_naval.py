"""gen_naval.py - Procedural generators for 6 naval ship models.

Models:
  nav_galley (28m, draft 1.0)
  nav_galleon (45m, draft 4.0)
  nav_ironclad (70m, draft 5.0)
  nav_destroyer (150m, draft 6.0)
  nav_carrier (330m, draft 11.0)
  nav_sub (100m, draft 6.0)
"""

from dataclasses import dataclass
import math
import numpy as np

import params
from prims import box, taper_box, cylinder, sphere, wedge, prism, Piece
from meshbuilder import Part, place
from rig import Rig
from anim import sway, bob, pulse, kick, tip, sink, spin
from model import Model, Hardpoint, P


def make_ship(
    id_str: str,
    target_dim: float,
    kind: str,
    draft: float,
) -> Model:
    """Generate a naval vessel according to spec G4."""
    rig = Rig()
    s = target_dim

    # Rig base setup
    p_hull = rig.add_part("hull", (0.0, 0.0, 0.0), parent=-1)
    rig.set_ops(p_hull, "Idle", [bob(1, 0.12, 1), sway(2, 0.8, 1)])
    rig.set_ops(p_hull, "Move", [bob(1, 0.25, 2), sway(2, 1.5, 2), sway(0, 1.0, 1)])
    rig.set_ops(p_hull, "Die", [tip(2, -18.0), tip(0, 25.0), sink(1, -0.6 * draft)])

    parts: list[Part] = []
    hardpoints: list[Hardpoint] = []

    if kind == "galley":
        length = s * 0.96
        beam = s * 0.18
        freeboard = 1.4
        h = draft + freeboard
        y_mid = (-draft + freeboard) * 0.5

        # Hull prism
        x0 = -length * 0.5
        x1 = length * 0.5
        b = beam * 0.5
        xb = x1 - 0.28 * length
        poly = [(x0, -0.85 * b), (xb, -b), (x1, 0.0), (xb, b), (x0, 0.85 * b)]
        parts.append(P(
            prism, poly, h,
            part=p_hull, at=(0.0, y_mid, 0.0), trim="wood_log", keep=True, name="hull"
        ))
        # Team stripe along gunwale
        parts.append(P(
            box, length * 0.80, 0.25, beam * 1.02,
            part=p_hull, at=(0.0, freeboard - 0.15, 0.0),
            trim="cloth_tunic", team=True, keep=True, name="team_stripe"
        ))
        # Deck
        parts.append(P(
            box, length * 0.75, 0.10, beam * 0.80,
            part=p_hull, at=(-length * 0.05, freeboard - 0.1, 0.0),
            trim="wood_planks", keep=True, name="deck"
        ))
        # Mast (static)
        parts.append(P(
            cylinder, 0.2, 0.15, 8.0,
            part=p_hull, at=(0.0, freeboard + 4.0, 0.0),
            trim="wood_log", keep=True, name="mast"
        ))
        # Oars: left (part 1) and right (part 2)
        p_oars_l = rig.add_part("oars_left", (0.0, freeboard, b), parent=p_hull)
        p_oars_r = rig.add_part("oars_right", (0.0, freeboard, -b), parent=p_hull)
        rig.set_ops(p_oars_l, "Move", [sway(1, 18.0, 3)])
        rig.set_ops(p_oars_r, "Move", [sway(1, -18.0, 3)])

        # 6 oars on each side
        for i in range(6):
            ox = -length * 0.25 + i * (length * 0.10)
            parts.append(P(
                cylinder, 0.05, 0.03, 3.5,
                part=p_oars_l, at=(ox, freeboard - 0.2, b + 1.2), rot=(0.0, 0.0, -35.0),
                trim="wood_planks", keep=False, name=f"oar_l_{i}"
            ))
            parts.append(P(
                cylinder, 0.05, 0.03, 3.5,
                part=p_oars_r, at=(ox, freeboard - 0.2, -b - 1.2), rot=(0.0, 0.0, 35.0),
                trim="wood_planks", keep=False, name=f"oar_r_{i}"
            ))

        # Bow ram (Muzzle)
        ram_x = x1 + 1.2
        parts.append(P(
            cone := cylinder, 0.4, 0.0, 1.5,
            part=p_hull, at=(x1 + 0.75, 0.0, 0.0), rot=(0.0, 0.0, -90.0),
            trim="steel_worn", keep=True, name="ram"
        ))
        hardpoints.append(Hardpoint(0, p_hull, (ram_x, 0.0, 0.0)))

    elif kind == "galleon":
        length = s * 0.96
        beam = s * 0.22
        freeboard = 4.5
        h = draft + freeboard
        y_mid = (-draft + freeboard) * 0.5

        # Hull prism
        x0 = -length * 0.5
        x1 = length * 0.5
        b = beam * 0.5
        xb = x1 - 0.28 * length
        poly = [(x0, -0.85 * b), (xb, -b), (x1, 0.0), (xb, b), (x0, 0.85 * b)]
        parts.append(P(
            prism, poly, h,
            part=p_hull, at=(0.0, y_mid, 0.0), trim="wood_log", keep=True, name="hull"
        ))
        # Team stripe
        parts.append(P(
            box, length * 0.85, 0.60, beam * 1.02,
            part=p_hull, at=(0.0, freeboard - 0.5, 0.0),
            trim="cloth_tunic", team=True, keep=True, name="team_stripe"
        ))
        # Superstructure / Poop deck & Forecastle
        parts.append(P(
            box, length * 0.28, 2.5, beam * 0.85,
            part=p_hull, at=(-length * 0.35, freeboard + 1.25, 0.0),
            trim="wood_planks", keep=True, name="poop_deck"
        ))
        parts.append(P(
            box, length * 0.18, 1.8, beam * 0.75,
            part=p_hull, at=(length * 0.32, freeboard + 0.9, 0.0),
            trim="wood_planks", keep=True, name="forecastle"
        ))
        # 2 Turrets (Cannon batteries)
        p_turr_a = rig.add_part("turret_a", (length * 0.15, freeboard + 0.8, 0.0), parent=p_hull)
        p_turr_b = rig.add_part("turret_b", (-length * 0.10, freeboard + 0.8, 0.0), parent=p_hull)
        rig.set_ops(p_turr_a, "Idle", [sway(1, 5.0, 1)])
        rig.set_ops(p_turr_b, "Idle", [sway(1, -5.0, 1)])
        rig.set_ops(p_turr_a, "Attack", [kick(0, -1.2)])
        rig.set_ops(p_turr_b, "Attack", [kick(0, -1.2)])

        for p_t, pos in ((p_turr_a, (length * 0.15, freeboard + 0.8, 0.0)), (p_turr_b, (-length * 0.10, freeboard + 0.8, 0.0))):
            parts.append(P(
                cylinder, 1.2, 1.2, 0.6,
                part=p_t, at=pos, trim="steel_worn", keep=True, name="turret_base"
            ))
            parts.append(P(
                cylinder, 0.18, 0.15, 2.8,
                part=p_t, at=(pos[0] + 1.4, pos[1] + 0.2, pos[2]), rot=(0.0, 0.0, -90.0),
                trim="steel_worn", keep=True, name="barrel"
            ))

        # Sails (Part 3)
        p_sails = rig.add_part("sails", (0.0, freeboard + 6.0, 0.0), parent=p_hull)
        rig.set_ops(p_sails, "Idle", [sway(2, 1.5, 1)])

        # 3 Masts and Sails
        for mx in (-length * 0.20, length * 0.05, length * 0.25):
            parts.append(P(
                cylinder, 0.35, 0.20, 14.0,
                part=p_hull, at=(mx, freeboard + 7.0, 0.0),
                trim="wood_log", keep=True, name="mast"
            ))
            # Sail cloth
            parts.append(P(
                box, 0.10, 8.0, beam * 0.90,
                part=p_sails, at=(mx, freeboard + 7.0, 0.0),
                trim="cloth_dark", keep=True, name="sail"
            ))
        hardpoints.append(Hardpoint(0, p_turr_a, (length * 0.15 + 2.8, freeboard + 1.0, 0.0)))

    elif kind == "ironclad":
        length = s * 0.96
        beam = s * 0.20
        freeboard = 3.2
        h = draft + freeboard
        y_mid = (-draft + freeboard) * 0.5

        # Hull prism
        x0 = -length * 0.5
        x1 = length * 0.5
        b = beam * 0.5
        xb = x1 - 0.28 * length
        poly = [(x0, -0.85 * b), (xb, -b), (x1, 0.0), (xb, b), (x0, 0.85 * b)]
        parts.append(P(
            prism, poly, h,
            part=p_hull, at=(0.0, y_mid, 0.0), trim="steel_worn", keep=True, name="hull"
        ))
        # Team stripe
        parts.append(P(
            box, length * 0.85, 0.50, beam * 1.02,
            part=p_hull, at=(0.0, freeboard - 0.3, 0.0),
            trim="cloth_tunic", team=True, keep=True, name="team_stripe"
        ))
        # Armored Casemate (taper_box)
        parts.append(P(
            taper_box, length * 0.55, beam * 0.85, length * 0.45, beam * 0.65, 2.8,
            part=p_hull, at=(0.0, freeboard + 1.4, 0.0),
            trim="armor_grey", keep=True, name="casemate"
        ))
        # Smokestack
        parts.append(P(
            cylinder, 0.8, 0.8, 5.0,
            part=p_hull, at=(-length * 0.05, freeboard + 4.5, 0.0),
            trim="steel_plain", keep=True, name="smokestack"
        ))
        # Turret A & B
        p_turr_a = rig.add_part("turret_a", (length * 0.20, freeboard + 2.8, 0.0), parent=p_hull)
        p_turr_b = rig.add_part("turret_b", (-length * 0.20, freeboard + 2.8, 0.0), parent=p_hull)
        rig.set_ops(p_turr_a, "Idle", [sway(1, 5.0, 1)])
        rig.set_ops(p_turr_b, "Idle", [sway(1, -5.0, 1)])
        rig.set_ops(p_turr_a, "Attack", [kick(0, -1.2)])
        rig.set_ops(p_turr_b, "Attack", [kick(0, -1.2)])

        for p_t, pos, name in ((p_turr_a, (length * 0.20, freeboard + 2.8, 0.0), "fwd"), (p_turr_b, (-length * 0.20, freeboard + 2.8, 0.0), "aft")):
            parts.append(P(
                cylinder, 2.0, 1.8, 1.2,
                part=p_t, at=pos, trim="armor_grey", keep=True, name=f"turret_{name}"
            ))
            parts.append(P(
                cylinder, 0.25, 0.22, 4.5,
                part=p_t, at=(pos[0] + 2.2, pos[1] + 0.3, pos[2]), rot=(0.0, 0.0, -90.0),
                trim="steel_worn", keep=True, name=f"barrel_{name}"
            ))
        hardpoints.append(Hardpoint(0, p_turr_a, (length * 0.20 + 4.5, freeboard + 3.1, 0.0)))

    elif kind == "destroyer":
        length = s * 0.98
        beam = s * 0.11
        freeboard = 6.5
        h = draft + freeboard
        y_mid = (-draft + freeboard) * 0.5

        # Hull prism
        x0 = -length * 0.5
        x1 = length * 0.5
        b = beam * 0.5
        xb = x1 - 0.28 * length
        poly = [(x0, -0.85 * b), (xb, -b), (x1, 0.0), (xb, b), (x0, 0.85 * b)]
        parts.append(P(
            prism, poly, h,
            part=p_hull, at=(0.0, y_mid, 0.0), trim="armor_grey", keep=True, name="hull"
        ))
        # Team stripe
        parts.append(P(
            box, length * 0.85, 0.70, beam * 1.02,
            part=p_hull, at=(0.0, freeboard - 0.4, 0.0),
            trim="cloth_tunic", team=True, keep=True, name="team_stripe"
        ))
        # Superstructure / Bridge
        parts.append(P(
            taper_box, length * 0.35, beam * 0.80, length * 0.30, beam * 0.65, 5.0,
            part=p_hull, at=(length * 0.08, freeboard + 2.5, 0.0),
            trim="armor_grey", keep=True, name="bridge"
        ))
        # Bridge windows
        parts.append(P(
            box, 2.0, 1.2, beam * 0.66,
            part=p_hull, at=(length * 0.20, freeboard + 4.2, 0.0),
            trim="glass_window", keep=True, name="bridge_glass"
        ))
        # Funnel / exhaust
        parts.append(P(
            cylinder, 1.8, 1.4, 6.0,
            part=p_hull, at=(-length * 0.06, freeboard + 5.0, 0.0),
            trim="steel_worn", keep=True, name="funnel"
        ))
        # Radar mast (part 3)
        p_radar = rig.add_part("radar", (length * 0.02, freeboard + 7.5, 0.0), parent=p_hull)
        rig.set_ops(p_radar, "Idle", [spin(1, 1)])
        rig.set_ops(p_radar, "Move", [spin(1, 1)])
        parts.append(P(
            cylinder, 0.4, 0.2, 7.0,
            part=p_hull, at=(length * 0.02, freeboard + 6.0, 0.0),
            trim="steel_plain", keep=True, name="mast"
        ))
        parts.append(P(
            box, 1.2, 0.6, 3.8,
            part=p_radar, at=(length * 0.02, freeboard + 9.5, 0.0),
            trim="hazard_stripe", keep=True, name="radar_dish"
        ))
        # Turrets A & B
        p_turr_a = rig.add_part("turret_a", (length * 0.32, freeboard + 1.2, 0.0), parent=p_hull)
        p_turr_b = rig.add_part("turret_b", (-length * 0.32, freeboard + 1.2, 0.0), parent=p_hull)
        rig.set_ops(p_turr_a, "Idle", [sway(1, 5.0, 1)])
        rig.set_ops(p_turr_b, "Idle", [sway(1, -5.0, 1)])
        rig.set_ops(p_turr_a, "Attack", [kick(0, -1.2)])
        rig.set_ops(p_turr_b, "Attack", [kick(0, -1.2)])

        for p_t, pos, name in ((p_turr_a, (length * 0.32, freeboard + 1.2, 0.0), "fwd"), (p_turr_b, (-length * 0.32, freeboard + 1.2, 0.0), "aft")):
            parts.append(P(
                cylinder, 2.6, 2.2, 1.6,
                part=p_t, at=pos, trim="armor_grey", keep=True, name=f"turret_{name}"
            ))
            parts.append(P(
                cylinder, 0.28, 0.22, 6.0,
                part=p_t, at=(pos[0] + 3.0, pos[1] + 0.3, pos[2]), rot=(0.0, 0.0, -90.0),
                trim="steel_worn", keep=True, name=f"barrel_{name}"
            ))
        hardpoints.append(Hardpoint(0, p_turr_a, (length * 0.32 + 6.0, freeboard + 1.5, 0.0)))

    elif kind == "carrier":
        length = s * 0.98
        beam = s * 0.12
        freeboard = 16.0
        h = draft + freeboard
        y_mid = (-draft + freeboard) * 0.5

        # Hull prism
        x0 = -length * 0.5
        x1 = length * 0.5
        b = beam * 0.5
        xb = x1 - 0.28 * length
        poly = [(x0, -0.85 * b), (xb, -b), (x1, 0.0), (xb, b), (x0, 0.85 * b)]
        parts.append(P(
            prism, poly, h,
            part=p_hull, at=(0.0, y_mid, 0.0), trim="armor_grey", keep=True, name="hull"
        ))
        # Flight deck: giant flat box
        deck_w = beam * 1.75
        parts.append(P(
            box, length * 0.96, 2.0, deck_w,
            part=p_hull, at=(0.0, freeboard + 1.0, 0.0),
            trim="concrete_panel", keep=True, name="flight_deck"
        ))
        # Runway team stripes (widened for team colour share)
        parts.append(P(
            box, length * 0.88, 0.12, 14.0,
            part=p_hull, at=(0.0, freeboard + 2.05, -deck_w * 0.10),
            trim="cloth_tunic", team=True, keep=True, name="runway_stripe"
        ))
        # Island superstructure (starboard +Z)
        parts.append(P(
            taper_box, length * 0.12, 6.0, length * 0.09, 5.0, 8.0,
            part=p_hull, at=(length * 0.05, freeboard + 6.0, deck_w * 0.38),
            trim="armor_grey", keep=True, name="island"
        ))
        # Island radar (part 3)
        p_radar = rig.add_part("radar", (length * 0.05, freeboard + 11.5, deck_w * 0.38), parent=p_hull)
        rig.set_ops(p_radar, "Idle", [spin(1, 1)])
        rig.set_ops(p_radar, "Move", [spin(1, 1)])
        parts.append(P(
            box, 1.5, 0.8, 5.0,
            part=p_radar, at=(length * 0.05, freeboard + 11.5, deck_w * 0.38),
            trim="hazard_stripe", keep=True, name="carrier_radar"
        ))
        # Defense turrets
        p_turr_a = rig.add_part("turret_a", (length * 0.42, freeboard - 1.0, -deck_w * 0.45), parent=p_hull)
        p_turr_b = rig.add_part("turret_b", (-length * 0.42, freeboard - 1.0, deck_w * 0.45), parent=p_hull)
        rig.set_ops(p_turr_a, "Idle", [sway(1, 5.0, 1)])
        rig.set_ops(p_turr_b, "Idle", [sway(1, -5.0, 1)])
        rig.set_ops(p_turr_a, "Attack", [kick(0, -1.2)])
        rig.set_ops(p_turr_b, "Attack", [kick(0, -1.2)])

        for p_t, pos in ((p_turr_a, (length * 0.42, freeboard - 1.0, -deck_w * 0.45)), (p_turr_b, (-length * 0.42, freeboard - 1.0, deck_w * 0.45))):
            parts.append(P(
                cylinder, 2.0, 1.8, 1.4,
                part=p_t, at=pos, trim="steel_worn", keep=True, name="ciws_mount"
            ))
            parts.append(P(
                cylinder, 0.25, 0.25, 3.0,
                part=p_t, at=(pos[0] + 1.5, pos[1] + 0.3, pos[2]), rot=(0.0, 0.0, -90.0),
                trim="steel_worn", keep=True, name="ciws_barrel"
            ))
        hardpoints.append(Hardpoint(0, p_turr_a, (length * 0.42 + 3.0, freeboard - 0.7, -deck_w * 0.45)))

    elif kind == "sub":
        # Submarine: cylinder along X, cone ends as parts 1 & 2, sail = box, screw as part 3
        length = s * 0.96
        dia = s * 0.10
        r = dia * 0.5
        fuse_len = length * 0.75
        y_center = -1.5

        p_bow = rig.add_part("bow", (fuse_len * 0.5, y_center, 0.0), parent=p_hull)
        p_stern = rig.add_part("stern", (-fuse_len * 0.5, y_center, 0.0), parent=p_hull)
        p_screw = rig.add_part("screw", (-length * 0.49, y_center, 0.0), parent=p_hull)

        for clip in ("Idle", "Move", "Attack", "Die"):
            rig.set_ops(p_screw, clip, [spin(0, 6)])

        parts.append(P(
            cylinder, r, r, fuse_len,
            part=p_hull, at=(0.0, y_center, 0.0), rot=(0.0, 0.0, -90.0),
            trim="armor_grey", keep=True, name="hull_cyl"
        ))
        # Bow cone (part 1)
        cone_len = length * 0.125
        parts.append(P(
            cylinder, r, 0.0, cone_len,
            part=p_bow, at=(fuse_len * 0.5 + cone_len * 0.5, y_center, 0.0), rot=(0.0, 0.0, -90.0),
            trim="armor_grey", keep=True, name="bow_cone"
        ))
        # Stern cone (part 2)
        parts.append(P(
            cylinder, 0.0, r, cone_len,
            part=p_stern, at=(-fuse_len * 0.5 - cone_len * 0.5, y_center, 0.0), rot=(0.0, 0.0, -90.0),
            trim="armor_grey", keep=True, name="stern_cone"
        ))
        # Conning tower / sail
        sail_h = r * 0.9
        parts.append(P(
            taper_box, length * 0.12, r * 0.5, length * 0.09, r * 0.4, sail_h,
            part=p_hull, at=(length * 0.08, y_center + r + sail_h * 0.5, 0.0),
            trim="armor_grey", keep=True, name="sail"
        ))
        # Team stripe on sail & upper deck
        parts.append(P(
            box, length * 0.45, 0.4, r * 0.85,
            part=p_hull, at=(length * 0.05, y_center + r + 0.2, 0.0),
            trim="cloth_tunic", team=True, keep=True, name="team_stripe"
        ))
        # Stern dive planes
        parts.append(P(
            box, length * 0.06, 0.15, r * 2.8,
            part=p_hull, at=(-fuse_len * 0.45, y_center, 0.0),
            trim="steel_worn", keep=True, name="stern_planes"
        ))
        # Sail periscopes / masts
        parts.append(P(
            cylinder, 0.12, 0.10, 2.5,
            part=p_hull, at=(length * 0.09, y_center + r + sail_h + 1.25, 0.0),
            trim="steel_plain", keep=False, name="periscope"
        ))
        # Screw hub + 4 blades (part 3)
        parts.append(P(
            cylinder, 0.4, 0.2, 0.8,
            part=p_screw, at=(-length * 0.49, y_center, 0.0), rot=(0.0, 0.0, -90.0),
            trim="steel_worn", keep=True, name="screw_hub"
        ))
        parts.append(P(
            box, 0.08, 2.2, 0.35,
            part=p_screw, at=(-length * 0.49, y_center, 0.0), rot=(25.0, 0.0, 0.0),
            trim="steel_worn", keep=True, name="blade1"
        ))
        parts.append(P(
            box, 0.08, 0.35, 2.2,
            part=p_screw, at=(-length * 0.49, y_center, 0.0), rot=(0.0, 25.0, 0.0),
            trim="steel_worn", keep=True, name="blade2"
        ))
        # Bow torpedo tube (Muzzle)
        hardpoints.append(Hardpoint(0, p_hull, (length * 0.50, y_center, 0.0)))

    return Model(id_str, category=3, target_dim_m=target_dim, parts=parts, rig=rig, hardpoints=hardpoints)
