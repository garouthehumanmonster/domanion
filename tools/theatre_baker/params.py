"""params.py - Central constants for DOMINION 2026 Continental Theatre Baker."""

# World and Grid Dimensions
WORLD_M = 4096.0
HEIGHT_MIN_M = -128.0
HEIGHT_MAX_M = 512.0
SEA_LEVEL_M = 0.0
SEA_LEVEL_CODE = 13107 # Exact: round((0 + 128) * 65535 / 640)
H_CODE_SCALE = 65535.0 / 640.0
H_CODE_OFFSET = 128.0

DEFAULT_N = 2048
ALLOWED_N = (512, 1024, 2048)
LOGIC_N = 512
LOGIC_CELL_M = 8.0 # 4096.0 / 512

LAND_FRACTION = 0.46
MAX_LAND_M = 420.0
PLAYER_SLOTS = 8
START_COUNT = 8

# Binary Format Constants
MAGIC = 0x52544854 # 'T','H','T','R' in little-endian
VERSION = 1
SECTION_COUNT = 8
HEADER_BYTES = 64
SECTION_TABLE_BYTES = 128 # 8 * 16

# Enums
class Surface:
    DeepWater = 0
    ShallowWater = 1
    Beach = 2
    Land = 3
    Rough = 4
    Cliff = 5

class Biome:
    Ocean = 0
    Beach = 1
    Grass = 2
    Forest = 3
    Arid = 4
    Tundra = 5
    Rock = 6

class NodeKind:
    Berries = 0
    Forest = 1
    Iron = 2
    Gold = 3
    Relic = 4
    Oil = 5
    Silicon = 6
    Herd = 7

# Surface Decision Thresholds
SURF_DEEP_WATER_HM = -8.0
SURF_SHALLOW_WATER_HM = -0.75
SURF_BEACH_HM = 2.5
SURF_BEACH_MAX_SLOPE = 12.0
SURF_CLIFF_MIN_SLOPE = 40.0
SURF_ROUGH_MIN_SLOPE = 25.0

# 3.3 Erosion Tunables (Adjustable within stated ranges)
KC = 0.25            # (range 0.05..1.0)
KS = 0.05            # (range 0.01..0.2)
KD = 0.05            # (range 0.01..0.2)
RAIN_PER_ITER = 0.0015 # (range 0.0005..0.005)
EVAP = 0.985         # (range 0.97..0.995)

# Fixed Erosion Constants
DT = 0.02
G = 9.81
PIPE_A = 1.0
FLUX_DAMP = 0.99
DMAX = 0.1
SIN_MIN = 0.05
EPS = 1e-4
UMAX = 1.0 / DT # 50.0
EROSION_CLAMP_M = 40.0

COARSE_ITERS = 400 # 512 grid
FINE_ITERS = 80   # N > 512 only
THERMAL_ITERS = 24
THERMAL_RATE = 0.125
# THERMAL_TALUS_PER_CELL_M is 0.8 * cell_m

# Shore Shaping
SHORE_BLEND_MIN_M = 2.0
SHORE_BLEND_MAX_M = 8.0

# Amphibious Landing Ramps
LANDING_TARGET = 10
LANDINGS_MIN = 6
LANDING_MIN_DIST_M = 400.0
LANDING_MAX_SLOPE = 12.0
LANDING_MAX_DISTWATER = 2
LANDING_MAX_H_BEHIND_M = 25.0

# Player Start Placements
START_MIN_SLOPE = 20.0 # Note: Relaxed from 8.0 to 20.0 (see docs/spec_deviations.md)
START_MIN_DISTWATER = 12 # Note: Relaxed from 20 to 12 (see docs/spec_deviations.md)
START_MIN_HM = 8.0
START_MAX_HM = 120.0
START_MARGIN_CELLS = 24
START_MIN_DIST_M = 600.0
PLATEAU_FLAT_RADIUS_M = 64.0
PLATEAU_WINDOW_RADIUS_M = 160.0
PLATEAU_MEDIAN_RADIUS_M = 96.0

# Kit Resources Table
# (kind, count, ring_min_m, ring_max_m, amount, footprint_m)
KIT_RESOURCES = [
    (NodeKind.Berries, 3, 85,  120,  800,  4),
    (NodeKind.Herd,    2, 110, 170, 1200,  6),
    (NodeKind.Forest,  6, 100, 190, 1500, 12),
    (NodeKind.Iron,    3, 120, 210, 2000,  6),
    (NodeKind.Gold,    2, 150, 240, 1500,  5),
    (NodeKind.Oil,     1, 220, 330, 3000,  8),
    (NodeKind.Silicon, 1, 260, 360, 2500,  6),
]

# Neutral Resources Table
# (name, kind, target, min, spacing_m, amount, footprint_m)
NEUTRAL_RESOURCES = [
    ("Forest",  NodeKind.Forest,  240, 100,  55, 1500, 12),
    ("Berries", NodeKind.Berries,  40,  16,  90,  800,  4),
    ("Herd",    NodeKind.Herd,     30,  12, 120, 1200,  6),
    ("Iron",    NodeKind.Iron,     48,  20,  90, 2000,  6),
    ("Gold",    NodeKind.Gold,     32,  14, 140, 1500,  5),
    ("Silicon", NodeKind.Silicon,  24,   8, 120, 2500,  6),
    ("OilLand", NodeKind.Oil,      16,   8, 180, 3000,  8),
    ("OilSea",  NodeKind.Oil,      12,   6, 200, 3000,  8),
    ("Relic",   NodeKind.Relic,    16,   8, 250, 1000,  5),
]

# Validator Thresholds
LAND_FRACTION_MIN = 0.40
LAND_FRACTION_MAX = 0.52
BEACH_CELLS_MIN = 300
FOREST_GRASS_MIN_SHARE = 0.30
MAX_TOTAL_RESOURCES = 1024
MAX_TOTAL_LANDINGS = 32
