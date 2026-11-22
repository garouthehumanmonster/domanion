<USER_REQUEST>
# Gate 07: Model Kitbasher + Vertex-Animation Textures (Python 3.13 + numpy, then a C++ loader)

Gate 07 doesn't read Gate 06's output, so you can start it now. You didn't say whether Gate 06 reached 39/39. If it didn't, run Groups A-G here and paste me the first Gate 06 failure separately. Group H's final test total is "previous total + 6", which is 45 only if Gate 06 was 39.

I can't run any of this code. The tunable ranges in section 1.4 are where I'm least sure, and I've limited the places Flash may adjust to those.

Why this should fail less than earlier gates:
- **Eight small groups.** Each has its own pass/fail command.
- **Every animation is zero at frame 0**, which gives a strong, exact test for the whole VAT.
- **Checks that can't be impossible.** Triangle budgets and size bands are loose. LOD ratios have tunable levers instead of fixed thresholds.
- **Procedural generators, not 71 hand-written models.** Flash writes about 12 generators plus parameter tables.

Open a **new Antigravity task** and paste everything inside the block.

````
# GATE 07: MODEL KITBASHER (tools/model_baker) + VAT + C++ ModelData LOADER

## 0. RULES FOR THIS TASK (read first, obey literally)
- GEMINI.md rules apply. Do not modify Gate 01-06 source files. Only exceptions: CMake source lists/targets (to add the new library, .cpp files and test file) and docs/.
- Do not import from tools/theatre_baker. Shared Python helpers for this gate and Gate 08 live in NEW package tools/asset_common/ (you may copy code from theatre_baker, not move it).
- Work in 8 GROUPS (A to H). After each group run its check command. Do NOT start the next group until it prints PASS.
- Never weaken a test to get green. If a number in this spec is provably impossible, STOP, quote the spec line, fix code and test, and append an entry to docs/spec_deviations.md. The only values you may change without a deviation entry are the TUNABLES in section 1.4, within their ranges.
- Python 3.13 only, numpy only (tools/model_baker/requirements.txt contains exactly: numpy>=2.1,<3). No scipy/PIL/trimesh/matplotlib. If `python --version` is not 3.13.x use `py -3.13`.
- DETERMINISM: no time seeds, no Python hash(), no np.random, no set-order dependence. Randomness only via asset_common.rng.Rng. Per-model randomness uses Rng(fnv1a64(model_id)). Geometry arrays are float32 unless stated; matrices/maths float64.
- Each Python file <= 400 lines, with a short docstring at the top.
- Entry scripts start with: sys.path.insert(0, <tools dir>) and sys.path.insert(0, <own dir>), so `from asset_common import ...` and sibling imports work from any cwd.
- Outputs (git-ignored, not committed): assets/compiled/models/models_2026.mdbin, models_2026.manifest.json, models_2026.preview.png. Committed source output: assets/source/trims.json.

## 1. LOCKED DECISIONS
### 1.1 Conventions
- Model space: metres, true real-world scale (renderer may apply a global scale later). +Y up, forward = +X, right side = +Z (right-handed: X east, Y up, Z south). Front faces are counter-clockwise (CCW) seen from outside.
- Ground models (Infantry, Vehicle, Building, Prop, Resource): feet/underside at y=0. Naval: origin at the waterline (hull extends below 0). Air and Projectile: origin at centre of the body.
- Categories (u8): 0 Infantry, 1 Vehicle (incl. mounted + siege), 2 Air, 3 Naval, 4 Building, 5 Prop, 6 Resource, 7 Projectile.
- Hardpoint kinds (u8): 0 Muzzle, 1 TurretPivot, 2 Exhaust, 3 Sensor, 4 Hatch (spawn/door), 5 Engine, 6 Laser, 7 Misc.
- Vertex flags (u8): bit0 team_color, bit1 emissive, bit2 scroll_uv (treads).
- Part index: u8 0..15 per model (MAX_PARTS=16). Part 0 is the root. Every other part's parent chain MUST lead to 0.
- Id hash = FNV-1a-64 of the UTF-8 id (offset 0xCBF29CE484222325, prime 0x100000001B3). Same function Gate 05 uses for ids.

### 1.2 Vertex format, 24 bytes (little-endian, "<3f2h2H BBH")
  f32 x,y,z | i16 nx,ny (octahedral snorm16) | u16 u,v (unorm16, SHEET uv) | u8 trim_id | u8 flags | u16 part
Indices: u16, LOCAL to the LOD's vertex block (drawn with baseVertex). Each LOD of each model must have <= 65535 vertices.
3 LODs per model (LOD0 best). Flat faces get their own vertices; cylinders/spheres/cones are smooth-shaded.

### 1.3 Animation (VAT) layout
Every model has exactly 44 frames: clips Idle (8, looping), Move (16, looping), Attack (8, non-looping), Die (12, non-looping). Clip start offsets in frames: Idle 0, Move 8, Attack 24, Die 32. t = f/F for looping clips, f/(F-1) for non-looping clips. The renderer chooses playback speed.
VAT texture data: RGBA16F, 48 columns x (44*model_count) rows. Row = vat_row_base + clip_start + frame. Column = part*3 + r (r=0..2); texel RGBA = row r of the 3x4 affine matrix M (m[r][0], m[r][1], m[r][2], m[r][3]) mapping a BIND-POSE model-space point p to animated model-space p' = M*[p,1]. Unused parts = identity. Bind pose = Idle frame 0.
RULE: every animation op evaluates to exactly zero at t=0, so frame 0 of EVERY clip is the identity matrix for every part.

### 1.4 TUNABLES (the only adjustable values; keep in params.py)
 LOD1_DROP_FRAC = 0.06 (range 0.03..0.12): a part is dropped from LOD1 if its bbox diagonal < this x model bbox diagonal (root part 0 and parts with keep=True are never dropped).
 LOD2_DROP_FRAC = 0.18 (range 0.10..0.30): same for LOD2.
 Fixed: CYL_SEGS=[12,8,6]; SPHERE_SEGS (lat,lon)=[(8,12),(6,8),(4,6)].
 LOD ratio rule (tested): for models with LOD0 > 150 tris: LOD1 <= 0.85*LOD0 and LOD2 <= 0.60*LOD0. If a model misses, first raise the DROP_FRACs within range, then make the model's small details separate small parts. Do NOT edit the ratios.
 TEAM_AREA_MIN = 0.03, TEAM_AREA_MAX = 0.50 (fixed). If a model misses, resize its team-colour parts.

### 1.5 Trim sheet contract (Gate 08 will paint it)
Sheet 2048x2048, 8x8 grid of 256 px tiles. Trim id t: col=t%8, row=t//8. Rect in pixels: x0=col*256+4, x1=(col+1)*256-4, y0=row*256+4, y1=(row+1)*256-4. SHEET UV: u=x/2048, v=y/2048, with v=0 at the TOP row of the image file (no flipping anywhere). Trim-local uv (0..1, 0..1) maps linearly into the rect. Only ids 0..31 are used now; 32..63 reserved.
32 names, in id order: steel_plain, steel_worn, armor_olive, armor_desert, armor_grey, camo_woodland, camo_desert, camo_urban, cloth_tunic, cloth_dark, leather, skin, wood_planks, wood_log, stone_block, stone_smooth, brick, concrete, concrete_panel, glass_window, roof_tile, roof_metal, rubber_tread, hazard_stripe, glow_emissive, solar_panel, foliage_leaf, bark, grass_tuft, gold, oil_dark, ice_crystal.
Each trim also carries a preview_rgb (3 ints 0..255; pick plausible colours; used only by preview.py).

## 2. BINARY FORMAT ".mdbin" (little-endian; all section offsets multiples of 64)
Header 64 bytes:
  0 u32 magic=0x36324D44 (bytes 'M','D','2','6') | 4 u32 version=1 | 8 u32 total_bytes | 12 u32 section_count=5
  16 u64 content_xxh64 = XXH64(seed 0) of bytes [64, total_bytes)
  24 u32 model_count | 28 u32 vertex_count | 32 u32 index_count | 36 u32 hardpoint_count
  40 u32 vat_rows (=44*model_count) | 44 u32 vat_cols (=48) | 48 u32 frames_per_model (=44) | 52..63 zero
Section table at offset 64: 8 slots x 16 bytes (only the first 5 used, rest zero): {u32 id,u32 offset,u32 size_bytes,u32 count}. First section at offset 192; each next starts at previous end rounded up to 64; padding zero; total_bytes = last end rounded up to 64.
Sections: 1 MODELS 96 B x model_count | 2 VERTICES 24 B x vertex_count | 3 INDICES u16 x index_count | 4 HARDPOINTS 16 B x hardpoint_count | 5 VAT (vat_rows*48*8 bytes, float16 little-endian, texel-major RGBA).
MODEL record (96 B):
  0 u64 id_hash | 8 i32 bmin[3] | 20 i32 bmax[3] (fx32 raw = rint(metres*65536), from LOD0 vertices)
  32 u32 vtx_base[3] (global vertex index of each LOD block) | 44 u32 idx_first[3] (in u16 elements) | 56 u32 idx_count[3]
  68 u16 vtx_count[3] | 74 u16 part_count | 76 u32 vat_row_base | 80 u16 hp_first | 82 u16 hp_count
  84 u16 tri_count_lod0 | 86 u8 category | 87 u8 flags (bit0 has team colour, bit1 idle is animated, bit2 has scroll uv)
  88 i32 radius_xz (fx32 max sqrt(x^2+z^2) over LOD0 vertices) | 92 u32 reserved=0
HARDPOINT record (16 B): {u8 kind; u8 part; u16 reserved=0; i32 x; i32 y; i32 z} (fx32 raw). The muzzle direction is the model's +X (turret aiming is a runtime matter).
Models are stored in catalogue order (section 6).

## 3. GROUP A: shared helpers, trims, octahedral normals
Files: tools/asset_common/{__init__.py, rng.py, hashing.py, pngio.py}; tools/model_baker/{requirements.txt, params.py, trims.py, octa.py, selftest.py}
- rng.py: class Rng (SplitMix64: state=(state+0x9E3779B97F4A7C15)&M64; z=state; z=((z^(z>>30))*0xBF58476D1CE4E5B9)&M64; z=((z^(z>>27))*0x94D049BB133111EB)&M64; return z^(z>>31)), uniform()=(next_u64()>>11)/2^53, randint(lo,hi) inclusive, shuffle (Fisher-Yates from the end). If tools/theatre_baker/rng.py exists and passes the vectors below, copy it.
- hashing.py: xxh64(data, seed=0) pure Python (standard XXH64; 32-byte stripes with struct.unpack_from, then 8/4/1-byte tails), fnv1a64(data: bytes|str) -> int.
- pngio.py: write_png(path, rgb_uint8 ndarray [H,W,3]) with stdlib only (zlib+struct, filter byte 0 per row, CRC via zlib.crc32); read_png_info(path) -> (w,h) validating signature and IHDR; decode_png_rgb(path) -> ndarray for tests (supports only what write_png produces).
- params.py: every numeric constant of this spec (clips, VAT layout, LOD tables, tunables, budgets of section 6.1, tolerances). No magic numbers elsewhere.
- trims.py: TRIMS list (id, name, col, row, px rect, preview_rgb); trim_id(name); trim_rect_uv(id) -> (u0,v0,u1,v1) float64; `python trims.py --write` writes assets/source/trims.json (id,name,col,row,px:[x0,y0,x1,y1],preview_rgb), sorted by id, json indent=2, sort_keys=True.
- octa.py: encode(n float array [k,3]) -> int16 [k,2]: n normalised in float64; p = n.xy/(|x|+|y|+|z|); where n.z<0: p = (1-|p.yx|)*s with s=+1 where p>=0 else -1 (per component); snorm = rint(clip(p,-1,1)*32767). decode(int16 [k,2]) -> float64 [k,3]: p=snorm/32767; v=(p.x,p.y,1-|p.x|-|p.y|); where v.z<0: v.xy=(1-|v.yx|)*s(v.xy) (s as above); normalise. Also unorm16(x)=rint(clip(x,0,1)*65535) -> uint16 and its inverse.
selftest.py supports `--group A..H` and `--group ALL`; each test prints its name and PASS/FAIL; groups print "GROUP X PASS (n/n)".
Group A tests (6):
 1 Rng(0) first three outputs == 0xE220A8397B1DCDAF, 0x6E789E6AA1B965F4, 0x06C45D188009454F; 1000 seeds give 1000 distinct first outputs.
 2 xxh64(b"")==0xEF46DB3751D8E999, xxh64(b"a")==0xD24EC4F1A98C6E5B, xxh64(b"abc")==0x44BC2CF5AD770999; 1000 Rng bytes hashed as bytes and as memoryview agree; fnv1a64("")==0xCBF29CE484222325, fnv1a64("a")==0xAF63DC4C8601EC8C.
 3 octa: 20000 random unit vectors (from Rng) round-trip with angular error < 0.05 degrees; the six axis vectors (+-X,+-Y,+-Z) decode within 1e-4 of the original; encode/decode of (0,0,-1) works (no NaN).
 4 trims: exactly 32 trims, ids 0..31 contiguous, names unique and equal to the list in 1.5; all rects inside the sheet; all pairwise rect gaps >= 8 px; trim_rect_uv(0) == (4/2048, 4/2048, 252/2048, 252/2048) within 1e-12; trims.json written and re-read equal; preview_rgb all ints in 0..255.
 5 pngio: write a 4x3 test image, read_png_info == (4,3), decode_png_rgb equals the source, and a flipped CRC byte makes read_png_info raise ValueError.
 6 unorm16: values 0,0.5,1 map to 0,32768 (round-half-even is NOT required; assert abs(x-32767.5)<=1),65535; inverse error <= 1/65535 over 10000 values.
CHECK A: `python tools/model_baker/selftest.py --group A` prints "GROUP A PASS (6/6)".

## 4. GROUP B: primitives + mesh builder
Files: tools/model_baker/{prims.py, meshbuilder.py}; extend selftest.py (--group B).
### 4.1 prims.py: every primitive returns Piece(pos f32[n,3], nrm f32[n,3], uv f32[n,2] trim-local 0..1, tris int32[m,3]), centred at the origin, closed (watertight), CCW outward. Detail comes from `lod` (0..2) and params tables.
 box(sx,sy,sz) | taper_box(bx,bz,tx,tz,h,dx=0.0): rect bx x bz at y=-h/2, rect tx x tz at y=+h/2, top shifted by dx along X; tx,tz > 0 | cylinder(r0,r1,h,lod,caps=(True,True)): axis Y, r0 at bottom y=-h/2, r1 at top; if r0==0 or r1==0 it is a cone (triangle-fan sides, ONE triangle per segment with its own apex vertex); segs = CYL_SEGS[lod]; smooth side normals n=normalize(h*cos t, r0-r1, h*sin t); caps flat | sphere(r,lod,flat=False,jitter=None): UV sphere with (lat,lon)=SPHERE_SEGS[lod], pole triangle fans; jitter=(amp,seed) (requires flat=True) displaces each vertex radially by r*(1+amp*f(dir)) with f(d)=sin(3.1*dx+p1)*cos(2.7*dy+p2)*sin(3.7*dz+p3), p1..p3=Rng(seed).uniform()*2pi; amp <= 0.25; flat=True gives per-triangle normals from final positions | wedge(sx,sy,sz): right-triangle prism, cross-section in XY with vertices (-sx/2,-sy/2),(+sx/2,-sy/2),(+sx/2,+sy/2), extruded along Z | prism(poly, h): convex polygon [(x,z),...], extruded from y=-h/2 to +h/2; the function reorders the polygon so the top-face normal is +Y; asserts convexity.
 UVs: every FLAT face (box, taper_box, wedge, prism, cylinder caps use disc mapping u=0.5+0.5cos, v=0.5+0.5sin instead) uses planar_fit(face_vertices): tangent axis a = normalised first edge, b = normal x a; project, normalise by the bounding box to [0,1]x[0,1]. Cylinder side: u=i/segs (seam vertex duplicated), v=0..1 along the axis. Sphere: u=lon fraction, v=lat fraction (v=0 at the +Y pole).
### 4.2 meshbuilder.py
 place(piece, pos, rot_deg=(0,0,0), mirror_z=False) -> Piece: R = Ry(ry) @ Rx(rx) @ Rz(rz) (degrees, column vectors); p' = R p + pos; normals rotated by R. If mirror_z: AFTER placing, negate z of positions and normals and swap triangle index columns 1 and 2 (keeps outward CCW).
 @dataclass Part: part (int index 0..15), make (callable lod -> Piece), pos, rot_deg, mirror_z (also emits the mirrored copy in the SAME part), trim (name), team=False, emissive=False, scroll=False, keep=False, name="".
 assemble(parts, lod, mask=None) -> Mesh(pos f32, nrm f32, uv_sheet f32, trim u8, flags u8, part u16, tris u32 [m,3]): concatenates pieces (mask: optional list of bool per Part, True=include), converts trim-local uv to SHEET uv with trims.trim_rect_uv(trim_id), sets trim/flags/part per vertex. Use `mirror_z=True` as: emit original + mirrored copy.
Group B tests (6) (all geometry tests build pieces at lod 0 unless stated):
 1 box(2,3,4): 12 tris, 24 verts, signed volume (sum dot(v0,cross(v1,v2))/6) == 24 within 1e-5, and every stored vertex normal has dot > 0.999 with its triangle's geometric normal.
 2 cylinder(1,1,2,lod=0): volume == 0.5*12*sin(2pi/12)*1^2*2 within 1e-4 (regular 12-gon prism); cone cylinder(1,0,2,0) and cylinder(0,1,2,0) have positive volume and no zero-area triangle (min area > 1e-9); taper_box, wedge(2,1,3) volume == 3.0 within 1e-5, prism of a 5-point convex hull: all volumes positive and equal to polygon-area*h within 1e-4.
 3 CLOSED check on all prims (box, taper_box, cylinder, cone, sphere lod 0/1/2, sphere flat+jitter(0.2,7), wedge, prism): weld vertices by position rounded to 1e-5; every undirected edge is shared by exactly two triangles; sphere volume between 0.80 and 1.0 of 4/3*pi*r^3.
 4 place(): box rotated (0,90,0) swaps the x and z bounding extents of box(1,2,3) exactly within 1e-5; mirror_z of an off-centre box gives z-negated bounds, positive signed volume and the same triangle count; rot order check: a point (1,0,0) with rot (0,0,90) lands at (0,1,0) within 1e-6.
 5 uv: for every prim all trim-local uv in [0,1]; assemble() with a trim whose rect is known yields sheet uv strictly inside that rect (with 1e-7 slack); trim/flags/part arrays carry the Part settings (team=True -> bit0, emissive -> bit1, scroll -> bit2).
 6 LOD tables: tri count of sphere flat is 168/80/36 at lod 0/1/2; cylinder(1,1,2,lod) tri counts are 44/28/20; assemble with mask drops exactly the masked part's vertices.
CHECK B: "GROUP B PASS (6/6)".

## 5. GROUP C: animation ops, rig, VAT
Files: tools/model_baker/{anim.py, rig.py, vat.py}; extend selftest.py (--group C).
### 5.1 anim.py: AnimOp factories; all return an object with matrix(t, pivot) -> 4x4 float64. A rotation op is T(pivot) R T(-pivot); a shift op is a translation. All are exactly identity at t=0:
 sway(axis, amp_deg, cycles=1): angle = amp*sin(2*pi*cycles*t) | spin(axis, turns): angle = 360*turns*t (turns integer) | bob(axis, amp_m, cycles=1): offset = amp*sin(2*pi*cycles*t) along that axis | pulse(axis, amp_deg, peak=0.35): angle = amp*e(t), where e rises 0->1 on [0,peak] with smoothstep and falls 1->0 on [peak,1] with smoothstep (e(0)=e(1)=0) | kick(axis, amp_m, peak=0.2): offset = amp*e(t) along the axis (same e, peak default 0.2) | tip(axis, final_deg): angle = final*t*t | sink(axis, final_m): offset = final*t (axis is the world axis index 0,1,2 = X,Y,Z; rotation about +axis by positive angle, right-handed).
### 5.2 rig.py
 class Rig: add_part(name, pivot, parent=-1) -> index (the first part must be parent -1; others need parent in range and < own index); set_ops(part, clip_name, [ops]); world_matrices(clip_index, frame) -> float64 [16,4,4]: local = product of the part's ops in listed order (op1 @ op2 @ ...), world(part) = world(parent) @ local; unused parts identity. Child anims happen first in bind space and the parent's afterwards.
 validate(): all parents chain to 0; at most 16 parts.
### 5.3 vat.py
 clip constants from params (CLIPS=[("Idle",8,True),("Move",16,True),("Attack",8,False),("Die",12,False)], CLIP_START=[0,8,24,32]).
 build_model_rows(rig) -> float32 [44,16,3,4] (top 3 rows of each world matrix). pack_half(rows) -> float16 [44,48,4] with texel (part*3+r) = rows[f,part,r,:]. build_vat(rigs) -> float16 [44*len(rigs),48,4].
Group C tests (6):
 1 rotation about a pivot: a point at the pivot is fixed by sway/tip about it; with pivot (1,0,0), tip(axis Z, 90) at t=1 maps (2,0,0) to (1,1,0) within 1e-9.
 2 shifts: bob(Y,0.5,1) at t=0.25 gives dy=0.5; kick(X,1.0) at t=0.2 gives dx=1.0 and at t=1.0 gives 0; sink(Y,-2) at t=1 gives dy=-2.
 3 chain: rig with root pivot (0,0,0) and child pivot (2,0,0): root tip(Y,90) at t=1 + child tip(Z,90) at t=1: world of child maps bind point (3,0,0) to the hand-computed result (compute it in the test from first principles, assert within 1e-9). Also a rig with a bad parent order raises ValueError.
 4 zero-at-origin: for every op kind, at t=0 matrix == identity exactly (tol 1e-12); looping ops with integer cycles/turns also return identity at t=1 (frame F equals frame 0); pulse/kick are identity at t=1; Die final frame (t=1) of tip(Z,90) equals the 90-degree rotation matrix.
 5 pack_half: shape (44,48,4), dtype float16; identity rows pack exactly (1.0/0.0); a random-rotation rig round-trips with every entry error <= 2e-3 and row lengths of the 3x3 within 0.005 of 1; unused parts identity.
 6 build_vat for 2 synthetic rigs: shape (88,48,4); row for (model 1, clip Attack, frame 3) is 44+24+3=71; frame 0 of every clip identity for all parts.
CHECK C: "GROUP C PASS (6/6)".

## 6. GROUPS D, E, F: model generators + catalogue
Shared files (create in D): tools/model_baker/{model.py, lod.py, checks.py}
- model.py: @dataclass Hardpoint(kind, part, pos(x,y,z) metres); @dataclass Model(id, category, target_dim_m, parts, rig, hardpoints). helper P(prim_callable_with_args, part=0, at=(0,0,0), rot=(0,0,0), trim="steel_plain", team=False, emissive=False, scroll=False, mirror=False, keep=False) building a Part (the make callable is `lambda lod: prim(*args, lod=lod)` for lod-aware prims). All Idle/Move/Attack/Die frames come from the rig.
- lod.py: lod_masks(parts, lod): LOD0 = all; LOD1/LOD2 keep a part if part==0 or keep or (its LOD0 bbox diagonal >= DROP_FRAC * model LOD0 bbox diagonal).
- checks.py: the 7 MODEL CHECKS (below) as functions taking a list of Models and returning (ok, messages); used by selftest groups D/E/F/ALL. Also bake-time catalogue builder `build_catalogue()` in catalogue.py (below).
- catalogue.py: ALL_MODELS in this fixed order: infantry, vehicle (table order), air, naval, building, prop, resource, projectile. Group D registers infantry+vehicle (23 models), E adds air+naval (36), F adds the rest (71). Each tables_* module exports MODELS (list of Model). catalogue.py imports whichever exist.
### 6.1 The 7 MODEL CHECKS (run over every registered model; each is ONE selftest test)
 M1 structure: count == expected for the group (23/36/71) with the category counts in the tables below; ids unique and lowercase [a-z0-9_]; part_count <= 16 and parts contiguous from 0; Rig.validate() passes.
 M2 geometry (LOD0): all finite; every normal unit within 1e-3; per PART: signed volume > 0 and CLOSED (every welded edge shared by exactly two triangles); min triangle area > 1e-10; every part has >= 1 triangle; every vertex part < part_count.
 M3 size/placement: largest AABB dimension within +-25% of target_dim_m (tables). Ground categories (0,1,4,5,6): min y within [-0.05, 0.10]. Naval: min y in [-14,-0.3] and max y > 0.5. Vehicle/Air/Naval/Projectile: X extent >= 0.9 * Z extent.
 M4 budgets: LOD0 triangles within [min,max] per category: Infantry 150..700, Vehicle 150..2200, Air 80..1800, Naval 200..3500, Building 200..5000, Prop 40..900, Resource 40..900, Projectile 20..200. LOD1 <= LOD0 and LOD2 <= LOD1 always; ratio rule of 1.4 when LOD0 > 150; every LOD <= 65535 vertices; every LOD has >= 1 triangle.
 M5 animation: for every clip, frame 0 of every part is identity within 1e-9; all matrices finite; every rotation 3x3 has determinant within 1e-6 of 1; Idle clip of animated models changes (flag bit1 computed = any Idle frame differs from identity).
 M6 hardpoints and team colour: hardpoint parts < part_count, positions inside the LOD0 AABB expanded by 1.0 m; required hardpoints per section 6.2; for categories 0-4 the team-colour AREA share of LOD0 (sum of triangle areas with flag bit0 / total area) is within [TEAM_AREA_MIN, TEAM_AREA_MAX]; categories 5-7 must have no team-flagged vertex.
 M7 determinism: building every model twice gives byte-identical assembled LOD0..2 arrays and VAT rows.
### 6.2 Generators (every generator also creates its Rig and Hardpoints)
All dimensions below are guidance, the CHECKS decide. Keep every model <= 16 parts; most need far fewer. Every anim parameter not listed defaults to "none" (identity). Each rig gives ALL non-root parts the root as an ancestor.
G1 humanoid(spec) [gen_humanoid.py], scale s=height/1.8. Parts: 0 torso (root, pivot (0,0,0)), 1 leg_l, 2 leg_r, 3 arm_l, 4 arm_r, 5 weapon (parent 4), 6 head, 7 extra (shield/backpack). Legs 0.16x0.85x0.18 boxes at z=+-0.11 (pivot at hip y=0.85); torso taper_box ~0.30x0.46 -> 0.28x0.50, h 0.62 at y=1.16 (team=True); head sphere r~0.11 at y~1.62 plus headgear; arms boxes 0.12x0.62x0.12 at z=+-0.29 (pivot at shoulder y~1.45). Weapon kinds: spear (cyl r0.025 length 2.3 + cone tip), bow (arc of 3 boxes), sword (thin box + guard), musket (box + long thin cylinder), rifle, rifle_short, launcher (cylinder r0.08 length 1.0 on shoulder), tool_pick (cyl handle + box head), controller (box + thin antenna). Headgear: helmet_bronze (cone over sphere), hood, helmet_crest, hat_tricorn, helmet_modern, helmet_nvg (modern + emissive small box), hat_straw. Extras: shield_round, shield_kite (cylinder disc / taper), pack (box on the back), quiver, none.
 Rig: Idle: torso bob(Y,0.012,1); arms sway(Z,+-2,1) opposite. Move: leg_l sway(Z,+35,1), leg_r sway(Z,-35,1), arm_l sway(Z,-25,1), arm_r sway(Z,+25,1), torso bob(Y,0.04,2). Attack by weapon: spear kick(X,+0.7) on weapon; sword pulse(Z,-80) on arm_r; bow/musket/rifle/rifle_short kick(X,-0.05) on weapon + pulse(Z,+4) on arm_r; launcher kick(X,-0.1) on weapon; tool_pick pulse(Z,-70) on arm_r; controller none. Die: torso tip(Z,+90) + sink(Y,-0.08). Hardpoint: Muzzle at the weapon tip for every weapon kind except tool_pick.
G2 mounted/siege/tracked/wheeled [gen_ground.py].
 mounted(kind lance|heavy): parts 0 body (root), 1 legs_front, 2 legs_back, 3 rider (parent 0, team=True torso), 4 weapon (parent 3), 5 head (parent 0). Horse body box ~2.0x0.7x0.55 at y~1.2, 4 legs as boxes merged per pair (pivot at hip), neck/head taper boxes. Rig: Move legs_front sway(Z,+28,1), legs_back sway(Z,-28,1), body bob(Y,0.05,2), head sway(Z,6,2). Attack: weapon kick(X,+0.8), rider pulse(Z,-10). Die: body tip(X,90) + sink(Y,-0.1). Muzzle at weapon tip.
 siege(kind catapult|cannon): catapult parts 0 base (root, team stripe), 1 wheels (spin Z 1 turn in Move), 2 arm (pivot at the axle top), 3 bucket (parent 2). Attack: arm pulse(Z,-85,peak 0.3). Cannon parts 0 carriage (root), 1 wheels, 2 barrel (pivot at trunnion): Attack: barrel kick(X,-0.7). Die: root tip(Z,8)+sink(Y,-0.2). Muzzle at bucket/barrel tip.
 tracked(spec): parts 0 hull (root; includes two tracks: rubber_tread, scroll=True, plus team stripes on the flanks), 1 turret (pivot at turret ring, parent 0), 2 barrel (parent 1, pivot at the trunnion), 3 extras (parent 1; e.g. APS pods or ATGM pods; static). Rig: Idle hull bob(Y,0.01,1), turret sway(Y,4,1). Move: hull bob(Y,0.03,2), hull sway(Z,0.6,2). Attack: barrel kick(X,-0.5). Die: hull tip(Z,6)+sink(Y,-0.25), turret tip(Z,18). Muzzle at the barrel tip; TurretPivot hardpoint at the ring.
 wheeled(spec): parts 0 chassis (root; cab + body, team stripe), 1 payload (parent 0), 2 payload2 (parent 1), then one part per axle 3..(3+n_axles-1) (parent 0; both wheels of the axle as two cylinders at z=+-w, axis along Z, pivot at the axle centre; spin(Z,1) in Move). Payload kinds: troop (apc: roof turret = payload, MG barrel = payload2), pods (himars: pod box = payload, pivot at the rear hinge, Attack pulse(Z,+55); payload2 unused), laser_cram (sam: dome = payload with emissive lens, gatling cluster = payload2, Attack spin(X,6) on payload2), mast (ew: mast + 3 flat panels = payload, Idle spin(Y,1) on payload), cargo (truck: static box), ugv (low flat deck, antenna). Die: chassis tip(Z,5)+sink(Y,-0.2).
 Required Muzzle: all vehicles except veh_ew, veh_truck, veh_ugv; Laser hardpoint (kind 6) additionally on veh_sam. TurretPivot optional.
G3 air [gen_air.py].
 fixed_wing(kind biplane|prop|jet|stealth|bomber): parts 0 body (root; fuselage = cylinder rotated to lie along X, wings = taper_boxes, tail fins, canopy emissive-free), 1 propeller (only biplane and prop kinds; pivot at the nose; spin(X,4) in ALL clips). stealth = faceted prism planform (flying wing) with no props. Rig: Idle: body bob(Y,0.02,1) + sway(X,0.5,1); Move: body sway(X,3,1) + bob(Y,0.1,2); Attack: body kick(X,-0.05); Die: body tip(X,100)+tip(Z,-50)+sink(Y,-2). Team flag on tail fins/wing roundels. Muzzle: nose (guns) / belly bay (bomber, stealth).
 rotary(kind helo|quad): helo parts 0 fuselage (root), 1 main_rotor (pivot at mast top; spin(Y,6) all clips; thin box blades), 2 tail_rotor (spin(Z,8)); quad (FPV drone, ~0.6 m): parts 0 body, 1..4 rotors (spin(Y,+-6) alternating sign). Rig Attack: helo body pulse(Z,-6); Die: tip(X,70)+tip(Z,-30). Helo Muzzle at the nose/stub wings; quad has no Muzzle.
G4 naval [gen_naval.py]: ship(kind). Hull = prism over polygon (x0=-len/2, x1=len/2, b=beam/2, xb = x1-0.28*len) [(x0,-0.85b),(xb,-b),(x1,0),(xb,b),(x0,0.85b)] from y=-draft to y=+freeboard. Parts: 0 hull+superstructure (root; team stripe on the hull side), 1 turret_a, 2 turret_b (both aim by Idle sway(Y,5,1)), 3 mast/radar (spin(Y,1) in Idle/Move for destroyer/carrier), galley: 1,2 = oar rows left/right (sway(Y,+-18,3) in Move, pivot at gunwale mid-length, mirror_z NOT used, make two separate parts), galleon: 3 = sails (Idle sway(Z,1.5,1)). Sub (nav_sub): hull = cylinder rotated (0,0,90) so its axis is X, cone ends via cylinders with r1=0, sail = box, mostly below the waterline; no turrets. Rig: root Idle bob(Y,0.12,1)+sway(Z,0.8,1); Move bob(Y,0.25,2)+sway(Z,1.5,2)+sway(X,1.0,1); Attack: turret kick(X,-1.2); Die: root tip(Z,-18)+tip(X,25)+sink(Y,-0.6*draft). Muzzle at the gun/tube tip (all naval; the galley gets a Muzzle at the bow ram, the sub at the bow tube).
G5 building(row) [gen_building.py]: parts 0 main (root: walls, roof, all static decor), 1 spinner/flag (animated decor A), 2 animated decor B. Roof kinds: flat, gable (wedge/prism), hip (taper_box), dome (sphere flat=False lod-aware), none. Decor vocabulary (static unless noted): chimney, corner_towers, columns, fields (flat boxes), stalls, runway (flat box), hangar, tower_tall, sam_launchers (3 slanted boxes), tank_farm (cylinders), pier, smokestack, silo_hatch, antenna_mast, flagpole (ANIMATED part 1: sway(Z,3,1)), dish (ANIMATED part 1: spin(Y,1) in Idle/Move), blades (windmill; ANIMATED part 1 spin(X,1)), pumpjack (ANIMATED part 2: sway(Z,20,1)), crane (ANIMATED part 2: sway(Y,15,1)). Die: root sink(Y,-0.45*height)+tip(Z,3). Hatch hardpoint at (len_x/2,0,0) at ground level for the production buildings listed below; Muzzle for bld_tower (top) and bld_sam_site. Team colour: flag/banner/roof stripe or sign.
G6 static props [gen_props.py] (Rng(fnv1a64(id)) for any variation): trees (trunk cylinder + 2..3 foliage cones/spheres; foliage as part 1 with Idle sway(Z,1.5,1); trunk part 0), rock (sphere flat with jitter amp 0.22, squashed by building it from a box-rotation hack or by non-uniform scale in the piece: allowed, implement `squash` by editing positions in the make callable), berry bush (3 spheres + red emissive-free small spheres), deer (box body, 4 leg boxes, head; Idle: head sway(Z,8,1)), gold/iron deposits (rock + gold/steel crystals or ingots), oil seep (dark flat cylinder + derrick of boxes; ANIMATED pumpjack part 1), silicon outcrop (cluster of cones in ice_crystal trim), relic (obelisk prism + small emissive sphere, Idle bob on part 1). Projectiles: arrow (thin cylinder + cone + 3 fin boxes), shell (cylinder + cone), rocket, missile (cylinder + cone nose + 4 fins + exhaust emissive), hypersonic (long slender cone/cylinder + 2 wedge fins), bomb (cylinder + sphere nose + fins). No animation for props (except as listed) and projectiles. Resources and props have NO team-coloured vertices.
Team colour: categories 0-4 only (see M6).
### 6.3 Catalogue tables. id | generator and key params | category | target largest dimension (m)
INFANTRY (cat 0), 9 models: inf_spear (humanoid, spear, shield_round, helmet_bronze, cloth_tunic) 2.6 | inf_bow (bow, quiver, hood) 1.9 | inf_sword (sword, shield_kite, helmet_crest, steel_plain torso) 1.9 | inf_musket (musket, hat_tricorn, cloth_dark) 1.9 | inf_rifle (rifle, helmet_modern, camo_woodland) 1.9 | inf_at (launcher, pack, camo_desert) 1.9 | inf_special (rifle_short, helmet_nvg, camo_urban) 1.9 | inf_worker (tool_pick, hat_straw, cloth_tunic) 1.9 | inf_drone_op (controller, helmet_modern, pack, camo_urban) 1.9
VEHICLE (cat 1), 14 models: cav_light (mounted lance) 3.2 | cav_heavy (mounted heavy, steel trims) 3.2 | siege_catapult 7.0 | siege_cannon 4.5 | veh_tank_early (tracked, hull ~5.2x2.5x1.1, small turret, 75mm-style barrel) 6.5 | veh_mbt (tracked, hull ~7.6x3.5x1.2, wedge turret, long barrel, APS pods as extras) 10.5 | veh_ifv (tracked, autocannon turret, ATGM pods) 7.0 | veh_arty_sp (tracked, big turret, very long barrel) 11.0 | veh_apc (wheeled, 4 axles, troop) 8.0 | veh_himars (wheeled, 3 axles, pods) 7.0 | veh_sam (wheeled, 3 axles, laser_cram) 8.5 | veh_ew (wheeled, 3 axles, mast) 8.0 | veh_truck (wheeled, 2 axles, cargo) 8.0 | veh_ugv (wheeled, 2 axles, ugv, tiny) 2.4
AIR (cat 2), 7 models: air_biplane 8.0 | air_prop (prop fighter) 9.5 | air_jet 16 | air_stealth (UCAV loyal wingman) 12 | air_bomber 40 | air_helo 16 | air_fpv (quad) 0.6
NAVAL (cat 3), 6 models: nav_galley 28 (draft 1.0) | nav_galleon 45 (draft 4) | nav_ironclad 70 (draft 5) | nav_destroyer 150 (draft 6) | nav_carrier 330 (draft 11, flight deck box + island) | nav_sub 100 (draft 6, mostly submerged)
BUILDING (cat 4), 18 models (largest of x,z,height): bld_city_center_a 30 (ancient: timber + stone, gable roof, flagpole, Hatch) | bld_city_center_b 36 (industrial: brick, dome/tower, flagpole, Hatch) | bld_city_center_c 44 (modern: concrete_panel + glass_window, tower, antenna_mast, dish, Hatch) | bld_house 9 | bld_farm 24 (barn + fields + blades) | bld_lumber 14 | bld_mine 14 (headframe tower) | bld_market 20 (stalls) | bld_barracks 24 (Hatch) | bld_factory 40 (smokestack, Hatch) | bld_airfield 90 (runway flat box + hangar + tower_tall, Hatch) | bld_dock 40 (pier + crane, Hatch) | bld_library 26 (columns + dome) | bld_tower 16 (tall, Muzzle) | bld_sam_site 20 (sam_launchers, Muzzle) | bld_radar 18 (dish) | bld_silo 16 (cylinders, silo_hatch, hazard_stripe) | bld_refinery 36 (tank_farm, smokestack, pumpjack)
PROP (cat 5), 4 models: prop_tree_oak 9 | prop_tree_pine 12 | prop_tree_palm 10 | prop_rock 3.5
RESOURCE (cat 6), 7 models: res_berry_bush 1.6 | res_herd 2.0 | res_gold 3.0 | res_iron 3.2 | res_oil 6.0 | res_silicon 3.0 | res_relic 4.0
PROJECTILE (cat 7), 6 models: proj_arrow 0.9 | proj_shell 0.8 | proj_rocket 2.0 | proj_missile 4.5 | proj_hypersonic 6.0 | proj_bomb 2.2
Files per group: D = lod.py, model.py, checks.py, gen_humanoid.py, gen_ground.py, tables_ground.py (infantry + vehicle tables), catalogue.py | E = gen_air.py, gen_naval.py, tables_air_naval.py | F = gen_building.py, gen_props.py, tables_static.py.
Build in this order inside each group: first ONE model of each generator and run checks on those, then the rest. Group D must be finished before you begin E, and so on.
CHECK D: `python tools/model_baker/selftest.py --group D` prints "GROUP D PASS (7/7)" (7 MODEL CHECKS over 23 models).
CHECK E: "GROUP E PASS (7/7)" (36 models).
CHECK F: "GROUP F PASS (7/7)" (71 models).
If an M-check fails for a reason that is a spec impossibility, follow rule 0. Never change budgets/tolerances.

## 7. GROUP G: writer, bake pipeline, preview
Files: tools/model_baker/{mformat.py, bake_models.py, preview.py}; extend selftest.py with --group G and --group ALL.
- mformat.py: write_models(path, models_data) builds the exact section 2 layout from assembled LOD meshes, hardpoints and VAT (float16 little-endian bytes via .astype('<f2').tobytes()), computes xxh64 over [64,total) and writes atomically (.tmp then os.replace). read_models(path) -> dict with header fields and numpy views of every section (structured dtypes for MODELS/VERTICES/HARDPOINTS), verifying magic, version, total size, section table, and hash; raises ValueError with a clear message otherwise.
- bake_models.py CLI: `python tools/model_baker/bake_models.py [--out assets/compiled/models] [--no-preview]`. Pipeline: build_catalogue() -> run all 7 MODEL CHECKS (any failure prints messages and exits 3) -> assemble LOD0..2 with lod_masks -> quantise (octa.encode, unorm16) -> verify per-LOD vertex counts <= 65535 -> VAT -> write_models -> manifest json (sort_keys=True, indent=2: for each model id, id_hash as 16-digit hex, category, tris per LOD, vtx per LOD, parts, target_dim_m, bbox in metres, hardpoint count) -> preview. Print "[stage] name seconds" lines. Exit codes: 0 ok, 3 checks failed. Determinism: two bakes give identical file bytes. Writes ONLY into --out.
- preview.py: write_preview(path_png, models): contact sheet 12 columns x 6 rows of 160x160 px tiles (1920x960 RGB), background (28,30,36); models in catalogue order, row-major (71 tiles used, last tile empty). Software rasteriser, orthographic: view yaw 35 deg and pitch 30 deg around the model centre, uniform scale fitting the projection into 144x144 with the model centred in the tile; z-buffer float32; back-face cull; flat shading = 0.35 + 0.65*max(0, dot(normal, normalize(-0.4,0.8,0.45))); colour = trim preview_rgb * shade (team-flagged vertices blend 50% with (200,40,40)); rasterise each triangle via a numpy barycentric test inside its pixel bounding box. Draw a 1 px darker border around each tile.
Group G tests (5) (bake into a temp dir; use --no-preview for the second bake):
 1 bake exits 0; read_models OK; header: model_count 71, vat_rows 71*44 == 3124, vat_cols 48, frames_per_model 44, section_count 5; flipping one payload byte makes read_models raise ValueError.
 2 second bake: file sha256 identical to the first.
 3 manifest: 71 entries, unique id_hash, each equals fnv1a64(id) recomputed; category counts 9/14/7/6/18/4/7/6.
 4 preview PNG: valid signature, size 1920x960, and at least 68 of the 71 model tiles contain >= 300 non-background pixels.
 5 limits: file <= 24 MB, vertex_count <= 450000, vat_rows <= 8192, every LOD vertex count <= 65535, index_count % 3 == 0 per LOD, total index values < 65536.
CHECK G: "GROUP G PASS (5/5)". Then run the real bake: `python tools/model_baker/bake_models.py`. Expected: exit 0, about 5-8 MB. Tell me the path of models_2026.preview.png.
Final python count: `python tools/model_baker/selftest.py --group ALL` -> "ALL GROUPS PASS (30/30)" (A 6 + B 6 + C 6 + MODEL CHECKS 7 over 71 models + G 5).

## 8. GROUP H: C++ ModelData loader + 6 tests (total tests previous + 6, expected 45)
Create NEW static library target `ron_assets` (src/assets/model_data.hpp, src/assets/model_data.cpp) linking ron_core, using the same warning flags/static link flags as ron_core except that float is allowed here; keep -fno-exceptions -fno-rtti. Add tests/test_models.cpp to the existing test executable (link ron_assets). Copy the Gate 05/06 pattern for file lookup under assets/compiled/ and for the file-reading helper. No heap allocation: load into the permanent arena as GameDb/TheatreData do.
### 8.1 API (namespace ron::assets; reuse core types)
 enum class ModelError : u8 {Ok, TooSmall, BadMagic, BadVersion, BadSize, BadHeader, BadSectionTable, BadSectionSize, BadHash, BadModelTable};
 struct ModelRec (96 B), VertexRec (24 B: float x,y,z; i16 nx,ny; u16 u,v; u8 trim; u8 flags; u16 part), HardpointRec (16 B); exact byte layouts of section 2; static_assert sizeof and offsetof of id_hash, bmin, vtx_base, idx_first, idx_count, vtx_count, vat_row_base, category, radius_xz.
 class ModelData: ModelError init_from_blob(const u8*, usize) with this chain in order: size>=64 (TooSmall) -> magic -> version==1 -> total_bytes==size (BadSize) -> section_count==5 && vat_cols==48 && frames_per_model==44 && 1<=model_count<=256 && vat_rows==44*model_count (BadHeader) -> table: ids 1..5 in order, offset%64==0, offset>=192, offset+size<=total (BadSectionTable) -> exact sizes (models*96, vertices*24, indices*2, hardpoints*16, vat_rows*48*8) (BadSectionSize) -> xxh64(data+64,size-64,0)==content_xxh64 (BadHash, Gate 01 xxh64) -> every model's ranges (vtx_base+vtx_count<=vertex_count, idx_first+idx_count<=index_count, hp_first+hp_count<=hardpoint_count, idx_count%3==0, part_count<=16) (BadModelTable).
 ModelError load_file(const char*, MemoryArena&); u32 model_count(); const ModelRec& model(u32); i32 find(u64 id_hash) const (-1 if absent); const VertexRec* vertices(); u32 vertex_count(); const u16* indices(); u32 index_count(); const HardpointRec* hardpoints(); u32 hardpoint_count(); const u16* vat_halfs(); u32 vat_rows();
 static u64 fnv1a64(const char*); static void decode_oct(i16 x, i16 y, float out[3]) (same maths as octa.py); static float half_to_float(u16) (manual, handles zero/subnormal/normal/inf).
 Constants: constexpr u32 kFramesPerModel=44, kClipStart[4]={0,8,24,32}, kVatCols=48. Add the decode of a single vat matrix: bool vat_matrix(u32 model, u32 clip, u32 frame, u32 part, float m[12]) const.
### 8.2 The 6 tests (tests/test_models.cpp, names exactly; missing file -> FAIL with message "run: python tools/model_baker/bake_models.py")
 1 Model_LoadHeader: loads assets/compiled/models/models_2026.mdbin; fields as specified (model_count 71, vat_rows 3124); copy with a flipped payload byte -> BadHash; first byte changed -> BadMagic; truncated by 64 B -> BadSize; 10-byte buffer -> TooSmall (copies in an arena/scratch, no heap).
 2 Model_Lookup: fnv1a64("")==0xCBF29CE484222325 and ("a")==0xAF63DC4C8601EC8C; all 71 id hashes unique; find("inf_rifle"), find("veh_mbt"), find("nav_carrier"), find("bld_city_center_c"), find("proj_hypersonic") are >= 0 and model(idx).id_hash equals the hash; find("nope")==-1; categories of those five equal 0,1,3,4,7.
 3 Model_BufferIntegrity: per model and LOD: ranges in bounds; every index < that LOD's vtx_count; idx_count%3==0 and > 0; LOD counts non-increasing; no triangle has two equal indices; tri_count_lod0 == idx_count[0]/3; fx32 bounds satisfy bmin<bmax on every axis.
 4 Model_VertexDecode: for every vertex: positions finite and inside the model's fx32 AABB expanded by 0.01 m; decoded normals have length within 0.002 of 1; uv in [0,1] (u16 domain ok); trim < 32; part < part_count. For inf_rifle, veh_mbt, nav_destroyer, bld_factory, air_jet: for >= 99% of LOD0 triangles, dot(average decoded vertex normal, geometric normal from positions) > 0 (checks winding/normal agreement).
 5 Model_VatIntegrity: for every model: frame 0 of each of the 4 clips is the identity (all 16 parts, tolerance 1e-3); all matrices finite; every 3x3 row length within 0.01 of 1; for models flagged animated-idle (flags bit1) at least one Idle frame differs from identity by > 1e-3; bld_barracks has no Idle motion; air_helo has Idle motion.
 6 Model_Hardpoints: ranges in bounds; kinds <= 7; part < part_count; every position inside the model fx32 AABB expanded by 1.0 m; models inf_rifle, veh_mbt, air_jet, nav_destroyer, bld_tower each have >= 1 Muzzle (kind 0); bld_barracks, bld_factory, bld_dock each have >= 1 Hatch (kind 4); veh_truck has no Muzzle.
Expected total: previous + 6 (45 if Gate 06 was 39).

## 9. FINAL VERIFICATION (run all, show the output tail of each)
 1. python tools/model_baker/selftest.py --group ALL   -> "ALL GROUPS PASS (30/30)"
 2. python tools/model_baker/bake_models.py            -> exit 0 (re-run only if the file is missing)
 3. node tools/schema_compiler/compile_db.mjs --check  -> still passes
 4. Release build + test runner                        -> all pass (45/45 if Gate 06 was 39/39)
 5. Debug build + test runner                          -> same count, 0 warnings in both builds
 6. docs/spec_deviations.md updated (write "none" if nothing deviated)
 Report: test counts (Release/Debug), selftest count, which LOD tunable values ended up in params.py, total triangles per category at LOD0, vertex_count, index_count, file size in MB, vat_rows, bake seconds per stage, and the path of the preview PNG.
````

---

**Add this one line to GEMINI.md** (Antigravity can do it at the start of the task):

`tools/asset_common/` holds Python helpers shared by all asset bakers. Bakers never import from each other. Bake outputs live in assets/compiled and are git-ignored; assets/source holds committed inputs.

**What to send back:** reply **"Gate 08"** when the selftest says `ALL GROUPS PASS (30/30)` and the C++ tests are green in both builds. Open `models_2026.preview.png`. You should see a grid of about 70 chunky low-poly models: soldiers, tanks, aircraft, ships, buildings, trees and missiles. They'll be flat-coloured because the textures come in Gate 08, and some will look crude.

**If something fails**, paste only:
1. The group letter.
2. The first failing assertion or model-check message, with file:line.
3. The last 30 lines of output.

Group D is the most likely place to stall, because it's the first time the checks run on real models. If one model keeps failing, tell me its id and the check number.
</USER_REQUEST>
<ADDITIONAL_METADATA>
The current local time is: 2026-11-13T11:28:58+05:30.
</ADDITIONAL_METADATA>