# Gate 07: Model Kitbasher + Vertex-Animation Textures + C++ ModelData Loader

## Summary
- Tools: `tools/asset_common/`, `tools/model_baker/`
- Binary: `assets/compiled/models/models_2026.mdbin` + `models_2026.manifest.json` + `models_2026.preview.png`
- Source: `assets/source/trims.json`
- C++ Static Library: `ron_assets` (`src/assets/model_data.hpp`, `src/assets/model_data.cpp`)
- C++ Tests: `tests/test_models.cpp` (6 tests: Model_LoadHeader, Model_Lookup, Model_BufferIntegrity, Model_VertexDecode, Model_VatIntegrity, Model_Hardpoints)

## Plan: Groups A - H
- Group A: `tools/asset_common/{__init__.py, rng.py, hashing.py, pngio.py}` + `tools/model_baker/{requirements.txt, params.py, trims.py, octa.py, selftest.py}`
- Group B: `tools/model_baker/{prims.py, meshbuilder.py}`
- Group C: `tools/model_baker/{anim.py, rig.py, vat.py}`
- Group D: `tools/model_baker/{model.py, lod.py, checks.py, gen_humanoid.py, gen_ground.py, tables_ground.py, catalogue.py}` (23 models)
- Group E: `tools/model_baker/{gen_air.py, gen_naval.py, tables_air_naval.py}` (+13 = 36 models)
- Group F: `tools/model_baker/{gen_building.py, gen_props.py, tables_static.py}` (+35 = 71 models)
- Group G: `tools/model_baker/{mformat.py, bake_models.py, preview.py}`
- Group H: `src/assets/model_data.hpp`, `src/assets/model_data.cpp`, `CMakeLists.txt`, `tests/test_models.cpp`
