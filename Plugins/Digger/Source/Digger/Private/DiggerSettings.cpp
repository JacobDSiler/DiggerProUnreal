#include "DiggerSettings.h"

UDiggerSettings::UDiggerSettings()
{
	CategoryName = TEXT("Digger");
	SectionName  = TEXT("Runtime Settings");

	SkirtRadiusWorld = 300.0f;
	SkirtClipBias = 2.0f;
	VerticalSearchBand = 500.0f;

	// Hole spawning defaults
	ProximityToleranceMultiplier = 0.6f;
	FallbackTraceHeightMultiplier = 2.0f;
	ChunkRetryOffset = 5.0f;
	ScaleDivisor = 47.0f;
}

const UDiggerSettings* UDiggerSettings::Get()
{
	return GetDefault<UDiggerSettings>();
}