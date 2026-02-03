#pragma once

#include "CoreMinimal.h"
#include "Modules/ModuleManager.h"

class FDiggerRuntimeModule : public IModuleInterface
{
public:
    /** IModuleInterface implementation */
    virtual void StartupModule() override;
    virtual void ShutdownModule() override;
    static void OnWorldInit(UWorld* World, const UWorld::InitializationValues IVS);
    static void OnPIEStart(bool bIsSimulating);
};