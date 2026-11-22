"""tables_static.py - Model definitions for Buildings (18), Props (4), Resources (7), and Projectiles (6)."""

from gen_building import make_building
from gen_props import make_prop, make_resource, make_projectile
from model import Model

BUILDING_SPECS = [
    ("bld_city_center_a", 30.0),
    ("bld_city_center_b", 36.0),
    ("bld_city_center_c", 44.0),
    ("bld_house", 9.0),
    ("bld_farm", 24.0),
    ("bld_lumber", 14.0),
    ("bld_mine", 14.0),
    ("bld_market", 20.0),
    ("bld_barracks", 24.0),
    ("bld_factory", 40.0),
    ("bld_airfield", 90.0),
    ("bld_dock", 40.0),
    ("bld_library", 26.0),
    ("bld_tower", 16.0),
    ("bld_sam_site", 20.0),
    ("bld_radar", 18.0),
    ("bld_silo", 16.0),
    ("bld_refinery", 36.0),
]

PROP_SPECS = [
    ("prop_tree_oak", 9.0),
    ("prop_tree_pine", 12.0),
    ("prop_tree_palm", 10.0),
    ("prop_rock", 3.5),
]

RESOURCE_SPECS = [
    ("res_berry_bush", 1.6),
    ("res_herd", 2.0),
    ("res_gold", 3.0),
    ("res_iron", 3.2),
    ("res_oil", 6.0),
    ("res_silicon", 3.0),
    ("res_relic", 4.0),
]

PROJECTILE_SPECS = [
    ("proj_arrow", 0.9),
    ("proj_shell", 0.8),
    ("proj_rocket", 2.0),
    ("proj_missile", 4.5),
    ("proj_hypersonic", 6.0),
    ("proj_bomb", 2.2),
]

BUILDINGS = [make_building(i, d) for i, d in BUILDING_SPECS]
PROPS = [make_prop(i, d) for i, d in PROP_SPECS]
RESOURCES = [make_resource(i, d) for i, d in RESOURCE_SPECS]
PROJECTILES = [make_projectile(i, d) for i, d in PROJECTILE_SPECS]

MODELS = BUILDINGS + PROPS + RESOURCES + PROJECTILES
