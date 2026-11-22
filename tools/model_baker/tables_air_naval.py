"""tables_air_naval.py - Model definitions for Air (7) and Naval (6) models."""

from gen_air import make_fixed_wing, make_rotary
from gen_naval import make_ship
from model import Model

AIR_MODELS = [
    make_fixed_wing("air_biplane", 8.0, "biplane"),
    make_fixed_wing("air_prop", 9.5, "prop"),
    make_fixed_wing("air_jet", 16.0, "jet"),
    make_fixed_wing("air_stealth", 12.0, "stealth"),
    make_fixed_wing("air_bomber", 40.0, "bomber"),
    make_rotary("air_helo", 16.0, "helo"),
    make_rotary("air_fpv", 0.6, "quad"),
]

NAVAL_MODELS = [
    make_ship("nav_galley", 28.0, "galley", draft=1.0),
    make_ship("nav_galleon", 45.0, "galleon", draft=4.0),
    make_ship("nav_ironclad", 70.0, "ironclad", draft=5.0),
    make_ship("nav_destroyer", 150.0, "destroyer", draft=6.0),
    make_ship("nav_carrier", 330.0, "carrier", draft=11.0),
    make_ship("nav_sub", 100.0, "sub", draft=6.0),
]

MODELS = AIR_MODELS + NAVAL_MODELS
