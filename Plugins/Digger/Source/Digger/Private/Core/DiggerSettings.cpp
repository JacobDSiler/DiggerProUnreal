#include "DiggerSettings.h"
#include "HoleShapeLibrary.h" // <--- Important for the TSoftObjectPtr type

UDiggerSettings::UDiggerSettings()
{
	CategoryName = TEXT("Digger");
	SectionName = TEXT("Runtime Settings");

	SkirtRadiusWorld = 300.0f;
	SkirtClipBias = 2.0f;
	VerticalSearchBand = 500.0f;

	// Hole spawning defaults
	ProximityToleranceMultiplier = 0.6f;
	FallbackTraceHeightMultiplier = 2.0f;
	ChunkRetryOffset = 5.0f;
	ScaleDivisor = 47.0f;

	// Initialize with your current paths
	DefaultHoleLibrary = TSoftObjectPtr<UHoleShapeLibrary>(FSoftObjectPath(TEXT("/Digger/Digger/BluePrints/HoleShapeLibrary.HoleShapeLibrary")));

	// Note: For Classes, use _C for the Blueprint generated class
	DefaultHoleActorClass = TSoftClassPtr<AActor>(FSoftObjectPath(TEXT("/Digger/Digger/DynamicHoles/BP_MeshHole.BP_MeshHole_C")));
}

const UDiggerSettings *UDiggerSettings::Get()
{
	return GetDefault<UDiggerSettings>();
}