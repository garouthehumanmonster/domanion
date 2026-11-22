"""tables_ground.py - Model definitions for Infantry (9) and Vehicles (14)."""

import params
from gen_humanoid import HumanoidSpec, make_humanoid
from gen_ground import (
    make_mounted, make_siege, TrackedSpec, make_tracked, WheeledSpec, make_wheeled
)
from model import Model

INFANTRY_SPECS = [
    HumanoidSpec("inf_spear", 2.6, "spear", "helmet_bronze", "shield_round", "cloth_tunic", limbs_trim="cloth_tunic"),
    HumanoidSpec("inf_bow", 1.9, "bow", "hood", "quiver", "cloth_tunic", limbs_trim="cloth_tunic"),
    HumanoidSpec("inf_sword", 1.9, "sword", "helmet_crest", "shield_kite", "steel_plain", limbs_trim="leather"),
    HumanoidSpec("inf_musket", 1.9, "musket", "hat_tricorn", "none", "cloth_dark", limbs_trim="cloth_dark"),
    HumanoidSpec("inf_rifle", 1.9, "rifle", "helmet_modern", "none", "camo_woodland", limbs_trim="camo_woodland"),
    HumanoidSpec("inf_at", 1.9, "launcher", "helmet_modern", "pack", "camo_desert", limbs_trim="camo_desert"),
    HumanoidSpec("inf_special", 1.9, "rifle_short", "helmet_nvg", "none", "camo_urban", limbs_trim="camo_urban"),
    HumanoidSpec("inf_worker", 1.9, "tool_pick", "hat_straw", "none", "cloth_tunic", limbs_trim="cloth_tunic"),
    HumanoidSpec("inf_drone_op", 1.9, "controller", "helmet_modern", "pack", "camo_urban", limbs_trim="camo_urban"),
]

def build_infantry_models() -> list[Model]:
    return [make_humanoid(s) for s in INFANTRY_SPECS]

def build_vehicle_models() -> list[Model]:
    vehicles = [
        make_mounted("cav_light", 3.2, kind="lance"),
        make_mounted("cav_heavy", 3.2, kind="heavy"),
        make_siege("siege_catapult", 7.0, kind="catapult"),
        make_siege("siege_cannon", 4.5, kind="cannon"),
        make_tracked(TrackedSpec("veh_tank_early", 6.5, (5.2, 2.5, 1.1), (2.2, 1.8, 0.8), 2.2, 0.12, "armor_olive")),
        make_tracked(TrackedSpec("veh_mbt", 10.5, (7.6, 3.5, 1.2), (3.6, 2.6, 0.9), 4.8, 0.16, "camo_woodland", has_extras=True)),
        make_tracked(TrackedSpec("veh_ifv", 7.0, (5.8, 2.8, 1.1), (2.4, 2.0, 0.8), 2.4, 0.08, "camo_desert", has_extras=True)),
        make_tracked(TrackedSpec("veh_arty_sp", 11.0, (7.8, 3.4, 1.3), (3.8, 2.6, 1.2), 6.0, 0.18, "armor_olive")),
        make_wheeled(WheeledSpec("veh_apc", 8.0, 4, "troop", "armor_olive")),
        make_wheeled(WheeledSpec("veh_himars", 7.0, 3, "pods", "camo_desert")),
        make_wheeled(WheeledSpec("veh_sam", 8.5, 3, "laser_cram", "armor_grey")),
        make_wheeled(WheeledSpec("veh_ew", 8.0, 3, "mast", "camo_urban")),
        make_wheeled(WheeledSpec("veh_truck", 8.0, 2, "cargo", "armor_olive")),
        make_wheeled(WheeledSpec("veh_ugv", 2.4, 2, "ugv", "armor_desert")),
    ]
    return vehicles

MODELS = build_infantry_models() + build_vehicle_models()
