# Digger for Unreal — User Guide

Welcome to the documentation for **Digger for Unreal**, a voxel-based terrain carving and building tool for Unreal Engine.

---

## Getting Started

- Enable the plugin in Unreal.
- Enter **Digger Mode** from the toolbar.
- Select a brush and begin digging or adding terrain.

---

## Quickstart for Artists

1. Use a Sphere brush, Dig mode, radius ~400, falloff ~0.3.
2. Drag to create a depression; enable Continuous for smooth strokes.
3. Switch to Add to build a mound.
4. Bake to Nanite for performance.

---

## Quickstart for Technical Artists

- Set up a Hole Mesh Material with layered sediments.
- Use triplanar mapping and macro noise for de-tiling.
- Assign material to baked meshes.

---

## Quickstart for Programmers

- Call `ApplyBrushStroke` API from C++/Blueprint.
- Use interpolated strokes or spline brushes for performance.
- Ensure mesh regeneration runs on the game thread.

---

## Concepts

### Voxels, SDF & Marching Cubes

- Brushes modify the Signed Distance Field (SDF).
- Marching Cubes converts voxels to triangles.

### Grid & Subdivisions

- TerrainGridSize sets base voxel size.
- Subdivisions increase detail.

### Landscape Seams

- Hidden Seam for seamless blending.
- Rim height can be set to 0 for flush joins.

---

## Brush System

- Operations: Dig / Add.
- Shapes: Sphere, Cube, Custom SDF.
- Modes: Continuous, Interpolated, Spline.
- Undo queue per chunk.

---

## Baking & Islands

- Bake areas to static meshes with Nanite.
- Detect islands, extract them as props, and enable physics.

---

## Lighting

- Spawn Point/Spot/Directional lights via brush clicks.
- Spotlights can align to surface normals.

---

## How-To Recipes

- **Dig a mound:** Sphere brush, Add mode, bake when satisfied.
- **Cut a tunnel:** Use Spline brush, Apply Along Spline.
- **Custom brush:** Convert a Static Mesh to SDF brush.
- **Extract island:** Detect, Extract to Actor, Simulate Physics.

---

## Troubleshooting

- **HoleBP is null:** Assign `/Game/DiggerEditorMode/BP_MeshHole`.
- **Lights all Point:** Ensure correct subclass is spawned.
- **Crash IsInGameThread:** Run landscape queries on game thread.
- **Invisible top:** Fix flipped normals or adjust seam settings.

---

## FAQ

- **Do I need Nanite?** Recommended for baked meshes.
- **Can I edit at runtime?** Possible but costly.
- **Why seams?** Enable Hidden Seam & precise sampling.

---

## Glossary

- **SDF:** Signed Distance Field.
- **Voxel:** 3D pixel of data.
- **Marching Cubes:** Meshing algorithm.
- **Chunk:** Partition of voxel data.
- **Subdivision:** Extra resolution.
- **Hidden Seam:** Seam blend mode.
- **Island:** Continuous geometry.
- **Bake:** Convert live edits to static mesh.
