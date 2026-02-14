// SpawnedHoleDataVersion.h

#pragma once

#include "CoreMinimal.h"
#include "Serialization/CustomVersion.h"

struct FSpawnedHoleDataCustomVersion
{
	// Bump this enum when you change the serialized layout of FSpawnedHoleData
	enum Type : int32
	{
		// Version 0: Legacy saves – only Location, Rotation, Scale were serialized.
		BeforeShapeField = 0,

		// Version 1: Shape field added to FSpawnedHoleData.
		AddedShapeField,

		// Always keep this as the last entry.
		LatestVersion = AddedShapeField
	};

	// Unique GUID for this custom version
	static const FGuid GUID;

private:
	FSpawnedHoleDataCustomVersion() = delete;
};