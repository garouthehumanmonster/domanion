"""params.py - Central constants and tunables for Model Baker & VAT pipeline."""

# Category constants (u8)
CAT_INFANTRY = 0
CAT_VEHICLE = 1
CAT_AIR = 2
CAT_NAVAL = 3
CAT_BUILDING = 4
CAT_PROP = 5
CAT_RESOURCE = 6
CAT_PROJECTILE = 7
CATEGORY_NAMES = [
    "Infantry", "Vehicle", "Air", "Naval",
    "Building", "Prop", "Resource", "Projectile",
]

# Hardpoint kinds (u8)
HP_MUZZLE = 0
HP_TURRET_PIVOT = 1
HP_EXHAUST = 2
HP_SENSOR = 3
HP_HATCH = 4
HP_ENGINE = 5
HP_LASER = 6
HP_MISC = 7

# Vertex flags (u8)
FLAG_TEAM_COLOR = 1 << 0
FLAG_EMISSIVE = 1 << 1
FLAG_SCROLL_UV = 1 << 2

# Model flags (u8)
MODEL_FLAG_HAS_TEAM_COLOR = 1 << 0
MODEL_FLAG_ANIMATED_IDLE = 1 << 1
MODEL_FLAG_SCROLL_UV = 1 << 2

# Constraints
MAX_PARTS = 16
MAX_VERTICES_PER_LOD = 65535

# Binary format constants
MAGIC_MD26 = 0x36324D44  # 'MD26'
FORMAT_VERSION = 1
SECTION_COUNT = 5
MODEL_REC_SIZE = 96
VERTEX_REC_SIZE = 24
HARDPOINT_REC_SIZE = 16

# VAT layout
FRAMES_PER_MODEL = 44
VAT_COLS = 48
CLIPS = [
    ("Idle", 8, True),
    ("Move", 16, True),
    ("Attack", 8, False),
    ("Die", 12, False),
]
CLIP_START = [0, 8, 24, 32]

# TUNABLES (Section 1.4)
LOD1_DROP_FRAC = 0.06  # range 0.03..0.12
LOD2_DROP_FRAC = 0.18  # range 0.10..0.30

# Primitive detail per LOD [LOD0, LOD1, LOD2]
CYL_SEGS = [12, 8, 6]
SPHERE_SEGS = [(8, 12), (6, 8), (4, 6)]  # (lat, lon)

# Triangle budgets and ratios
LOD_RATIO_LOD1_MAX = 0.85
LOD_RATIO_LOD2_MAX = 0.60
LOD_RATIO_THRESHOLD_TRIS = 150

TEAM_AREA_MIN = 0.03
TEAM_AREA_MAX = 0.50

CATEGORY_TRIS_RANGE = {
    CAT_INFANTRY: (150, 700),
    CAT_VEHICLE: (150, 2200),
    CAT_AIR: (80, 1800),
    CAT_NAVAL: (200, 3500),
    CAT_BUILDING: (200, 5000),
    CAT_PROP: (40, 900),
    CAT_RESOURCE: (40, 900),
    CAT_PROJECTILE: (20, 200),
}

# Trim sheet layout
SHEET_DIM = 2048
TILE_DIM = 256
SHEET_GRID = 8
TILE_PADDING = 4
MAX_TRIMS = 32
