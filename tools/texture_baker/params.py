"""params.py - Numeric constants, texture specifications, and tunables for texture_baker."""

# Tunables (Section 1.4)
NORMAL_SCALE = 24.0
MSDF_USE_PSEUDO = True

# Format codes
FMT_BC1 = 0
FMT_BC3 = 1
FMT_BC4 = 2
FMT_BC5 = 3
FMT_RGBA8 = 4

FORMAT_NAMES = {
    FMT_BC1: "BC1",
    FMT_BC3: "BC3",
    FMT_BC4: "BC4",
    FMT_BC5: "BC5",
    FMT_RGBA8: "RGBA8",
}

BLOCK_BYTES = {
    FMT_BC1: 8,
    FMT_BC3: 16,
    FMT_BC4: 8,
    FMT_BC5: 16,
    FMT_RGBA8: 4,
}

# Flags
FLAG_SRGB = 1 << 0
FLAG_PREMUL = 1 << 1

# Texture table definition (Section 1.1)
# (id, name, format, width, height, layers, mips, flags)
TEXTURE_DEFS = [
    (0, "trim_albedo", FMT_BC1, 2048, 2048, 1, 12, FLAG_SRGB),
    (1, "trim_normal", FMT_BC5, 2048, 2048, 1, 12, 0),
    (2, "trim_orm", FMT_BC1, 2048, 2048, 1, 12, 0),
    (3, "trim_emissive", FMT_BC4, 2048, 2048, 1, 12, 0),
    (4, "terrain_albedo", FMT_BC1, 512, 512, 8, 10, FLAG_SRGB),
    (5, "terrain_normal", FMT_BC5, 512, 512, 8, 10, 0),
    (6, "terrain_rh", FMT_BC1, 512, 512, 8, 10, 0),
    (7, "vfx_atlas", FMT_BC3, 2048, 2048, 1, 4, FLAG_PREMUL),
    (8, "msdf_atlas", FMT_RGBA8, 1024, 512, 1, 1, 0),
]

# Container limits
MAGIC_TX26 = 0x36325854  # 'TX26'
FORMAT_VERSION = 1
HEADER_SIZE = 64
RECORD_SIZE = 64
GLYPH_REC_SIZE = 16
GLYPH_COUNT = 95
TEXTURE_COUNT = 9

# Trim sheet constants
SHEET_SIZE = 2048
TILE_SIZE = 256
GRID_COUNT = 8
TRIM_COUNT = 32
TOTAL_TRIMS = 64
TRIM_MAX_LOD = 5

# MSDF constants
RANGE_PX = 4.0
FONT_CELL_SIZE = 48
ICON_CELL_SIZE = 64
ICON_COUNT = 32
ATLAS_WIDTH = 1024
ATLAS_HEIGHT = 512

# VFX constants
VFX_ATLAS_SIZE = 2048
VFX_CELL_SIZE = 512
VFX_FRAME_SIZE = 128
VFX_FRAMES_PER_EFFECT = 16
VFX_EFFECT_COUNT = 12
