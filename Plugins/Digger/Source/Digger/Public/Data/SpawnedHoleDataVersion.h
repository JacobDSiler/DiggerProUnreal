// SpawnedHoleDataVersion.h

#pragma once

#include "CoreMinimal.h"
#include "Serialization/CustomVersion.h"

struct FSpawnedHoleDataCustomVersion
{
	// Bump this enum when you change the serialized layout of FSpawnedHoleData.
	//
	// HISTORY
	// -------
	//  V0 (BeforeShapeField / BeforeMagicHeader)
	//      Absolute legacy — no magic header, no FileVersion int written to disk.
	//      Only Location, Rotation, Scale were stored.
	//      These files are detected in LoadChunkData by the absence of a magic
	//      header sentinel: the first int32 in the hole section IS HoleCount.
	//
	//  V1 (AddedShapeField)
	//      Magic header (-0x444947) + FileVersion int written before HoleCount.
	//      FileVersion == 0 when this was introduced (the magic header was the
	//      only structural change; shape data was added in the same commit).
	//      Stores: Location, Rotation, Scale, ShapeByte (uint8).
	//
	//  V2 (AddedHoleUID) — FileVersion == 1
	//      Adds int32 HoleUID per hole entry so undo/redo can identify actors
	//      by stable ID rather than array index.
	//      Stores: Location, Rotation, Scale, ShapeByte, HoleUID (int32).
	//      Old files (V0/V1) load with HoleUID = INDEX_NONE; a new UID is
	//      allocated at spawn time so the actor is still tracked correctly.

	enum Type : int32
	{
		// Before the shape field was added (absolute legacy — no magic header).
		BeforeShapeField  = 0,

		// Shape field added; magic header introduced.
		AddedShapeField   = 1,

		// Stable HoleUID field added for undo/redo identity tracking.
		AddedHoleUID      = 2,

		// Always keep this as the last entry.
		LatestVersion     = AddedHoleUID
	};

	// Unique GUID for this custom version (used by FArchive::CustomVer).
	static const FGuid GUID;

	// The FileVersion integer written after the magic sentinel in the
	// chunk binary.  Monotonically increasing; independent of the GUID path.
	// V1 files have FileVersion == 0.
	// V2 files have FileVersion == 1.
	static constexpr int32 FileVersion_AddedShapeField = 0;
	static constexpr int32 FileVersion_AddedHoleUID    = 1;
	static constexpr int32 FileVersion_Current         = FileVersion_AddedHoleUID;

private:
	FSpawnedHoleDataCustomVersion() = delete;
};