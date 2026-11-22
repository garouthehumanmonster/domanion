"""gen_air.py - Procedural generators for 7 aircraft models.

Models:
  air_biplane (8.0m)
  air_prop (9.5m)
  air_jet (16.0m)
  air_stealth (12.0m)
  air_bomber (40.0m)
  air_helo (16.0m)
  air_fpv (0.6m)
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


def make_fixed_wing(
    id_str: str,
    target_dim: float,
    kind: str,
    body_trim: str = "camo_woodland",
    wing_trim: str = "camo_woodland",
) -> Model:
    """Generate fixed wing aircraft (biplane, prop, jet, stealth, bomber)."""
    rig = Rig()
    p_body = rig.add_part("body", (0.0, 0.0, 0.0), parent=-1)
    has_prop = kind in ("biplane", "prop")
    p_prop = -1
    if has_prop:
        p_prop = rig.add_part("propeller", (target_dim * 0.48, 0.0, 0.0), parent=p_body)

    # Standard flight animations
    rig.set_ops(p_body, "Idle", [bob(1, 0.02, 1), sway(0, 0.5, 1)])
    rig.set_ops(p_body, "Move", [sway(0, 3.0, 1), bob(1, 0.1, 2)])
    rig.set_ops(p_body, "Attack", [kick(0, -0.05)])
    rig.set_ops(p_body, "Die", [tip(0, 100.0), tip(2, -50.0), sink(1, -2.0)])

    if has_prop:
        for clip in ("Idle", "Move", "Attack", "Die"):
            rig.set_ops(p_prop, clip, [spin(0, 4)])

    parts: list[Part] = []
    hardpoints: list[Hardpoint] = []

    s = target_dim

    if kind == "biplane":
        # Fuselage
        fuse_len = s * 0.90
        fuse_r = s * 0.075
        parts.append(P(
            cylinder, fuse_r * 0.8, fuse_r * 1.1, fuse_len,
            part=p_body, at=(0.0, 0.0, 0.0), rot=(0.0, 0.0, -90.0),
            trim=body_trim, keep=True, name="fuselage"
        ))
        # Lower wings (taper_box mirrored)
        w_span = s * 0.85
        parts.append(P(
            box, s * 0.18, s * 0.02, w_span,
            part=p_body, at=(s * 0.05, -fuse_r * 0.5, 0.0),
            trim=wing_trim, keep=True, name="wing_lower"
        ))
        # Upper wings
        parts.append(P(
            box, s * 0.18, s * 0.02, w_span,
            part=p_body, at=(s * 0.08, fuse_r * 1.3, 0.0),
            trim=wing_trim, keep=True, name="wing_upper"
        ))
        # Wing team stripes
        parts.append(P(
            box, s * 0.185, s * 0.025, w_span * 0.25,
            part=p_body, at=(s * 0.08, fuse_r * 1.3, 0.0),
            trim="cloth_tunic", team=True, keep=True, name="team_stripe"
        ))
        # Tail fin (vertical)
        parts.append(P(
            wedge, s * 0.18, s * 0.18, s * 0.025,
            part=p_body, at=(-fuse_len * 0.45, fuse_r * 1.2, 0.0), rot=(0.0, 0.0, 180.0),
            trim="cloth_tunic", team=True, keep=True, name="tail_fin"
        ))
        # Horizontal stabilizer
        parts.append(P(
            box, s * 0.15, s * 0.02, s * 0.35,
            part=p_body, at=(-fuse_len * 0.42, 0.0, 0.0),
            trim=wing_trim, keep=True, name="tail_h"
        ))
        # Propeller blades
        parts.append(P(
            box, s * 0.015, s * 0.28, s * 0.04,
            part=p_prop, at=(fuse_len * 0.50 + s * 0.01, 0.0, 0.0),
            trim="steel_worn", keep=True, name="prop"
        ))
        hardpoints.append(Hardpoint(0, p_body, (fuse_len * 0.50, 0.0, 0.0)))

    elif kind == "prop":
        fuse_len = s * 0.95
        fuse_r = s * 0.085
        # Fuselage
        parts.append(P(
            cylinder, fuse_r * 0.6, fuse_r * 1.1, fuse_len,
            part=p_body, at=(0.0, 0.0, 0.0), rot=(0.0, 0.0, -90.0),
            trim=body_trim, keep=True, name="fuselage"
        ))
        # Wings starting from fuselage side
        wing_half = (s * 0.92 - fuse_r * 2.0) * 0.5
        parts.append(P(
            taper_box, s * 0.24, s * 0.03, s * 0.12, s * 0.02, wing_half,
            part=p_body, at=(s * 0.05, -fuse_r * 0.3, fuse_r + wing_half * 0.5), rot=(90.0, 0.0, 0.0),
            mirror=True, trim=wing_trim, keep=True, name="wings"
        ))
        # Team stripe on wings
        parts.append(P(
            box, s * 0.18, s * 0.035, wing_half * 0.35,
            part=p_body, at=(s * 0.05, -fuse_r * 0.3, fuse_r + wing_half * 0.3), mirror=True,
            trim="cloth_tunic", team=True, keep=True, name="team_wing"
        ))
        # Canopy
        parts.append(P(
            box, s * 0.22, s * 0.08, s * 0.10,
            part=p_body, at=(s * 0.05, fuse_r * 1.1, 0.0),
            trim="glass_window", keep=True, name="canopy"
        ))
        # Tail
        parts.append(P(
            wedge, s * 0.20, s * 0.22, s * 0.03,
            part=p_body, at=(-fuse_len * 0.45, fuse_r * 1.2, 0.0), rot=(0.0, 0.0, 180.0),
            trim="cloth_tunic", team=True, keep=True, name="tail_fin"
        ))
        parts.append(P(
            box, s * 0.16, s * 0.025, s * 0.38,
            part=p_body, at=(-fuse_len * 0.42, 0.0, 0.0),
            trim=wing_trim, keep=True, name="tail_h"
        ))
        # Propeller
        parts.append(P(
            box, s * 0.02, s * 0.32, s * 0.04,
            part=p_prop, at=(fuse_len * 0.50 + s * 0.01, 0.0, 0.0),
            trim="steel_worn", keep=True, name="prop"
        ))
        hardpoints.append(Hardpoint(0, p_body, (fuse_len * 0.50, 0.0, 0.0)))

    elif kind == "jet":
        p_pods = rig.add_part("pods", (0.0, 0.0, 0.0), parent=p_body)
        fuse_len = s * 0.98
        fuse_r = s * 0.07
        # Fuselage
        parts.append(P(
            cylinder, fuse_r * 0.7, fuse_r * 1.1, fuse_len * 0.8,
            part=p_body, at=(-fuse_len * 0.1, 0.0, 0.0), rot=(0.0, 0.0, -90.0),
            trim=body_trim, keep=True, name="fuselage"
        ))
        # Radome nose cone
        parts.append(P(
            cylinder, fuse_r * 1.1, 0.0, fuse_len * 0.25,
            part=p_body, at=(fuse_len * 0.40, 0.0, 0.0), rot=(0.0, 0.0, -90.0),
            trim=body_trim, keep=True, name="nose_cone"
        ))
        # Swept delta wings starting from fuselage side
        wing_half = (s * 0.72 - fuse_r * 2.0) * 0.5
        parts.append(P(
            taper_box, s * 0.35, s * 0.03, s * 0.14, s * 0.015, wing_half, dx=-s * 0.08,
            part=p_body, at=(-s * 0.05, 0.0, fuse_r + wing_half * 0.5), rot=(90.0, 0.0, 0.0),
            mirror=True, trim=wing_trim, keep=True, name="wings"
        ))
        # Team vertical canted fins
        parts.append(P(
            wedge, s * 0.22, s * 0.18, s * 0.03,
            part=p_body, at=(-fuse_len * 0.35, fuse_r * 1.2, fuse_r * 1.1),
            rot=(15.0, 0.0, 180.0), mirror=True, trim="cloth_tunic", team=True, keep=True, name="tail_fins"
        ))
        # Canopy
        parts.append(P(
            box, s * 0.20, s * 0.06, s * 0.08,
            part=p_body, at=(s * 0.18, fuse_r * 1.0, 0.0),
            trim="glass_window", keep=True, name="canopy"
        ))
        # Engine exhaust
        parts.append(P(
            cylinder, fuse_r * 0.65, fuse_r * 0.65, s * 0.06,
            part=p_body, at=(-fuse_len * 0.50, 0.0, 0.0), rot=(0.0, 0.0, -90.0),
            trim="steel_worn", keep=True, name="exhaust"
        ))
        # Wingtip missile rails (drops on LOD2 to satisfy LOD ratio)
        parts.append(P(
            box, s * 0.14, s * 0.02, s * 0.02,
            part=p_pods, at=(-s * 0.05, 0.0, fuse_r + wing_half), mirror=True,
            trim="steel_worn", keep=False, name="missile_rail"
        ))
        hardpoints.append(Hardpoint(0, p_body, (fuse_len * 0.48, -0.05 * s, 0.0)))

    elif kind == "stealth":
        # Flying wing faceted diamond planform
        x_nose = s * 0.48
        x_tip = -s * 0.05
        x_tail = -s * 0.48
        b = s * 0.48
        poly = [(x_tail, 0.0), (x_tip, -b), (x_nose, 0.0), (x_tip, b)]
        parts.append(P(
            prism, poly, s * 0.06,
            part=p_body, at=(0.0, 0.0, 0.0), trim=body_trim, keep=True, name="wing_body"
        ))
        # Center dorsal spine / cockpit
        parts.append(P(
            taper_box, s * 0.40, s * 0.18, s * 0.20, s * 0.10, s * 0.06, dx=s * 0.05,
            part=p_body, at=(s * 0.05, s * 0.05, 0.0), trim="armor_grey", keep=True, name="cockpit"
        ))
        # Canopy
        parts.append(P(
            box, s * 0.16, s * 0.04, s * 0.08,
            part=p_body, at=(s * 0.12, s * 0.09, 0.0), trim="glass_window", keep=True, name="canopy"
        ))
        # Twin canted stealth tailfins (small)
        parts.append(P(
            wedge, s * 0.14, s * 0.12, s * 0.02,
            part=p_body, at=(-s * 0.25, s * 0.08, s * 0.12), rot=(20.0, 0.0, 180.0),
            mirror=True, trim="cloth_tunic", team=True, keep=True, name="stealth_fins"
        ))
        # Engine intake pods
        parts.append(P(
            box, s * 0.22, s * 0.04, s * 0.08,
            part=p_body, at=(-s * 0.05, s * 0.04, s * 0.14), mirror=True,
            trim="armor_grey", keep=False, name="intake_pods"
        ))
        # Team stripes on outer wingtips
        parts.append(P(
            box, s * 0.12, s * 0.065, s * 0.16,
            part=p_body, at=(x_tip, 0.0, b * 0.75), mirror=True,
            trim="cloth_tunic", team=True, keep=True, name="wingtip_team"
        ))
        hardpoints.append(Hardpoint(0, p_body, (s * 0.15, -s * 0.04, 0.0)))

    elif kind == "bomber":
        fuse_len = s * 0.98
        fuse_r = s * 0.05
        # Main fuselage
        parts.append(P(
            cylinder, fuse_r * 0.7, fuse_r * 1.1, fuse_len * 0.85,
            part=p_body, at=(-fuse_len * 0.05, 0.0, 0.0), rot=(0.0, 0.0, -90.0),
            trim=body_trim, keep=True, name="fuselage"
        ))
        # Nose cone
        parts.append(P(
            cylinder, fuse_r * 1.1, 0.0, fuse_len * 0.15,
            part=p_body, at=(fuse_len * 0.42, 0.0, 0.0), rot=(0.0, 0.0, -90.0),
            trim=body_trim, keep=True, name="nose"
        ))
        # Giant wings starting from fuselage side
        wing_half = (s * 0.92 - fuse_r * 2.0) * 0.5
        parts.append(P(
            taper_box, s * 0.22, s * 0.03, s * 0.08, s * 0.015, wing_half, dx=-s * 0.05,
            part=p_body, at=(s * 0.05, 0.0, fuse_r + wing_half * 0.5), rot=(90.0, 0.0, 0.0),
            mirror=True, trim=wing_trim, keep=True, name="wings"
        ))
        # 4 Jet nacelles (2 per side)
        for z_off in (fuse_r + wing_half * 0.35, fuse_r + wing_half * 0.70):
            parts.append(P(
                cylinder, s * 0.02, s * 0.02, s * 0.12,
                part=p_body, at=(s * 0.02, -fuse_r * 0.8, z_off), rot=(0.0, 0.0, -90.0),
                mirror=True, trim="steel_worn", keep=False, name="nacelle"
            ))
        # Team tail fin
        parts.append(P(
            wedge, s * 0.20, s * 0.20, s * 0.03,
            part=p_body, at=(-fuse_len * 0.44, fuse_r * 1.4, 0.0), rot=(0.0, 0.0, 180.0),
            trim="cloth_tunic", team=True, keep=True, name="tail_fin"
        ))
        # Tail horizontal
        parts.append(P(
            box, s * 0.14, s * 0.02, s * 0.32,
            part=p_body, at=(-fuse_len * 0.42, 0.0, 0.0),
            trim=wing_trim, keep=True, name="tail_h"
        ))
        hardpoints.append(Hardpoint(0, p_body, (s * 0.10, -fuse_r * 1.1, 0.0)))

    return Model(id_str, category=2, target_dim_m=target_dim, parts=parts, rig=rig, hardpoints=hardpoints)


def make_rotary(id_str: str, target_dim: float, kind: str) -> Model:
    """Generate rotary wing aircraft (helo, quad/fpv)."""
    rig = Rig()
    s = target_dim

    if kind == "helo":
        p_fuse = rig.add_part("fuselage", (0.0, 0.0, 0.0), parent=-1)
        p_gear = rig.add_part("gear", (s * 0.05, -s * 0.10, 0.0), parent=p_fuse)
        p_main = rig.add_part("main_rotor", (s * 0.05, s * 0.14, 0.0), parent=p_fuse)
        p_tail = rig.add_part("tail_rotor", (-s * 0.46, s * 0.10, s * 0.03), parent=p_fuse)

        # Helo anims
        for clip in ("Idle", "Move", "Attack", "Die"):
            rig.set_ops(p_main, clip, [spin(1, 6)])
            rig.set_ops(p_tail, clip, [spin(2, 8)])

        rig.set_ops(p_fuse, "Idle", [bob(1, 0.02, 1)])
        rig.set_ops(p_fuse, "Move", [sway(2, -5.0, 1), bob(1, 0.05, 2)])
        rig.set_ops(p_fuse, "Attack", [pulse(2, -6.0)])
        rig.set_ops(p_fuse, "Die", [tip(0, 70.0), tip(2, -30.0), sink(1, -2.0)])

        parts: list[Part] = []
        fuse_len = s * 0.88
        body_r = s * 0.09
        # Cabin
        parts.append(P(
            taper_box, s * 0.45, s * 0.18, s * 0.35, s * 0.14, s * 0.22, dx=s * 0.02,
            part=p_fuse, at=(s * 0.05, 0.0, 0.0), trim="armor_olive", keep=True, name="cabin"
        ))
        # Stub wings with rocket pods
        p_pods = rig.add_part("pods", (s * 0.08, -s * 0.02, 0.0), parent=p_fuse)
        parts.append(P(
            box, s * 0.12, s * 0.02, s * 0.42,
            part=p_pods, at=(s * 0.08, -s * 0.02, 0.0), trim="armor_olive",
            keep=True, name="stub_wings"
        ))
        parts.append(P(
            cylinder, s * 0.025, s * 0.025, s * 0.18,
            part=p_pods, at=(s * 0.08, -s * 0.04, s * 0.16), rot=(0.0, 0.0, -90.0),
            mirror=True, trim="steel_worn", keep=False, name="rocket_pods"
        ))
        # Tail boom
        parts.append(P(
            cylinder, body_r * 0.35, body_r * 0.8, fuse_len * 0.50,
            part=p_fuse, at=(-fuse_len * 0.26, s * 0.04, 0.0), rot=(0.0, 0.0, 90.0),
            trim="armor_olive", keep=True, name="tail_boom"
        ))
        # Team stripe on tail boom
        parts.append(P(
            box, s * 0.08, s * 0.12, s * 0.12,
            part=p_fuse, at=(-fuse_len * 0.32, s * 0.04, 0.0),
            trim="cloth_tunic", team=True, keep=True, name="tail_team"
        ))
        # Vertical tail fin
        parts.append(P(
            wedge, s * 0.14, s * 0.16, s * 0.02,
            part=p_fuse, at=(-fuse_len * 0.48, s * 0.10, 0.0), rot=(0.0, 0.0, 180.0),
            trim="cloth_tunic", team=True, keep=False, name="tail_fin"
        ))
        # Skids (left and right) - drops on LOD1/2
        parts.append(P(
            box, s * 0.45, s * 0.02, s * 0.02,
            part=p_gear, at=(s * 0.05, -s * 0.14, s * 0.12), mirror=True,
            trim="steel_worn", keep=False, name="skids"
        ))
        # Skid struts
        parts.append(P(
            box, s * 0.02, s * 0.08, s * 0.02,
            part=p_gear, at=(s * 0.15, -s * 0.08, s * 0.12), mirror=True,
            trim="steel_worn", keep=False, name="skid_strut1"
        ))
        parts.append(P(
            box, s * 0.02, s * 0.08, s * 0.02,
            part=p_gear, at=(-s * 0.05, -s * 0.08, s * 0.12), mirror=True,
            trim="steel_worn", keep=False, name="skid_strut2"
        ))
        # Main rotor mast + 2 crossed blades
        r_dia = s * 0.86
        parts.append(P(
            cylinder, s * 0.015, s * 0.015, s * 0.06,
            part=p_main, at=(s * 0.05, s * 0.14, 0.0),
            trim="steel_worn", keep=True, name="mast"
        ))
        parts.append(P(
            box, s * 0.035, s * 0.015, r_dia,
            part=p_main, at=(s * 0.05, s * 0.165, 0.0),
            trim="steel_worn", keep=True, name="blade1"
        ))
        parts.append(P(
            box, r_dia, s * 0.015, s * 0.035,
            part=p_main, at=(s * 0.05, s * 0.165, 0.0),
            trim="steel_worn", keep=True, name="blade2"
        ))
        # Tail rotor
        parts.append(P(
            box, s * 0.02, s * 0.18, s * 0.01,
            part=p_tail, at=(-fuse_len * 0.46, s * 0.10, s * 0.03),
            trim="steel_worn", keep=True, name="tail_blade"
        ))
        hardpoints = [Hardpoint(0, p_fuse, (s * 0.32, -s * 0.04, 0.0))]
        return Model(id_str, category=2, target_dim_m=target_dim, parts=parts, rig=rig, hardpoints=hardpoints)

    else:
        # quad / FPV drone (~0.6 m)
        p_body = rig.add_part("body", (0.0, 0.0, 0.0), parent=-1)
        r_dist = s * 0.38
        p_r1 = rig.add_part("rotor1", (r_dist, s * 0.04, r_dist), parent=p_body)
        p_r2 = rig.add_part("rotor2", (r_dist, s * 0.04, -r_dist), parent=p_body)
        p_r3 = rig.add_part("rotor3", (-r_dist, s * 0.04, r_dist), parent=p_body)
        p_r4 = rig.add_part("rotor4", (-r_dist, s * 0.04, -r_dist), parent=p_body)

        for clip in ("Idle", "Move", "Attack", "Die"):
            rig.set_ops(p_r1, clip, [spin(1, 6)])
            rig.set_ops(p_r2, clip, [spin(1, -6)])
            rig.set_ops(p_r3, clip, [spin(1, 6)])
            rig.set_ops(p_r4, clip, [spin(1, -6)])

        rig.set_ops(p_body, "Idle", [bob(1, 0.01, 1)])
        rig.set_ops(p_body, "Move", [sway(2, -8.0, 1)])
        rig.set_ops(p_body, "Die", [tip(0, 90.0), sink(1, -1.0)])

        parts = []
        # Central plate
        parts.append(P(
            box, s * 0.24, s * 0.04, s * 0.24,
            part=p_body, at=(0.0, 0.0, 0.0), trim="armor_grey",
            keep=True, name="frame"
        ))
        # Team sticker
        parts.append(P(
            box, s * 0.12, s * 0.045, s * 0.12,
            part=p_body, at=(0.0, 0.005, 0.0), trim="cloth_tunic", team=True,
            keep=True, name="team"
        ))
        # 4 Arms (X configuration)
        arm_w = s * 0.03
        arm_h = s * 0.02
        diag_len = r_dist * 2.0 * math.sqrt(2)
        parts.append(P(
            box, diag_len, arm_h, arm_w,
            part=p_body, at=(0.0, 0.0, 0.0), rot=(0.0, 45.0, 0.0),
            trim="steel_plain", keep=True, name="arm_diag1"
        ))
        parts.append(P(
            box, diag_len, arm_h, arm_w,
            part=p_body, at=(0.0, 0.0, 0.0), rot=(0.0, -45.0, 0.0),
            trim="steel_plain", keep=True, name="arm_diag2"
        ))
        # 4 Rotors (thin boxes)
        prop_len = s * 0.28
        for r_part, (rx, rz) in zip((p_r1, p_r2, p_r3, p_r4), (
            (r_dist, r_dist), (r_dist, -r_dist),
            (-r_dist, r_dist), (-r_dist, -r_dist)
        )):
            parts.append(P(
                box, prop_len, s * 0.008, s * 0.03,
                part=r_part, at=(rx, s * 0.04, rz), trim="steel_worn", keep=True, name="rotor"
            ))

        # Quad has NO muzzle
        return Model(id_str, category=2, target_dim_m=target_dim, parts=parts, rig=rig, hardpoints=[])
