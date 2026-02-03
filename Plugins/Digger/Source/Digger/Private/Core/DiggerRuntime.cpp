#include "DiggerRuntime.h" // Make sure this matches the filename in Public!

#include "VoxelConversion.h"

#define LOCTEXT_NAMESPACE "FDiggerRuntimeModule"

void FDiggerRuntimeModule::StartupModule()
{
    FWorldDelegates::OnPostWorldInitialization.AddStatic(&FDiggerRuntimeModule::OnWorldInit);
    FEditorDelegates::PostPIEStarted.AddStatic(&FDiggerRuntimeModule::OnPIEStart);
}


void FDiggerRuntimeModule::ShutdownModule()
{
    // This function may be called during shutdown to clean up your module.  For modules that support dynamic reloading,
    // we call this function before unloading the module.
}



void FDiggerRuntimeModule::OnWorldInit(UWorld* World, const UWorld::InitializationValues IVS)
{
    FVoxelConversion::RefreshDiggerManager(World);
}

void FDiggerRuntimeModule::OnPIEStart(bool bIsSimulating)
{
    if (GEditor)
    {
        UWorld* PIEWorld = GEditor->PlayWorld;
        FVoxelConversion::RefreshDiggerManager(PIEWorld);
    }
}


#undef LOCTEXT_NAMESPACE

IMPLEMENT_MODULE(FDiggerRuntimeModule, Digger);