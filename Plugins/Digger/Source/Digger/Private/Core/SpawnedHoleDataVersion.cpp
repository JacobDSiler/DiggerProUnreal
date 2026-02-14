#include "SpawnedHoleDataVersion.h"

// Define the GUID exactly once
const FGuid FSpawnedHoleDataCustomVersion::GUID(
	0xA3B4C5D6, 0x12345678, 0x9ABCDEF0, 0x13572468
);

// Register the version with Unreal
static FCustomVersionRegistration GRegisterSpawnedHoleDataCustomVersion(
	FSpawnedHoleDataCustomVersion::GUID,
	FSpawnedHoleDataCustomVersion::LatestVersion,
	TEXT("SpawnedHoleDataVer")
);
