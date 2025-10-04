#include "DiggerDebug.h"

FDiggerDebug& FDiggerDebug::Get()
{
	static FDiggerDebug Instance;
	return Instance;
}

FDiggerDebug::FDiggerDebug()
{
	RegisterFlag(TEXT("Verbose"), Verbose);
	RegisterFlag(TEXT("Performance"), Performance);
	RegisterFlag(TEXT("Cache"), Cache);
	RegisterFlag(TEXT("Normals"), Normals);
	RegisterFlag(TEXT("Error"), Error);
	RegisterFlag(TEXT("Holes"), Holes);
	RegisterFlag(TEXT("Landscape"), Landscape);
	RegisterFlag(TEXT("VoxelConv"), VoxelConv);
	RegisterFlag(TEXT("Mesh"), Mesh);
	RegisterFlag(TEXT("Islands"), Islands);
	RegisterFlag(TEXT("Brush"), Brush);
	RegisterFlag(TEXT("Context"), Context);
	RegisterFlag(TEXT("UserConv"), UserConv);
	RegisterFlag(TEXT("IO"), IO);
	RegisterFlag(TEXT("Threads"), Threads);
	RegisterFlag(TEXT("Space"), Space);
	RegisterFlag(TEXT("Chunks"), Chunks);
	RegisterFlag(TEXT("Voxels"), Voxels);
	RegisterFlag(TEXT("Casts"), Casts);
	RegisterFlag(TEXT("Delegates"), Delegates);
	RegisterFlag(TEXT("Manager"), Manager);
	RegisterFlag(TEXT("Caves"), Caves);
	RegisterFlag(TEXT("Lights"), Lights);
	RegisterFlag(TEXT("Flags"), Flags);
	RegisterFlag(TEXT("VoxelModificationReports"), VoxelModificationReports);
	RegisterFlag(TEXT("Seams"), Seams);
}

void FDiggerDebug::RegisterFlag(const TCHAR* FlagName, bool& FlagRef)
{
	const FName Name(FlagName);
	FlagRegistry.Add(Name, &FlagRef);
	FlagList.Emplace(Name, &FlagRef);
}

bool* FDiggerDebug::FindFlag(const FName& FlagName)
{
	if (bool** Result = FlagRegistry.Find(FlagName))
	{
		return *Result;
	}
	return nullptr;
}
