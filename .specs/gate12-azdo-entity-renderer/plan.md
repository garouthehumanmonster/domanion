# Implementation Plan: Gate 12 AZDO Entity Renderer

## Group Execution Structure
- Group A: Types, CPU math, model adapter, test_entity_cpu (k=6, T11+6=57)
- Group B: GPU assets (geometry VBO/IBO/SSBO, VAT texture, trim textures), test_entity_assets (k=3, T11+9=60)
- Group C: GPU culling compute pipeline (count, scan, scatter), test_entity_cull (k=4, T11+13=64)
- Group D: Draw path (entity_draw.vert/frag, TerrainRenderer draw order, overlays), test_entity_draw (k=5, T11+18=69)
- Group E: ron_view crowd demo, performance verification, VRAM validation (k=2, T11+20=71)
- Group F: Final verification, receipts, documentation
