# Dual Contouring Mesh Generation Fix

## Problem
The `GenerateDCMeshFromGrid` method was producing a degenerate and hole-filled mesh because it only generated faces for one plane orientation (XY plane with constant Z).

## Root Cause
In Dual Contouring, a watertight mesh requires generating quad faces between adjacent cells in **all three axis-aligned orientations**:
1. XY plane faces (constant Z) - horizontal slices
2. XZ plane faces (constant Y) - vertical slices parallel to XZ plane  
3. YZ plane faces (constant X) - vertical slices parallel to YZ plane

The original implementation only included #1, leaving holes in the mesh where faces in the X and Y orientations were needed.

## Solution
Modified the face generation loop in `GenerateDCMeshFromGrid` to generate faces for all three plane orientations:

### XY Plane Faces (constant Z)
```cpp
for (int32 x = MinV.X; x < MaxV.X; ++x)
    for (int32 y = MinV.Y; y < MaxV.Y; ++y)
        for (int32 z = MinV.Z; z <= MaxV.Z; ++z)
        {
            FIntVector c00(x, y, z);
            FIntVector c10(x + 1, y, z);
            FIntVector c11(x + 1, y + 1, z);
            FIntVector c01(x, y + 1, z);
            AddFaceOrFill(c00, c10, c11, c01);
        }
```

### XZ Plane Faces (constant Y)
```cpp
for (int32 x = MinV.X; x < MaxV.X; ++x)
    for (int32 z = MinV.Z; z < MaxV.Z; ++z)
        for (int32 y = MinV.Y; y <= MaxV.Y; ++y)
        {
            FIntVector c00(x, y, z);
            FIntVector c10(x + 1, y, z);
            FIntVector c11(x + 1, y, z + 1);
            FIntVector c01(x, y, z + 1);
            AddFaceOrFill(c00, c10, c11, c01);
        }
```

### YZ Plane Faces (constant X)
```cpp
for (int32 y = MinV.Y; y < MaxV.Y; ++y)
    for (int32 z = MinV.Z; z < MaxV.Z; ++z)
        for (int32 x = MinV.X; x <= MaxV.X; ++x)
        {
            FIntVector c00(x, y, z);
            FIntVector c10(x, y + 1, z);
            FIntVector c11(x, y + 1, z + 1);
            FIntVector c01(x, y, z + 1);
            AddFaceOrFill(c00, c10, c11, c01);
        }
```

## Performance Considerations
- The fix adds two additional loops (XZ and YZ planes) to the face generation process
- Each loop iterates over O(N³) cells where N is the chunk dimension
- The total face generation complexity remains O(N³), just with a 3x constant factor
- The `AddFaceOrFill` function already handles redundant/degenerate faces efficiently
- The hole-filling pass at the end provides additional robustness

## Expected Results
After this fix:
- ✓ The mesh should be watertight with no holes
- ✓ No degenerate triangles should be generated
- ✓ The mesh should accurately represent the voxel data
- ✓ No extraneous mesh should appear above or below the landscape
- ✓ Performance overhead is minimal (3x face generation loop iterations)

## Testing
To verify the fix works correctly:
1. Load a level with the DiggerManager actor
2. Set the mesh generation method to DualContouring
3. Create or modify terrain with voxel operations
4. Verify the generated mesh is solid with no visible holes
5. Check the performance is acceptable (should be similar to before with slightly more triangles)
