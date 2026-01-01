#pragma once

#include "CoreMinimal.h"
#include "Modules/ModuleManager.h"

class FDiggerEditorModule : public IModuleInterface
{
public:
    // Add a member to store the delegate handle so we can remove it later
    FDelegateHandle PreBeginPIEHandle;

    // The callback function
    void OnPreBeginPIE(bool bIsSimulating);
    
    virtual void StartupModule() override;
    virtual void ShutdownModule() override;
};
