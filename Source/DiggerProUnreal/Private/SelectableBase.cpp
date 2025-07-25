// SelectableBase.cpp
#include "SelectableBase.h"
#if WITH_EDITOR
#include "DiggerEdMode.h"
#endif

#include "Engine/World.h"

ASelectableBase::ASelectableBase()
{
    PrimaryActorTick.bCanEverTick = false;
    bOriginalSelectability = bIsSelectableInEditor;
}

ASelectableBase::~ASelectableBase()
{
    UnbindFromDiggerModeEvents();
}

void ASelectableBase::BeginPlay()
{
    Super::BeginPlay();
    
#if WITH_EDITOR
    // In editor, bind to digger mode events
    if (GetWorld() && GetWorld()->WorldType == EWorldType::Editor)
    {
        BindToDiggerModeEvents();
        
        // Set initial state based on current digger mode
        UpdateSelectabilityForDiggerMode(FDiggerEdMode::IsDiggerModeActive());
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
    
    // Store the original selectability state
    bOriginalSelectability = bIsSelectableInEditor;
    
    // Bind to digger mode events when actor is created
    BindToDiggerModeEvents();
    
    // Set initial state based on current digger mode
    UpdateSelectabilityForDiggerMode(FDiggerEdMode::IsDiggerModeActive());
}

void ASelectableBase::PostLoad()
{
    Super::PostLoad();
    
    // Store the original selectability state
    bOriginalSelectability = bIsSelectableInEditor;
    
    // Bind to digger mode events when actor is loaded
    BindToDiggerModeEvents();
    
    // Set initial state based on current digger mode
    UpdateSelectabilityForDiggerMode(FDiggerEdMode::IsDiggerModeActive());
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
    // Unbind first to avoid double binding
    UnbindFromDiggerModeEvents();
    
    // Bind to the static delegate
    FDiggerEdMode::OnDiggerModeChanged.AddUObject(this, &ASelectableBase::OnDiggerModeStateChanged);
#endif
}

void ASelectableBase::UnbindFromDiggerModeEvents()
{
#if WITH_EDITOR
    // Unbind from the static delegate
    FDiggerEdMode::OnDiggerModeChanged.RemoveAll(this);
#endif
}

void ASelectableBase::UpdateSelectabilityForDiggerMode(bool bDiggerModeActive)
{
#if WITH_EDITOR
    if (bHideInDiggerMode)
    {
        if (bDiggerModeActive)
        {
            // Digger mode is active - make unselectable
            bIsSelectableInEditor = false;
        }
        else
        {
            // Digger mode is inactive - restore original selectability
            bIsSelectableInEditor = bOriginalSelectability;
        }
    }
#endif
}