#pragma once

// DiggerDebug.h
// Debug flags for Digger extension logging control

namespace DiggerDebug
{
	// Set to true to enable logs for each area
	constexpr bool Verbose = false;      // For generally logging everything
	constexpr bool Performance = false;  // For performance-related logs
	constexpr bool Cache = false;        // For height cache logs
	constexpr bool Normals = false;      // For normal calculation logs
	constexpr bool Error = false;        // For error and exception logs
	constexpr bool Holes = false;        // For hole detection and processing logs
	constexpr bool Landscape = false;    // For landscape operation related logs
	constexpr bool VoxelConv = false;    // For Voxel Space Conversion logs
	constexpr bool Mesh = false;         // For mesh generation and section creation logs
	constexpr bool Islands = false;      // For island detection and analysis logs
	constexpr bool Brush = false;         // For brush operations and tool usage logs
	constexpr bool Context = false;      // For context and state management logs
	constexpr bool UserConv = false;     // For user input conversion logs
	constexpr bool IO = false;           // For file input/output operations logs
	constexpr bool Threads = false;      // For threading and parallel processing logs
	constexpr bool Space = false;        // For spatial calculations and transforms logs
	constexpr bool Chunks = false;       // For chunk loading and management logs
	constexpr bool Voxels = false;       // For individual voxel operations logs
	constexpr bool Casts = false;        // For raycasting and collision logs
	constexpr bool Manager = false;      // For manager class coordination logs
}
