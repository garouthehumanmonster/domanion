"""gen_ground.py - Procedural generator for 14 vehicle models (mounted, siege, tracked, wheeled)."""

from dataclasses import dataclass
import math
import numpy as np

import params
from prims import box, taper_box, cylinder, sphere, wedge, Piece
from meshbuilder import Part, place
from rig import Rig
from anim import sway, bob, pulse, kick, tip, sink, spin
from model import Model, Hardpoint, P


# 1. MOUNTED GENERATOR
def make_mounted(id_str: str, target_dim: float, kind: str) -> Model:
    """Mounted cavalry (cav_light, cav_heavy)."""
    scale = target_dim / 3.2
    s = scale

    rig = Rig()
    p_body = rig.add_part("body", (0.0, 1.2 * s, 0.0), parent=-1)
    p_legs_f = rig.add_part("legs_front", (0.7 * s, 0.85 * s, 0.0), parent=p_body)
    p_legs_b = rig.add_part("legs_back", (-0.7 * s, 0.85 * s, 0.0), parent=p_body)
    p_rider = rig.add_part("rider", (0.0, 1.45 * s, 0.0), parent=p_body)
    p_weapon = rig.add_part("weapon", (0.4 * s, 1.65 * s, -0.35 * s), parent=p_rider)
    p_head = rig.add_part("head", (0.9 * s, 1.4 * s, 0.0), parent=p_body)

    rig.set_ops(p_body, "Idle", [bob(1, 0.02 * s, 1)])
    rig.set_ops(p_head, "Idle", [sway(2, 2.0, 1)])

    rig.set_ops(p_legs_f, "Move", [sway(2, 28.0, 1)])
    rig.set_ops(p_legs_b, "Move", [sway(2, -28.0, 1)])
    rig.set_ops(p_body, "Move", [bob(1, 0.05 * s, 2)])
    rig.set_ops(p_head, "Move", [sway(2, 6.0, 2)])

    rig.set_ops(p_weapon, "Attack", [kick(0, 0.8 * s)])
    rig.set_ops(p_rider, "Attack", [pulse(2, -10.0)])

    rig.set_ops(p_body, "Die", [tip(0, 90.0), sink(1, -0.1 * s)])

    parts = []
    horse_trim = "leather" if kind == "lance" else "armor_grey"
    parts.append(P(
        box, 2.0 * s, 0.7 * s, 0.55 * s,
        part=p_body, at=(0.0, 1.2 * s, 0.0), trim=horse_trim, keep=True, name="horse_body"
    ))
    parts.append(P(
        box, 0.9 * s, 0.72 * s, 0.57 * s,
        part=p_body, at=(0.0, 1.2 * s, 0.0), trim="cloth_tunic", team=True, keep=True, name="blanket"
    ))

    def make_legs_f(lod: int) -> Piece:
        l1 = box(0.18 * s, 0.85 * s, 0.18 * s)
        l2 = box(0.18 * s, 0.85 * s, 0.18 * s)
        p1 = place(l1, (0.7 * s, 0.425 * s, 0.20 * s))
        p2 = place(l2, (0.7 * s, 0.425 * s, -0.20 * s))
        return Piece(
            np.vstack([p1.pos, p2.pos]), np.vstack([p1.nrm, p2.nrm]),
            np.vstack([p1.uv, p2.uv]), np.vstack([p1.tris, p2.tris + len(p1.pos)])
        )
    parts.append(Part(part=p_legs_f, make=make_legs_f, keep=True, trim=horse_trim, name="legs_f"))

    def make_legs_b(lod: int) -> Piece:
        l1 = box(0.18 * s, 0.85 * s, 0.18 * s)
        l2 = box(0.18 * s, 0.85 * s, 0.18 * s)
        p1 = place(l1, (-0.7 * s, 0.425 * s, 0.20 * s))
        p2 = place(l2, (-0.7 * s, 0.425 * s, -0.20 * s))
        return Piece(
            np.vstack([p1.pos, p2.pos]), np.vstack([p1.nrm, p2.nrm]),
            np.vstack([p1.uv, p2.uv]), np.vstack([p1.tris, p2.tris + len(p1.pos)])
        )
    parts.append(Part(part=p_legs_b, make=make_legs_b, keep=True, trim=horse_trim, name="legs_b"))

    rider_trim = "armor_olive" if kind == "lance" else "steel_plain"
    def make_rider(lod: int) -> Piece:
        torso = taper_box(0.28 * s, 0.40 * s, 0.26 * s, 0.44 * s, 0.55 * s)
        placed_t = place(torso, (0.0, 1.825 * s, 0.0))
        hd = sphere(0.10 * s, lod=lod)
        placed_h = place(hd, (0.0, 2.22 * s, 0.0))
        return Piece(
            np.vstack([placed_t.pos, placed_h.pos]), np.vstack([placed_t.nrm, placed_h.nrm]),
            np.vstack([placed_t.uv, placed_h.uv]), np.vstack([placed_t.tris, placed_h.tris + len(placed_t.pos)])
        )
    parts.append(Part(part=p_rider, make=make_rider, keep=True, trim=rider_trim, team=False, name="rider"))

    def make_weapon(lod: int) -> Piece:
        if kind == "lance":
            shaft = cylinder(0.03 * s, 0.03 * s, 2.2 * s, lod=lod)
            s_x = place(shaft, (0.6 * s, 0.0, 0.0), rot_deg=(0, 0, 90))
            tip_c = cylinder(0.05 * s, 0.0, 0.3 * s, lod=lod)
            t_x = place(tip_c, (1.85 * s, 0.0, 0.0), rot_deg=(0, 0, -90))
            return Piece(
                np.vstack([s_x.pos, t_x.pos]), np.vstack([s_x.nrm, t_x.nrm]),
                np.vstack([s_x.uv, t_x.uv]), np.vstack([s_x.tris, t_x.tris + len(s_x.pos)])
            )
        else:
            blade = box(0.9 * s, 0.08 * s, 0.02 * s)
            return place(blade, (0.5 * s, 0.0, 0.0))
    parts.append(Part(part=p_weapon, make=make_weapon, pos=(0.4 * s, 1.65 * s, -0.35 * s), keep=True, trim="steel_plain", name="weapon"))

    def make_hhead(lod: int) -> Piece:
        neck = taper_box(0.35 * s, 0.3 * s, 0.25 * s, 0.22 * s, 0.7 * s)
        neck_p = place(neck, (0.0, 0.2 * s, 0.0), rot_deg=(0, 0, -35))
        snout = box(0.4 * s, 0.25 * s, 0.25 * s)
        snout_p = place(snout, (0.35 * s, 0.35 * s, 0.0))
        return Piece(
            np.vstack([neck_p.pos, snout_p.pos]), np.vstack([neck_p.nrm, snout_p.nrm]),
            np.vstack([neck_p.uv, snout_p.uv]), np.vstack([neck_p.tris, neck_p.tris + len(neck_p.pos)])
        )
    parts.append(Part(part=p_head, make=make_hhead, pos=(0.9 * s, 1.4 * s, 0.0), keep=True, trim=horse_trim, name="horse_head"))

    muzzle_x = 2.4 * s if kind == "lance" else 1.35 * s
    hardpoints = [Hardpoint(kind=params.HP_MUZZLE, part=p_weapon, pos=(muzzle_x, 1.65 * s, -0.35 * s))]

    return Model(id=id_str, category=params.CAT_VEHICLE, target_dim_m=target_dim, parts=parts, rig=rig, hardpoints=hardpoints)


# 2. SIEGE GENERATOR
def make_siege(id_str: str, target_dim: float, kind: str) -> Model:
    """Catapult and Cannon."""
    scale = target_dim / (7.0 if kind == "catapult" else 4.5)
    s = scale
    rig = Rig()

    if kind == "catapult":
        p_base = rig.add_part("base", (0.0, 0.5 * s, 0.0), parent=-1)
        p_wheels = rig.add_part("wheels", (0.0, 0.5 * s, 0.0), parent=p_base)
        p_arm = rig.add_part("arm", (-1.2 * s, 2.0 * s, 0.0), parent=p_base)
        p_bucket = rig.add_part("bucket", (2.8 * s, 2.0 * s, 0.0), parent=p_arm)

        rig.set_ops(p_wheels, "Move", [spin(2, 1)])
        rig.set_ops(p_arm, "Attack", [pulse(2, -85.0, peak=0.3)])
        rig.set_ops(p_base, "Die", [tip(2, 8.0), sink(1, -0.2 * s)])

        parts = []
        parts.append(P(
            box, 5.8 * s, 0.7 * s, 2.4 * s,
            part=p_base, at=(0.0, 0.9 * s, 0.0), trim="wood_log", keep=True, name="base"
        ))
        parts.append(P(
            box, 3.2 * s, 0.72 * s, 0.35 * s,
            part=p_base, at=(0.0, 0.9 * s, 0.0), trim="cloth_tunic", team=True, keep=True, name="team_stripe"
        ))

        def make_wheels(lod: int) -> Piece:
            w = cylinder(0.55 * s, 0.55 * s, 0.35 * s, lod=lod)
            w1 = place(w, (2.2 * s, 0.55 * s, 1.4 * s), rot_deg=(90, 0, 0))
            w2 = place(w, (2.2 * s, 0.55 * s, -1.4 * s), rot_deg=(90, 0, 0))
            w3 = place(w, (-2.2 * s, 0.55 * s, 1.4 * s), rot_deg=(90, 0, 0))
            w4 = place(w, (-2.2 * s, 0.55 * s, -1.4 * s), rot_deg=(90, 0, 0))
            all_p = [w1, w2, w3, w4]
            return Piece(
                np.vstack([p.pos for p in all_p]), np.vstack([p.nrm for p in all_p]),
                np.vstack([p.uv for p in all_p]),
                np.vstack([all_p[0].tris, all_p[1].tris + len(w1.pos), all_p[2].tris + len(w1.pos)*2, all_p[3].tris + len(w1.pos)*3])
            )
        parts.append(Part(part=p_wheels, make=make_wheels, keep=True, trim="wood_planks", name="wheels"))

        parts.append(P(
            box, 5.4 * s, 0.3 * s, 0.3 * s,
            part=p_arm, at=(1.2 * s, 2.0 * s, 0.0), trim="wood_log", keep=True, name="arm"
        ))

        parts.append(P(
            box, 0.8 * s, 0.5 * s, 0.8 * s,
            part=p_bucket, at=(3.4 * s, 2.1 * s, 0.0), trim="steel_worn", keep=True, name="bucket"
        ))

        hardpoints = [Hardpoint(kind=params.HP_MUZZLE, part=p_bucket, pos=(3.8 * s, 2.2 * s, 0.0))]
    else:
        # Cannon
        p_base = rig.add_part("carriage", (0.0, 0.5 * s, 0.0), parent=-1)
        p_wheels = rig.add_part("wheels", (0.0, 0.5 * s, 0.0), parent=p_base)
        p_barrel = rig.add_part("barrel", (0.0, 0.9 * s, 0.0), parent=p_base)

        rig.set_ops(p_wheels, "Move", [spin(2, 1)])
        rig.set_ops(p_barrel, "Attack", [kick(0, -0.7 * s)])
        rig.set_ops(p_base, "Die", [tip(2, 8.0), sink(1, -0.2 * s)])

        parts = []
        parts.append(P(
            box, 2.8 * s, 0.5 * s, 1.4 * s,
            part=p_base, at=(0.0, 0.65 * s, 0.0), trim="wood_log", keep=True, name="carriage"
        ))
        parts.append(P(
            box, 1.4 * s, 0.52 * s, 0.3 * s,
            part=p_base, at=(0.0, 0.65 * s, 0.0), trim="cloth_tunic", team=True, keep=True, name="team_stripe"
        ))

        def make_wheels(lod: int) -> Piece:
            w = cylinder(0.55 * s, 0.55 * s, 0.25 * s, lod=lod)
            w1 = place(w, (0.0, 0.55 * s, 0.85 * s), rot_deg=(90, 0, 0))
            w2 = place(w, (0.0, 0.55 * s, -0.85 * s), rot_deg=(90, 0, 0))
            return Piece(
                np.vstack([w1.pos, w2.pos]), np.vstack([w1.nrm, w2.nrm]),
                np.vstack([w1.uv, w2.uv]), np.vstack([w1.tris, w2.tris + len(w1.pos)])
            )
        parts.append(Part(part=p_wheels, make=make_wheels, keep=True, trim="wood_planks", name="wheels"))

        def make_barrel(lod: int) -> Piece:
            b = cylinder(0.24 * s, 0.16 * s, 2.8 * s, lod=lod)
            return place(b, (0.6 * s, 0.95 * s, 0.0), rot_deg=(0, 0, 90))
        parts.append(Part(part=p_barrel, make=make_barrel, keep=True, trim="steel_worn", name="barrel"))

        hardpoints = [Hardpoint(kind=params.HP_MUZZLE, part=p_barrel, pos=(2.1 * s, 0.95 * s, 0.0))]

    return Model(id=id_str, category=params.CAT_VEHICLE, target_dim_m=target_dim, parts=parts, rig=rig, hardpoints=hardpoints)


# 3. TRACKED GENERATOR
@dataclass
class TrackedSpec:
    id: str
    target_dim: float
    hull_dim: tuple[float, float, float]
    turret_dim: tuple[float, float, float]
    barrel_len: float
    barrel_r: float
    trim_hull: str
    has_extras: bool = False


def make_tracked(spec: TrackedSpec) -> Model:
    """Tracked vehicle (tanks, IFV, SP artillery)."""
    s = spec.target_dim / (spec.hull_dim[0] + spec.barrel_len * 0.6)
    hx, hz, hy = spec.hull_dim[0] * s, spec.hull_dim[1] * s, spec.hull_dim[2] * s
    tx, tz, ty = spec.turret_dim[0] * s, spec.turret_dim[1] * s, spec.turret_dim[2] * s

    rig = Rig()
    p_hull = rig.add_part("hull", (0.0, 0.0, 0.0), parent=-1)
    p_turret = rig.add_part("turret", (0.0, hy + 0.3 * s, 0.0), parent=p_hull)
    p_barrel = rig.add_part("barrel", (tx * 0.3, hy + 0.3 * s + ty * 0.5, 0.0), parent=p_turret)
    p_extras = rig.add_part("extras", (0.0, hy + 0.3 * s + ty * 0.5, 0.0), parent=p_turret)

    rig.set_ops(p_hull, "Idle", [bob(1, 0.01 * s, 1)])
    rig.set_ops(p_turret, "Idle", [sway(1, 4.0, 1)])

    rig.set_ops(p_hull, "Move", [bob(1, 0.03 * s, 2), sway(2, 0.6, 2)])
    rig.set_ops(p_barrel, "Attack", [kick(0, -0.5 * s)])
    rig.set_ops(p_hull, "Die", [tip(2, 6.0), sink(1, -0.25 * s)])
    rig.set_ops(p_turret, "Die", [tip(2, 18.0)])

    parts = []
    track_w = 0.55 * s

    # Main hull body
    parts.append(P(
        box, hx, hy, hz - 2 * track_w,
        part=p_hull, at=(0.0, 0.3 * s + hy / 2.0, 0.0), trim=spec.trim_hull, keep=True, name="hull"
    ))
    # Team stripes on flanks
    parts.append(P(
        box, hx * 0.7, hy * 0.25, hz * 0.95,
        part=p_hull, at=(0.0, 0.3 * s + hy * 0.7, 0.0), trim="cloth_tunic", team=True, keep=True, name="team_stripe"
    ))

    # Tracks with LOD-aware road wheels
    def make_tracks(lod: int) -> Piece:
        t_box = box(hx * 1.05, 0.6 * s, track_w)
        t_l = place(t_box, (0.0, 0.3 * s, (hz - track_w) / 2.0))
        t_r = place(t_box, (0.0, 0.3 * s, -(hz - track_w) / 2.0))

        wheels = []
        for i in range(4):
            wx = -hx * 0.38 + i * (hx * 0.76 / 3.0)
            w = cylinder(0.28 * s, 0.28 * s, 0.15 * s, lod=lod)
            w_l = place(w, (wx, 0.3 * s, (hz - track_w) / 2.0 + 0.15 * s), rot_deg=(90, 0, 0))
            w_r = place(w, (wx, 0.3 * s, -(hz - track_w) / 2.0 - 0.15 * s), rot_deg=(90, 0, 0))
            wheels.extend([w_l, w_r])

        all_p = [t_l, t_r] + wheels
        pos = np.vstack([p.pos for p in all_p])
        nrm = np.vstack([p.nrm for p in all_p])
        uv = np.vstack([p.uv for p in all_p])
        all_t = []
        off = 0
        for p in all_p:
            all_t.append(p.tris + off)
            off += len(p.pos)
        return Piece(pos, nrm, uv, np.vstack(all_t))

    parts.append(Part(part=p_hull, make=make_tracks, keep=True, trim="rubber_tread", scroll=True, name="tracks"))

    # Turret
    parts.append(P(
        taper_box, tx, tz, tx * 0.8, tz * 0.8, ty,
        part=p_turret, at=(0.0, 0.3 * s + hy + ty / 2.0, 0.0), trim=spec.trim_hull, keep=True, name="turret"
    ))

    # Barrel
    blen = spec.barrel_len * s
    br = spec.barrel_r * s
    def make_barrel(lod: int) -> Piece:
        cyl = cylinder(br, br * 0.9, blen, lod=lod)
        return place(cyl, (tx * 0.3 + blen / 2.0, 0.3 * s + hy + ty * 0.5, 0.0), rot_deg=(0, 0, 90))
    parts.append(Part(part=p_barrel, make=make_barrel, keep=True, trim="steel_worn", name="barrel"))

    # Extras
    def make_extras(lod: int) -> Piece:
        if spec.has_extras:
            pod_l = box(0.6 * s, 0.35 * s, 0.35 * s)
            p1 = place(pod_l, (-0.2 * s, 0.3 * s + hy + ty * 0.6, tz * 0.45))
            p2 = place(pod_l, (-0.2 * s, 0.3 * s + hy + ty * 0.6, -tz * 0.45))
            return Piece(
                np.vstack([p1.pos, p2.pos]), np.vstack([p1.nrm, p2.nrm]),
                np.vstack([p1.uv, p2.uv]), np.vstack([p1.tris, p2.tris + len(p1.pos)])
            )
        else:
            hatch = cylinder(0.25 * s, 0.25 * s, 0.08 * s, lod=lod)
            return place(hatch, (0.0, 0.3 * s + hy + ty + 0.04 * s, 0.0))
    parts.append(Part(part=p_extras, make=make_extras, keep=False, trim="steel_plain", name="extras"))

    muzzle_x = tx * 0.3 + blen
    hardpoints = [
        Hardpoint(kind=params.HP_MUZZLE, part=p_barrel, pos=(muzzle_x, 0.3 * s + hy + ty * 0.5, 0.0)),
        Hardpoint(kind=params.HP_TURRET_PIVOT, part=p_turret, pos=(0.0, 0.3 * s + hy, 0.0)),
    ]

    return Model(id=spec.id, category=params.CAT_VEHICLE, target_dim_m=spec.target_dim, parts=parts, rig=rig, hardpoints=hardpoints)


# 4. WHEELED GENERATOR
@dataclass
class WheeledSpec:
    id: str
    target_dim: float
    n_axles: int
    payload_kind: str
    trim_chassis: str


def make_wheeled(spec: WheeledSpec) -> Model:
    """Wheeled military vehicle (APC, HIMARS, SAM, EW, Truck, UGV)."""
    scale = spec.target_dim / 8.0
    s = scale
    length = 6.8 * s
    width = 2.4 * s
    height = 1.3 * s

    rig = Rig()
    p_chassis = rig.add_part("chassis", (0.0, 0.0, 0.0), parent=-1)
    p_payload = rig.add_part("payload", (0.0, height + 0.4 * s, 0.0), parent=p_chassis)
    p_payload2 = rig.add_part("payload2", (0.5 * s, height + 0.8 * s, 0.0), parent=p_payload)

    axle_indices = []
    for a in range(spec.n_axles):
        idx = rig.add_part(f"axle_{a}", (0.0, 0.45 * s, 0.0), parent=p_chassis)
        axle_indices.append(idx)
        rig.set_ops(idx, "Move", [spin(2, 1)])

    pk = spec.payload_kind
    if pk == "pods":
        rig.set_ops(p_payload, "Attack", [pulse(2, 55.0)])
    elif pk == "laser_cram":
        rig.set_ops(p_payload2, "Attack", [spin(0, 6)])
    elif pk == "mast":
        rig.set_ops(p_payload, "Idle", [spin(1, 1)])

    rig.set_ops(p_chassis, "Die", [tip(2, 5.0), sink(1, -0.2 * s)])

    parts = []
    parts.append(P(
        box, length, height, width,
        part=p_chassis, at=(0.0, 0.45 * s + height / 2.0, 0.0), trim=spec.trim_chassis, keep=True, name="body"
    ))
    parts.append(P(
        box, length * 0.7, height * 0.3, width * 1.02,
        part=p_chassis, at=(0.0, 0.45 * s + height * 0.6, 0.0), trim="cloth_tunic", team=True, keep=True, name="team_stripe"
    ))

    axle_spacing = (length * 0.75) / max(1, spec.n_axles - 1)
    start_x = (length * 0.75) / 2.0

    for a, ax_part in enumerate(axle_indices):
        ax_x = start_x - a * axle_spacing
        def make_axle_wheels(lod: int, cur_x=ax_x) -> Piece:
            w = cylinder(0.45 * s, 0.45 * s, 0.32 * s, lod=lod)
            w_l = place(w, (cur_x, 0.45 * s, width / 2.0 + 0.16 * s), rot_deg=(90, 0, 0))
            w_r = place(w, (cur_x, 0.45 * s, -(width / 2.0 + 0.16 * s)), rot_deg=(90, 0, 0))
            return Piece(
                np.vstack([w_l.pos, w_r.pos]), np.vstack([w_l.nrm, w_r.nrm]),
                np.vstack([w_l.uv, w_r.uv]), np.vstack([w_l.tris, w_r.tris + len(w_l.pos)])
            )
        parts.append(Part(part=ax_part, make=make_axle_wheels, keep=True, trim="rubber_tread", name=f"wheel_{a}"))

    hardpoints = []

    if pk == "troop":
        parts.append(P(
            cylinder, 0.7 * s, 0.6 * s, 0.5 * s,
            part=p_payload, at=(0.5 * s, 0.45 * s + height + 0.25 * s, 0.0), trim=spec.trim_chassis, keep=True, name="turret"
        ))
        def make_mg(lod: int) -> Piece:
            mg = cylinder(0.04 * s, 0.04 * s, 1.2 * s, lod=lod)
            return place(mg, (1.2 * s, 0.45 * s + height + 0.35 * s, 0.0), rot_deg=(0, 0, 90))
        parts.append(Part(part=p_payload2, make=make_mg, keep=True, trim="steel_plain", name="mg"))
        hardpoints.append(Hardpoint(kind=params.HP_MUZZLE, part=p_payload2, pos=(1.8 * s, 0.45 * s + height + 0.35 * s, 0.0)))

    elif pk == "pods":
        parts.append(P(
            box, 3.2 * s, 1.1 * s, 1.6 * s,
            part=p_payload, at=(-0.5 * s, 0.45 * s + height + 0.65 * s, 0.0), trim="armor_olive", keep=True, name="pod"
        ))
        parts.append(P(
            box, 0.2 * s, 0.2 * s, 0.2 * s,
            part=p_payload2, at=(-0.5 * s, 0.45 * s + height + 1.25 * s, 0.0), trim="steel_plain", keep=False, name="pod_hinge"
        ))
        hardpoints.append(Hardpoint(kind=params.HP_MUZZLE, part=p_payload, pos=(1.2 * s, 0.45 * s + height + 0.65 * s, 0.0)))

    elif pk == "laser_cram":
        parts.append(P(
            sphere, 0.9 * s,
            part=p_payload, at=(-0.2 * s, 0.45 * s + height + 0.7 * s, 0.0), trim="armor_grey", keep=True, name="dome"
        ))
        def make_cram_barrels(lod: int) -> Piece:
            c1 = cylinder(0.04 * s, 0.04 * s, 1.4 * s, lod=lod)
            return place(c1, (0.8 * s, 0.45 * s + height + 0.7 * s, 0.0), rot_deg=(0, 0, 90))
        parts.append(Part(part=p_payload2, make=make_cram_barrels, keep=True, trim="steel_worn", name="gatling"))
        hardpoints.append(Hardpoint(kind=params.HP_MUZZLE, part=p_payload2, pos=(1.5 * s, 0.45 * s + height + 0.7 * s, 0.0)))
        hardpoints.append(Hardpoint(kind=params.HP_LASER, part=p_payload, pos=(0.7 * s, 0.45 * s + height + 0.7 * s, 0.0)))

    elif pk == "mast":
        def make_ew_mast(lod: int) -> Piece:
            mast = cylinder(0.12 * s, 0.08 * s, 2.5 * s, lod=lod)
            p1 = box(0.8 * s, 0.8 * s, 0.05 * s)
            placed_p = place(p1, (0.0, 1.8 * s, 0.0))
            return Piece(
                np.vstack([mast.pos, placed_p.pos]), np.vstack([mast.nrm, placed_p.nrm]),
                np.vstack([mast.uv, placed_p.uv]), np.vstack([mast.tris, placed_p.tris + len(mast.pos)])
            )
        parts.append(Part(part=p_payload, make=make_ew_mast, pos=(0.0, 0.45 * s + height, 0.0), keep=True, trim="steel_plain", name="mast"))
        parts.append(P(box, 0.1 * s, 0.1 * s, 0.1 * s, part=p_payload2, at=(0.0, 0.45 * s + height, 0.0), trim="steel_plain", keep=False, name="mast_base"))

    elif pk == "cargo":
        parts.append(P(
            box, 3.8 * s, 1.6 * s, width * 0.95,
            part=p_payload, at=(-1.0 * s, 0.45 * s + height + 0.85 * s, 0.0), trim="wood_planks", keep=True, name="cargo_bed"
        ))
        parts.append(P(box, 0.1 * s, 0.1 * s, 0.1 * s, part=p_payload2, at=(0.0, 0.45 * s + height, 0.0), trim="steel_plain", keep=False, name="cargo_gate"))

    elif pk == "ugv":
        parts.append(P(
            box, 1.2 * s, 0.3 * s, 0.8 * s,
            part=p_payload, at=(0.0, 0.45 * s + height + 0.15 * s, 0.0), trim="armor_desert", keep=True, name="sensor_pod"
        ))
        parts.append(P(
            cylinder, 0.02 * s, 0.02 * s, 0.6 * s,
            part=p_payload2, at=(0.4 * s, 0.45 * s + height + 0.6 * s, 0.0), trim="steel_plain", keep=False, name="antenna"
        ))

    return Model(id=spec.id, category=params.CAT_VEHICLE, target_dim_m=spec.target_dim, parts=parts, rig=rig, hardpoints=hardpoints)
