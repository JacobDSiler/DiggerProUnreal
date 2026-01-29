#include "SelectableBase.h"
#include "Engine/World.h"

// CRITICAL: Only include Editor headers inside this block
#if WITH_EDITOR
#include "DiggerEditorAccess.h"
#endif

ASelectableBase::ASelectableBase()
{
    PrimaryActorTick.bCanEverTick = false;
    bOriginalSelectability = bIsSelectableInEditor;
}

// Removed ~ASelectableBase()

void ASelectableBase::BeginDestroy()
{
    // This is the safe place to unbind before the object is garbage collected
    UnbindFromDiggerModeEvents();
    Super::BeginDestroy();
}

void ASelectableBase::BeginPlay()
{
    Super::BeginPlay();
    
#if WITH_EDITOR
    // In editor, bind to digger mode events
    if (GetWorld() && GetWorld()->WorldType == EWorldType::Editor)
    {
        BindToDiggerModeEvents();
        // Safe check: Ensure the class exists before calling static functions
        UpdateSelectabilityForDiggerMode(FDiggerEditorAccess::IsDiggerModeActive());
    }
#endif
}

void ASelectableBase::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
    UnbindFromDiggerModeEvents();
    Super::EndPlay(EndPlayReason);
}

#if WITH_EDITOR
void ASelectableBase::PostActorCreated()
{
    Super::PostActorCreated();
    bOriginalSelectability = bIsSelectableInEditor;
    BindToDiggerModeEvents();
    
        UpdateSelectabilityForDiggerMode(FDiggerEditorAccess::IsDiggerModeActive());
}

void ASelectableBase::PostLoad()
{
    Super::PostLoad();
    bOriginalSelectability = bIsSelectableInEditor;
    BindToDiggerModeEvents();
    
    UpdateSelectabilityForDiggerMode(FDiggerEditorAccess::IsDiggerModeActive());
}
#endif

bool ASelectableBase::CanEditChange(const FProperty* InProperty) const
{
    if (!bIsSelectableInEditor)
    {
        return false;
    }
    return Super::CanEditChange(InProperty);
}

void ASelectableBase::SetSelectableInEditor(bool bSelectable)
{
    bIsSelectableInEditor = bSelectable;
}

void ASelectableBase::OnDiggerModeStateChanged(bool bDiggerModeActive)
{
    UpdateSelectabilityForDiggerMode(bDiggerModeActive);
}

void ASelectableBase::BindToDiggerModeEvents()
{
#if WITH_EDITOR
    UnbindFromDiggerModeEvents();
    // Only bind if the Editor Access module is actually loaded
    FDiggerEditorAccess::OnEditorModeChanged.AddUObject(this, &ASelectableBase::OnDiggerModeStateChanged);
#endif
}

void ASelectableBase::UnbindFromDiggerModeEvents()
{
#if WITH_EDITOR
    // Safety check is tricky on shutdown, but try-catch isn't standard in UE. 
    // Usually safe to call RemoveAll if the module is still loaded.
    FDiggerEditorAccess::OnEditorModeChanged.RemoveAll(this);
#endif
}

void ASelectableBase::UpdateSelectabilityForDiggerMode(bool bDiggerModeActive)
{
#if WITH_EDITOR
    if (bHideInDiggerMode)
    {
        if (bDiggerModeActive)
        {
            bIsSelectableInEditor = false;
        }
        else
        {
            bIsSelectableInEditor = bOriginalSelectability;
        }
    }
#endif
}