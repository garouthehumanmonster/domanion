# Gate 13 Contract Notes: Lighting & World Marks

## Q1: Sun Direction Sign Convention
- Source: `assets/shaders/terrain.frag:130-131`, `assets/shaders/entity_draw.frag:76-77`
- In `terrain.frag`:
  ```glsl
  vec3 L = u_sun_dir.xyz;
  float NdotL = max(dot(n, L), 0.0);
  ```
  `n` is the outward/upward surface normal. Since `dot(n, L)` is positive when `L` points toward the sky/sun, `u_sun_dir.xyz` points **TOWARD the sun**.
- In `entity_draw.frag`:
  ```glsl
  vec3 L = u_sun_dir.xyz;
  float NdotL = max(dot(world_n, L), 0.0);
  ```
  Identical usage.
- Definition of `light_to_sun(const FrameUniforms& fu, float L[3])`:
  ```cpp
  float len = std::sqrt(fu.sun_dir[0]*fu.sun_dir[0] + fu.sun_dir[1]*fu.sun_dir[1] + fu.sun_dir[2]*fu.sun_dir[2]);
  float inv_len = (len > 1e-6f) ? (1.0f / len) : 1.0f;
  L[0] = fu.sun_dir[0] * inv_len;
  L[1] = fu.sun_dir[1] * inv_len;
  L[2] = fu.sun_dir[2] * inv_len;
  ```

## Q2: Sun Color Scale and Numeric Defaults
- Source: `src/render/frame_fill.cpp:49-59`, `src/render/ron_view_main.cpp:539-542`
- `fill_frame_uniforms` defaults:
  - `sun_dir`: `Vec3{0.30f, 0.70f, 0.65f}` normalized -> $(0.304855, 0.711329, 0.660520)$.
  - `sun_intensity`: `3.0f` stored in `sun_dir[3]`.
  - `sun_color`: `Vec3{1.0f, 0.95f, 0.85f}` stored in `sun_color[0..2]`, `sun_color[3] = 0.0f`.
- The product `u_sun_color.rgb * u_sun_dir.w` represents incident sun irradiance divided by $\pi$. In PBR-lite, `sun_rgb = u_sun_color.rgb * u_sun_dir.w`, matching the existing Lambertian diffuse direct term `(NdotL / PI) * sun_intensity * sun_color`.

## Q3: Tonemap Render Target & Marks Pass Placement
- Source: `src/render/ron_view_main.cpp:588`, `tests/render/entity_fixture.hpp:127`, `assets/shaders/tonemap.frag:20-27`
- Tonemap write target:
  - In `ron_view`: Default framebuffer `0`.
  - In test fixtures (`EntityDrawFixture`): Offscreen LDR target `ldr_rt.fbo()` (RGBA8, 640x360).
- sRGB conversion: Target is a standard linear RGBA8 texture (`TexFmt::RGBA8`), NOT `GL_SRGB8`. The tonemap fragment shader manually applies exact piecewise sRGB encoding (`tonemap.frag:22-24`). The framebuffer target does not perform hardware sRGB conversion (`GL_FRAMEBUFFER_SRGB` is disabled).
- Readback:
  - `glReadPixels` or `glGetTextureSubImage` reads pixel data where row 0 is the **BOTTOM** row in OpenGL texture coordinates.
- Marks placement:
  - Drawn immediately **AFTER** the tonemap pass and **BEFORE** the minimap/UI pass.
  - Drawn into the current LDR target (default FBO in `ron_view`, `ldr_rt.fbo()` in tests).
  - State: No depth test (`glDisable(GL_DEPTH_TEST)`), depth mask false (`glDepthMask(GL_FALSE)`), alpha blending enabled (`glEnable(GL_BLEND); glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA)`).
  - Colors are display-space RGB written directly without sRGB conversion.

## Q4: MSDF Atlas Icon Layout
- Source: `docs/asset_contracts.md:31-34`, `tools/texture_baker/msdf_atlas.py:35-47`, `tools/texture_baker/bake_textures.py:144-153`
- Dimensions: 1024x512 RGBA8 texture (`msdf_atlas`, texture ID 8 in `.tex26`).
- Region: Icons occupy 32 cells of 64x64 px in 16 columns at $x \in [0, 1024), y \in [384, 512)$.
  - Column: `col = i % 16`
  - Row: `row = i / 16` ($0$ or $1$)
  - Pixel rect: $x = \text{col} \times 64$, $y = 384 + \text{row} \times 64$, $w = 64$, $h = 64$.
  - UV rect:
    - $u_0 = x / 1024.0f = \text{col} \times (64.0f / 1024.0f)$
    - $u_1 = (x + 64) / 1024.0f = u_0 + (64.0f / 1024.0f)$
    - $v_0 = y / 512.0f = (384 + \text{row} \times 64) / 512.0f$
    - $v_1 = (y + 64) / 512.0f = v_0 + (64.0f / 512.0f)$
- $v$ direction: In texture memory and image files, row 0 is the **TOP** of the image ($y$ increases downward). With $v_0 < v_1$, sampling $v$ increasing downward samples image top at quad top, maintaining upright glyphs.
- Channel semantics: R, G, B hold multi-channel signed distance field (MSDF), A holds true signed distance field (SDF).
- Distance decoding formula:
  $$\text{sd\_val} = \text{median}(r, g, b)$$
  $$\text{dist\_px} = (\text{sd\_val} - 0.5) \times 8.0 \quad (\text{RANGE\_PX} = 4.0)$$
  $$\text{screenPxRange} = \max(\text{RANGE\_PX} \times \text{glyph\_px} / 64.0, 1.0)$$
  $$\text{alpha} = \text{clamp}((\text{sd\_val} - 0.5) \times \text{screenPxRange} + 0.5, 0.0, 1.0)$$

## Q5: Terrain Fragment Shader
- Normal: `vec3 n` (lines 123-127)
- Albedo: `vec3 albedo` (lines 84, 95, 115)
- Roughness: `float rough = texture(u_rh_arr, vec3(uv_detail, float(l00))).g;` (from `u_rh_arr`, default layer roughness)
- Final color: `vec3 color` (line 139)
- Placeholder lighting line range: lines 129-135 (`terrain.frag:129-135`)
- Height texture: `TEX_TERRAIN_HEIGHT` (format `GL_R16_UNORM`, filter `GL_NEAREST`), GLuint accessor: `TerrainRenderer::height_tex().id()`.

## Q6: Entity Fragment Shader
- Normal: `vec3 world_n` (lines 53-56)
- Albedo: `vec3 albedo` (lines 62-67)
- Roughness: `float rough = orm.g;` (line 71)
- Final color: `vec3 color` (line 88)
- Placeholder lighting line range: lines 76-88 (`entity_draw.frag:76-88`)

## Q7: Render Call Signatures
- Terrain:
  `void TerrainRenderer::draw_terrain(u32 program) noexcept;`
  `void TerrainRenderer::draw_ocean(u32 program) noexcept;`
- Entity Draw Fixture:
  `EntityDrawResult EntityDrawFixture::render(const std::vector<EntityInstance>& instances, const Camera& cam, float alpha, float time_s, bool draw_terrain, bool draw_entities, bool draw_ocean, bool ovl, const float* clear_color);`

## Q8: Complete 71-Model Icon and Shape Table
Categories:
- 0: Infantry (Shape 0 = ground box)
- 1: Vehicle (Shape 0 = ground box)
- 2: Air (Shape 1 = air dome / semicircle)
- 3: Naval (Shape 2 = naval circle)
- 4: Building (Shape 3 = building square)
- 5: Prop (None = 0xFFFF)
- 6: Resource (None = 0xFFFF)
- 7: Projectile (None = 0xFFFF)

Icon indices:
- 4: `sym_infantry`, 5: `sym_armor`, 6: `sym_artillery`, 7: `sym_air`, 8: `sym_naval`, 9: `sym_recon`, 10: `sym_missile`, 11: `sym_engineer`, 12: `sym_logistics`, 13: `sym_drone`, 14: `res_food`, 15: `res_timber`, 16: `res_metal`, 17: `res_wealth`, 18: `res_knowledge`, 19: `res_oil`, 29: `ui_square_ring`, 30: `ui_gear`.

| Index | Model Name | Category | Shape | Icon ID | Icon Name | Notes |
|---|---|---|---|---|---|---|
| 0 | `inf_spear` | 0 (infantry) | 0 (ground) | 4 | `sym_infantry` | Infantry default |
| 1 | `inf_bow` | 0 (infantry) | 0 (ground) | 4 | `sym_infantry` | Infantry default |
| 2 | `inf_sword` | 0 (infantry) | 0 (ground) | 4 | `sym_infantry` | Infantry default |
| 3 | `inf_musket` | 0 (infantry) | 0 (ground) | 4 | `sym_infantry` | Infantry default |
| 4 | `inf_rifle` | 0 (infantry) | 0 (ground) | 4 | `sym_infantry` | Infantry default |
| 5 | `inf_at` | 0 (infantry) | 0 (ground) | 4 | `sym_infantry` | Infantry default |
| 6 | `inf_special` | 0 (infantry) | 0 (ground) | 9 | `sym_recon` | Special ops / recon |
| 7 | `inf_worker` | 0 (infantry) | 0 (ground) | 11 | `sym_engineer` | Combat engineer / worker |
| 8 | `inf_drone_op` | 0 (infantry) | 0 (ground) | 13 | `sym_drone` | Drone operator |
| 9 | `cav_light` | 1 (vehicle) | 0 (ground) | 9 | `sym_recon` | Light cavalry recon |
| 10 | `cav_heavy` | 1 (vehicle) | 0 (ground) | 5 | `sym_armor` | Heavy armor shock |
| 11 | `siege_catapult` | 1 (vehicle) | 0 (ground) | 6 | `sym_artillery` | Artillery / siege |
| 12 | `siege_cannon` | 1 (vehicle) | 0 (ground) | 6 | `sym_artillery` | Artillery / siege |
| 13 | `veh_tank_early` | 1 (vehicle) | 0 (ground) | 5 | `sym_armor` | Armor |
| 14 | `veh_mbt` | 1 (vehicle) | 0 (ground) | 5 | `sym_armor` | Armor |
| 15 | `veh_ifv` | 1 (vehicle) | 0 (ground) | 5 | `sym_armor` | Armor / IFV |
| 16 | `veh_arty_sp` | 1 (vehicle) | 0 (ground) | 6 | `sym_artillery` | Self-propelled artillery |
| 17 | `veh_apc` | 1 (vehicle) | 0 (ground) | 4 | `sym_infantry` | Armored personnel carrier |
| 18 | `veh_himars` | 1 (vehicle) | 0 (ground) | 10 | `sym_missile` | Rocket / missile artillery |
| 19 | `veh_sam` | 1 (vehicle) | 0 (ground) | 10 | `sym_missile` | Surface-to-air missile |
| 20 | `veh_ew` | 1 (vehicle) | 0 (ground) | 9 | `sym_recon` | Electronic warfare / recon |
| 21 | `veh_truck` | 1 (vehicle) | 0 (ground) | 12 | `sym_logistics` | Supply truck |
| 22 | `veh_ugv` | 1 (vehicle) | 0 (ground) | 13 | `sym_drone` | Unmanned ground vehicle |
| 23 | `air_biplane` | 2 (air) | 1 (air) | 7 | `sym_air` | Air default |
| 24 | `air_prop` | 2 (air) | 1 (air) | 7 | `sym_air` | Air default |
| 25 | `air_jet` | 2 (air) | 1 (air) | 7 | `sym_air` | Air default |
| 26 | `air_stealth` | 2 (air) | 1 (air) | 9 | `sym_recon` | Stealth recon / strike |
| 27 | `air_bomber` | 2 (air) | 1 (air) | 7 | `sym_air` | Bomber |
| 28 | `air_helo` | 2 (air) | 1 (air) | 7 | `sym_air` | Rotary wing |
| 29 | `air_fpv` | 2 (air) | 1 (air) | 13 | `sym_drone` | FPV strike drone |
| 30 | `nav_galley` | 3 (naval) | 2 (naval) | 8 | `sym_naval` | Naval default |
| 31 | `nav_galleon` | 3 (naval) | 2 (naval) | 8 | `sym_naval` | Naval default |
| 32 | `nav_ironclad` | 3 (naval) | 2 (naval) | 8 | `sym_naval` | Naval default |
| 33 | `nav_destroyer` | 3 (naval) | 2 (naval) | 8 | `sym_naval` | Destroyer |
| 34 | `nav_carrier` | 3 (naval) | 2 (naval) | 8 | `sym_naval` | Aircraft carrier |
| 35 | `nav_sub` | 3 (naval) | 2 (naval) | 8 | `sym_naval` | Submarine |
| 36 | `bld_city_center_a` | 4 (building) | 3 (building) | 12 | `sym_logistics` | HQ / Logistics |
| 37 | `bld_city_center_b` | 4 (building) | 3 (building) | 12 | `sym_logistics` | HQ / Logistics |
| 38 | `bld_city_center_c` | 4 (building) | 3 (building) | 12 | `sym_logistics` | HQ / Logistics |
| 39 | `bld_house` | 4 (building) | 3 (building) | 29 | `ui_square_ring` | Housing settlement |
| 40 | `bld_farm` | 4 (building) | 3 (building) | 14 | `res_food` | Food production |
| 41 | `bld_lumber` | 4 (building) | 3 (building) | 15 | `res_timber` | Timber production |
| 42 | `bld_mine` | 4 (building) | 3 (building) | 16 | `res_metal` | Metal mine |
| 43 | `bld_market` | 4 (building) | 3 (building) | 17 | `res_wealth` | Market / Trade |
| 44 | `bld_barracks` | 4 (building) | 3 (building) | 4 | `sym_infantry` | Troop production |
| 45 | `bld_factory` | 4 (building) | 3 (building) | 30 | `ui_gear` | Vehicle factory |
| 46 | `bld_airfield` | 4 (building) | 3 (building) | 7 | `sym_air` | Air base |
| 47 | `bld_dock` | 4 (building) | 3 (building) | 8 | `sym_naval` | Naval shipyard |
| 48 | `bld_library` | 4 (building) | 3 (building) | 18 | `res_knowledge` | Research / Library |
| 49 | `bld_tower` | 4 (building) | 3 (building) | 9 | `sym_recon` | Watchtower / Guard |
| 50 | `bld_sam_site` | 4 (building) | 3 (building) | 10 | `sym_missile` | Surface-to-air missile |
| 51 | `bld_radar` | 4 (building) | 3 (building) | 9 | `sym_recon` | Radar early warning |
| 52 | `bld_silo` | 4 (building) | 3 (building) | 10 | `sym_missile` | Missile silo |
| 53 | `bld_refinery` | 4 (building) | 3 (building) | 19 | `res_oil` | Fuel refinery |
| 54 | `prop_tree_oak` | 5 (prop) | 0 | 0xFFFF | None | Excluded prop |
| 55 | `prop_tree_pine` | 5 (prop) | 0 | 0xFFFF | None | Excluded prop |
| 56 | `prop_tree_palm` | 5 (prop) | 0 | 0xFFFF | None | Excluded prop |
| 57 | `prop_rock` | 5 (prop) | 0 | 0xFFFF | None | Excluded prop |
| 58 | `res_berry_bush` | 6 (resource) | 0 | 0xFFFF | None | Excluded resource |
| 59 | `res_herd` | 6 (resource) | 0 | 0xFFFF | None | Excluded resource |
| 60 | `res_gold` | 6 (resource) | 0 | 0xFFFF | None | Excluded resource |
| 61 | `res_iron` | 6 (resource) | 0 | 0xFFFF | None | Excluded resource |
| 62 | `res_oil` | 6 (resource) | 0 | 0xFFFF | None | Excluded resource |
| 63 | `res_silicon` | 6 (resource) | 0 | 0xFFFF | None | Excluded resource |
| 64 | `res_relic` | 6 (resource) | 0 | 0xFFFF | None | Excluded resource |
| 65 | `proj_arrow` | 7 (projectile) | 0 | 0xFFFF | None | Excluded projectile |
| 66 | `proj_shell` | 7 (projectile) | 0 | 0xFFFF | None | Excluded projectile |
| 67 | `proj_rocket` | 7 (projectile) | 0 | 0xFFFF | None | Excluded projectile |
| 68 | `proj_missile` | 7 (projectile) | 0 | 0xFFFF | None | Excluded projectile |
| 69 | `proj_hypersonic` | 7 (projectile) | 0 | 0xFFFF | None | Excluded projectile |
| 70 | `proj_bomb` | 7 (projectile) | 0 | 0xFFFF | None | Excluded projectile |

### Summary of Models per Icon
- `sym_infantry` (4): 8 models (`inf_spear`, `inf_bow`, `inf_sword`, `inf_musket`, `inf_rifle`, `inf_at`, `veh_apc`, `bld_barracks`)
- `sym_armor` (5): 4 models (`cav_heavy`, `veh_tank_early`, `veh_mbt`, `veh_ifv`)
- `sym_artillery` (6): 3 models (`siege_catapult`, `siege_cannon`, `veh_arty_sp`)
- `sym_air` (7): 6 models (`air_biplane`, `air_prop`, `air_jet`, `air_bomber`, `air_helo`, `bld_airfield`)
- `sym_naval` (8): 7 models (`nav_galley`, `nav_galleon`, `nav_ironclad`, `nav_destroyer`, `nav_carrier`, `nav_sub`, `bld_dock`)
- `sym_recon` (9): 6 models (`inf_special`, `cav_light`, `veh_ew`, `air_stealth`, `bld_tower`, `bld_radar`)
- `sym_missile` (10): 4 models (`veh_himars`, `veh_sam`, `bld_sam_site`, `bld_silo`)
- `sym_engineer` (11): 1 model (`inf_worker`)
- `sym_logistics` (12): 4 models (`veh_truck`, `bld_city_center_a`, `bld_city_center_b`, `bld_city_center_c`)
- `sym_drone` (13): 3 models (`inf_drone_op`, `veh_ugv`, `air_fpv`)
- `res_food` (14): 1 model (`bld_farm`)
- `res_timber` (15): 1 model (`bld_lumber`)
- `res_metal` (16): 1 model (`bld_mine`)
- `res_wealth` (17): 1 model (`bld_market`)
- `res_knowledge` (18): 1 model (`bld_library`)
- `res_oil` (19): 1 model (`bld_refinery`)
- `ui_square_ring` (29): 1 model (`bld_house`)
- `ui_gear` (30): 1 model (`bld_factory`)
- None (0xFFFF): 17 models (4 props, 7 resources, 6 projectiles)
- **Total**: 54 marked models + 17 none = 71 models.
