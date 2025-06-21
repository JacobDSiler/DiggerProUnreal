#pragma once

// DiggerDebug.h
// Debug flags for Digger extension logging control

namespace DiggerDebug
{
	// Set to true to enable logs for each area
	constexpr bool Error = false;
	constexpr bool Holes = false;
	constexpr bool Islands = false;
	constexpr bool Brush = false;
	constexpr bool Context = false;
	constexpr bool UserConv = false;
	constexpr bool IO = false;
	constexpr bool Threads = false;
	constexpr bool Space = false;
	constexpr bool Chunks  = false;
	constexpr bool Voxels  = false;
	constexpr bool Casts   = false;
	constexpr bool Manager = false;
	// Add more flags as needed, e.g.:
	// constexpr bool Terrain = false;
	// constexpr bool Mesh    = false;
}