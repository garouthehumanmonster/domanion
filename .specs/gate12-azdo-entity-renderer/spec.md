# Spec: Gate 12 AZDO Entity Renderer
Project: DOMINION 2026 (ron2026)

## System Overview
High-performance AZDO entity renderer supporting up to 65,536 instances and 128 models with 3 LODs each:
- Render-side EntityInstance (64 B, std430)
- GPU frustum + projected-size culling compute pass (entity_cull_count, entity_cull_scan, entity_cull_scatter)
- Prefix scan to generate DrawElementsIndirectCommand structs for glMultiDrawElementsIndirect
- VAT (Vertex-Animation Texture) GPU sampling with per-part 3x4 matrices (44 frames, 4 clips: Idle, Move, Attack, Die)
- Trim sheet materials (albedo, normal, ORM, emissive) with LOD clamp at mip 5
- Reverse-Z HDR rendering between terrain and ocean passes

## Architectural Invariants
1. Simulation/Render Split: src/sim never touches render entities. Sim -> Render extraction deferred to future gates.
2. Static VRAM ledger freeze: ModelGeometry (<=128 MiB), TrimTextures (<=64 MiB), VatTextures (<=32 MiB), EntityBuffers (<=40 MiB). Total <= 2304 MiB.
3. Zero runtime GPU or heap allocations per frame. Persistent mapped ring for instance feeds.
4. Deterministic GL 4.6 DSA operations. Reverse-Z depth testing (GL_GREATER).
