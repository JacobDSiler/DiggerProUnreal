# Testing Guide for Dual Contouring Fix

## Overview
This guide helps verify that the Dual Contouring mesh generation fix produces watertight meshes without degeneracy.

## Prerequisites
- Unreal Engine 5.2 or compatible version
- DiggerProUnreal project loaded
- A level with a DiggerManager actor

## Test Scenarios

### Test 1: Basic Watertight Mesh
**Objective:** Verify the mesh has no visible holes

**Steps:**
1. Open a test level with DiggerManager
2. Set `MeshGenerationMethod` to `DualContouring` in DiggerManager settings
3. Create a simple cube of voxels using the digging/building tools
4. Observe the generated mesh

**Expected Result:**
- The mesh should be completely solid with no holes
- All faces should be properly connected
- No floating triangles or gaps

### Test 2: Complex Terrain
**Objective:** Verify watertight mesh on complex geometry

**Steps:**
1. Create an irregular terrain shape with caves and overhangs
2. Use various digging operations to create complexity
3. Inspect the mesh from different angles, especially at edges

**Expected Result:**
- All surfaces should be watertight
- Cave interiors should be properly meshed
- Overhangs and undercuts should have proper geometry

### Test 3: Performance Check
**Objective:** Ensure acceptable performance

**Steps:**
1. Monitor frame rate before and after the fix
2. Generate meshes for several chunks
3. Compare triangle counts and generation time

**Expected Result:**
- Frame rate should be similar to before (within 10%)
- Triangle count may be slightly higher (expected)
- Mesh generation time should not increase significantly

### Test 4: Landscape Integration
**Objective:** Verify no extraneous mesh above/below landscape

**Steps:**
1. Dig into terrain below the landscape surface
2. Check that no mesh appears above the landscape
3. Verify underground cavities are properly meshed

**Expected Result:**
- No mesh should appear above the landscape surface
- Underground geometry should be watertight
- Transition between landscape and voxel mesh should be clean

## Visual Inspection Checklist

When inspecting the generated mesh, look for:
- [ ] No visible holes or gaps
- [ ] No degenerate (zero-area) triangles
- [ ] Proper face normals (smooth lighting)
- [ ] No z-fighting or flickering
- [ ] Clean edges where chunks meet
- [ ] Proper collision (can walk on surface without falling through)

## Debugging Tips

If you encounter issues:

1. **Enable Debug Logging:**
   - Check the log output for "DC mesh: X verts, Y tris" messages
   - Compare vertex and triangle counts before/after

2. **Visualize Wireframe:**
   - Use Unreal's wireframe view mode (Alt+2) to see triangle structure
   - Look for missing triangles or irregular patterns

3. **Check Collision:**
   - Enable collision visualization (Show > Collision)
   - Walk around and test for gaps in collision

4. **Performance Profiling:**
   - Use Unreal's profiler to measure `GenerateDCMeshFromGrid` time
   - Compare against Marching Cubes method for reference

## Expected Metrics

Based on a typical 32³ voxel chunk:

**Before Fix:**
- Vertices: ~500-2000 (depending on complexity)
- Triangles: ~1000-4000
- Holes: Yes (visible gaps in mesh)
- Generation time: ~5-20ms

**After Fix:**
- Vertices: ~800-3000 (may be slightly higher)
- Triangles: ~2000-8000 (roughly 2-3x due to complete faces)
- Holes: No (watertight mesh)
- Generation time: ~10-40ms (roughly 3x due to 3x face loops)

Note: Exact numbers vary greatly based on terrain complexity and chunk size.

## Common Issues and Solutions

### Issue: Mesh still has holes
**Possible Causes:**
- Cell vertices not being generated properly
- Face generation logic error in specific orientation
- Numerical precision issues

**Solution:**
- Check that `AddFaceOrFill` is being called for all three planes
- Verify corner cell coordinates are correct
- Review the hole-filling pass logs

### Issue: Performance degradation
**Possible Causes:**
- Too many redundant faces being generated
- Inefficient vertex caching

**Solution:**
- The performance increase is expected (3x face loops)
- If unacceptable, consider optimizing with face culling
- Profile to identify specific bottlenecks

### Issue: Incorrect face normals
**Possible Causes:**
- Winding order inconsistency
- Normal calculation issues

**Solution:**
- Verify face winding in `AddFaceOrFill` produces counter-clockwise triangles
- Check normal accumulation and normalization logic

## Reporting Results

When reporting test results, please include:
1. Screenshots showing mesh before/after fix
2. Performance metrics (frame rate, generation time)
3. Any visual artifacts or issues observed
4. Comparison with Marching Cubes method if available

## Additional Validation

For thorough validation:
- Test with different chunk sizes
- Test with different voxel resolutions
- Test edge cases (empty chunks, fully solid chunks)
- Test chunk boundaries (verify seams are handled correctly)
