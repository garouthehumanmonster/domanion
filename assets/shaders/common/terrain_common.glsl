layout(std140, binding = BIND_UBO_TERRAIN) uniform TerrainUbo {
    vec4 u_lod_range[2];
    vec4 u_tparams0; // x: skirt_depth, y: rock_layer, z: tex_n, w: inv_n
    vec4 u_tparams1; // x: biome_w, y: biome_h, z: ocean_depth_max, w: pad
    uvec4 u_biome_layer[8];
};

struct TerrainNodeData {
    vec4 pos_size_level; // x, z, size, level
};

layout(std430, binding = BIND_SSBO_TERRAIN_NODES) readonly buffer TerrainNodes {
    TerrainNodeData u_nodes[];
};

layout(binding = TEX_TERRAIN_HEIGHT) uniform sampler2D u_height;
layout(binding = TEX_TERRAIN_BIOME) uniform sampler2D u_biome;
layout(binding = TEX_TERRAIN_ALBEDO) uniform sampler2DArray u_albedo_arr;
layout(binding = TEX_TERRAIN_NORMAL) uniform sampler2DArray u_normal_arr;
layout(binding = TEX_TERRAIN_RH) uniform sampler2DArray u_rh_arr;
layout(binding = TEX_OCEAN_DEPTH) uniform sampler2D u_ocean_depth;

float lod_range(int L) {
    int v_idx = L >> 2;
    int c_idx = L & 3;
    return u_lod_range[v_idx][c_idx];
}

uint biome_to_layer(uint b_id) {
    uint v_idx = b_id >> 2;
    uint c_idx = b_id & 3;
    return u_biome_layer[v_idx][c_idx];
}

float height_at_uv(vec2 uv) {
    float code_norm = textureLod(u_height, uv, 0.0).r;
    return RON_HEIGHT_MIN + RON_HEIGHT_RANGE * code_norm;
}

float height_at_world(vec2 xz) {
    vec2 uv = xz / RON_WORLD_SIZE;
    return height_at_uv(uv);
}
