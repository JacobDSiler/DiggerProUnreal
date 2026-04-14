// DynamicHole.cpp

#include "DynamicHole.h"
#include "DiggerManager.h"
#include "DiggerHistory.h"
#include "HoleShapeLibrary.h"
#include "VoxelChunk.h"
#include "Components/StaticMeshComponent.h"
#include "UObject/ConstructorHelpers.h"
#include "VT/RuntimeVirtualTexture.h"

ADynamicHole::ADynamicHole()
{
	PrimaryActorTick.bCanEverTick = false;

	// ---------------------------------------------------------
	// 1. COMPONENT SETUP
	// ---------------------------------------------------------
	HoleMeshComponent = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("HoleMesh"));
	RootComponent = HoleMeshComponent;

	// Default hole shape (metadata only — mesh assigned later)
	//HoleShape = FHoleShape(EHoleShapeType::Sphere);

	// ---------------------------------------------------------
	// 2. COLLISION: NON‑BLOCKING BUT STILL IDENTIFIABLE
	// ---------------------------------------------------------
	// We want hole actors to NEVER block visibility traces,
	// but still be detectable via IsHoleBPActor() and tags.
	HoleMeshComponent->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	HoleMeshComponent->SetGenerateOverlapEvents(false);
	HoleMeshComponent->SetCollisionResponseToAllChannels(ECR_Ignore);

	// Tag for SmartTrace identification
	Tags.Add(FName("DiggerHole"));

	// ---------------------------------------------------------
	// 3. LOAD DEFAULT MATERIAL (WriterMaterial)
	// ---------------------------------------------------------
	static ConstructorHelpers::FObjectFinder<UMaterialInterface> MatFinder(
		TEXT("/Digger/Digger/Materials/LandscapeMaterials/M_OpacityMask.M_OpacityMask")
	);

	if (MatFinder.Succeeded())
	{
		WriterMaterial = MatFinder.Object;
	}
	else
	{
		UE_LOG(LogTemp, Error,
			TEXT("ADynamicHole: Could not find M_OpacityMask! Check path."));
	}

	// ---------------------------------------------------------
	// 4. DEFAULTS FOR INTERNAL STATE
	// ---------------------------------------------------------
	OwningChunk = nullptr;
	HoleID = -1;
}

FVector ADynamicHole::ActorToLocal(const FVector& WorldPos) const
{
	return GetActorTransform().InverseTransformPosition(WorldPos);
}

bool ADynamicHole::ContainsPoint(const FVector& WorldPos) const
{
	bool bResult;
	if (BrushShapeInstance)
	{
		// Use the same stroke + shape logic that defines the actual hole volume
		bResult = BrushShapeInstance->IsWithinInterior(WorldPos, CachedStroke);
		if (DiggerDebug::Holes())
		{
		// UE_LOG(LogTemp, Warning,
		// 	TEXT("ContainsPoint: Hole=%s, Point=%s, Result=%d"),
		// 	*GetName(), *WorldPos.ToString(), bResult ? 1 : 0);
		}
		
		return bResult;
	}

	// Fallback: old simple sphere, in case BrushShapeInstance is missing
	const FVector Local = ActorToLocal(WorldPos);
	const float BaseRadius = 100.0f;
	const float Radius = BaseRadius * GetActorScale3D().GetMax();



	return Local.SizeSquared() < FMath::Square(Radius);
}


void ADynamicHole::SetHoleMesh(UStaticMesh* NewMesh)
{
	if (HoleMeshComponent && NewMesh)
	{
		// 1. Set the Shape
		HoleMeshComponent->SetStaticMesh(NewMesh);

		// 2. Force the Material
		// This overrides whatever material was set on the Static Mesh asset (e.g., WorldGridMaterial)
		if (WriterMaterial)
		{
			// Set it to Slot 0 (covers 99% of simple shapes)
			// HoleMeshComponent->SetMaterial(0, WriterMaterial);
            
			// If you have complex meshes with multiple slots, you might want to loop:
			for(int32 i=0; i < NewMesh->GetStaticMaterials().Num(); i++)
				HoleMeshComponent->SetMaterial(i, WriterMaterial);
		}
	}
}

void ADynamicHole::ConfigureRVTRendering(URuntimeVirtualTexture* DiggerRVT)
{
	if (!HoleMeshComponent || !DiggerRVT) return;
	// Clear any existing RVTs (e.g. proc mesh RVT from default construction)
	// and set ONLY the Digger opacity RVT so the hole writes to the correct target.
	HoleMeshComponent->RuntimeVirtualTextures.Empty();
	HoleMeshComponent->RuntimeVirtualTextures.Add(DiggerRVT);
	HoleMeshComponent->VirtualTextureRenderPassType = ERuntimeVirtualTextureMainPassType::Never;
	HoleMeshComponent->MarkRenderStateDirty();
}

void ADynamicHole::BeginPlay()
{
	Super::BeginPlay();

	// ---------------------------------------------------------
	// Hole Validation
	// ---------------------------------------------------------
	ValidateSpawnAgainstLandscape();

	// --- FIX: DO NOT SET MESH ON START ---
	// Previously: UpdateHoleMesh();
    
	// Instead, ensure we start invisible. 
	// The VoxelChunk will call UpdateHoleMesh() when the terrain geometry is actually ready.
	if (HoleMeshComponent)
	{
		HoleMeshComponent->SetStaticMesh(nullptr);
	}
}


void ADynamicHole::OnConstruction(const FTransform& Xform)
{
    Super::OnConstruction(Xform);

    // 📌 Assign World Outliner folder (Editor only) - RESPECTING SETTINGS
#if WITH_EDITOR
    if (GIsEditor)
    {
        // 1. Read the user preference directly from Config (Decoupled from Editor Module)
        bool bShowHoles = false; // Default to hidden
        if (GConfig)
        {
            GConfig->GetBool(
                TEXT("/Script/DiggerEditor.DiggerEditorSettings"), // Section
                TEXT("bShowDynamicHolesFolder"),                   // Key
                bShowHoles,                                        // Output
                GEditorPerProjectIni                               // Filename
            );
        }

        // 2. Determine the Target Path
        // If Show: "Digger/DynamicHoles"
        // If Hide: NAME_None (Root) -> Prevents folder creation/reappearance
        const FName TargetFolderPath = bShowHoles ? FName(TEXT("Digger/DynamicHoles")) : NAME_None;

        // 3. Set the Folder Path (Preserving your UE5/UE4 logic)
#if ENGINE_MAJOR_VERSION >= 5
        if (GetClass()->FindFunctionByName(TEXT("SetFolderPath_Recursively")))
        {
            SetFolderPath_Recursively(TargetFolderPath);
        }
        else
        {
            SetFolderPath(TargetFolderPath);
        }
#else
        SetFolderPath(TargetFolderPath);
#endif
    }
#endif // WITH_EDITOR


	// Construction should NOT reassign chunks
	CurrentChunkCoords = FVoxelConversion::WorldToChunk(GetActorLocation());
	PreviousChunkCoords = CurrentChunkCoords;
}

#if WITH_EDITOR
void ADynamicHole::PostEditMove(bool bFinished)
{
	Super::PostEditMove(bFinished);

	if (!bFinished)
	{
		// Drag start — snapshot the transform BEFORE the move so we can record
		// the full delta when bFinished fires.
		PreMoveTransform = GetActorTransform();
		return;
	}

	// --- Drag end ---

	// 1. Determine new chunk
	FIntVector NewCoords = FVoxelConversion::WorldToChunk(GetActorLocation());

	if (NewCoords != CurrentChunkCoords)
	{
		// Transfer chunk ownership
		if (OwningChunk)
			OwningChunk->RemoveHoleFromChunk(this);

		UVoxelChunk* NewChunk = FindOwningChunk(NewCoords);
		if (NewChunk)
		{
			NewChunk->AddHoleToChunk(this);
			OwningChunk = NewChunk;
		}

		PreviousChunkCoords = CurrentChunkCoords;
		CurrentChunkCoords  = NewCoords;
	}

	// 2. Record the move for undo
	if (DiggerManager && HoleUID != INDEX_NONE)
	{
		FDiggerActorMoveRecord Rec;
		Rec.ActorType     = EDiggerActorType::Hole;
		Rec.ActorUID      = HoleUID;
		Rec.PreTransform  = PreMoveTransform;
		Rec.PostTransform = GetActorTransform();
		Rec.OldChunkCoords = PreviousChunkCoords;
		Rec.NewChunkCoords = CurrentChunkCoords;
		DiggerManager->RecordActorMove(MoveTemp(Rec));
	}
}
#endif


void ADynamicHole::SetOwningChunk(UVoxelChunk* NewChunk)
{
	if (OwningChunk == NewChunk)
		return;

	if (OwningChunk)
	{
		OwningChunk->RemoveHoleFromChunk(this);
	}

	OwningChunk = NewChunk;

	if (OwningChunk)
	{
		HoleID = OwningChunk->GenerateHoleID();
		OwningChunk->AddHoleToChunk(this);
	}
	if (DiggerDebug::Holes())
		UE_LOG(LogTemp, Warning,
			TEXT("DynamicHole %s now owned by chunk %s"),
			*GetName(),
			OwningChunk ? *OwningChunk->GetChunkCoords().ToString() : TEXT("NONE"));
}

void ADynamicHole::ValidateSpawnAgainstLandscape()
{
    UWorld* World = GetWorld();
    if (!World)
        return;

    const FVector Start = GetActorLocation();
    const FVector Down  = FVector(0,0,-1);

    FCollisionQueryParams Params(TEXT("HoleSpawnValidation"), false, this);

    // --- 1. Trace down to find the landscape ---
    FHitResult LandscapeHit;
    bool bHitLandscape = World->LineTraceSingleByChannel(
        LandscapeHit,
        Start,
        Start + Down * 5000.f,
        ECC_Visibility,
        Params
    );

    if (!bHitLandscape || !LandscapeHit.GetActor()->IsA(ALandscapeProxy::StaticClass()))
    {
        // No landscape under us → valid hole
        return;
    }

    // --- 2. Trace again just below the landscape ---
    const FVector SecondStart = LandscapeHit.ImpactPoint + Down * 5.f;
    const FVector SecondEnd   = SecondStart + Down * 150.f;

    FHitResult MeshHit;
    bool bHitMesh = World->LineTraceSingleByChannel(
        MeshHit,
        SecondStart,
        SecondEnd,
        ECC_WorldStatic,
        Params
    );

    if (!bHitMesh)
        return; // No mesh below → valid hole

    // --- 3. Check if the hit is the skirt mesh ---
    UPrimitiveComponent* HitComp = MeshHit.GetComponent();
    if (!HitComp)
        return;

    // Must be a procedural mesh
    if (!HitComp->IsA(UProceduralMeshComponent::StaticClass()))
        return;

    // Must be owned by *your* DiggerManager
    AActor* OwnerActor = HitComp->GetOwner();
    if (!OwnerActor || !OwnerActor->IsA(ADiggerManager::StaticClass()))
        return;

    // --- 4. Check if the hit is shallow (skirt thickness) ---
    const float Depth = (MeshHit.ImpactPoint - LandscapeHit.ImpactPoint).Size();
    constexpr float MaxSkirtDepth = 15.f; // tune to your voxel size

    if (Depth < MaxSkirtDepth)
    {
        UE_LOG(LogTemp, Warning,
            TEXT("DynamicHole %s destroyed: spawned on skirt mesh (depth=%.2f)."),
            *GetName(), Depth);

        Destroy();
        return;
    }

    // Otherwise: valid hole
}



UVoxelChunk* ADynamicHole::FindOwningChunk(const FIntVector& ChunkCoords) const
{
	// Find the chunk using the coordinates (you can use WorldToChunk or a similar method)
	if (!DiggerManager)
		return nullptr;
	
	return DiggerManager->GetOrCreateChunkAtCoords(ChunkCoords);
}




// Prepares shape metadata (BrushShapeInstance, CachedStroke) WITHOUT touching the mesh.
// Called by VoxelChunk::OnMeshReady before it assigns the static mesh.
void ADynamicHole::PrepareShapeData()
{
	if (!OwningChunk || !DiggerManager || !DiggerManager->HoleShapeLibrary)
		return;

	BrushShapeInstance = DiggerManager->HoleShapeLibrary->CreateBrushShape(HoleShape.ShapeType);

	const float BaseSize = 100.0f;
	const FVector Scale = GetActorScale3D();

	CachedStroke = FBrushStroke{};
	CachedStroke.BrushType                = GetBrushTypeForHole(HoleShape.ShapeType);
	CachedStroke.BrushPosition            = GetActorLocation();
	CachedStroke.BrushRotation            = GetActorRotation();
	CachedStroke.BrushRadius              = BaseSize * Scale.GetMax();
	CachedStroke.AdvancedCubeHalfExtentX  = BaseSize * Scale.X;
	CachedStroke.AdvancedCubeHalfExtentY  = BaseSize * Scale.Y;
	CachedStroke.AdvancedCubeHalfExtentZ  = BaseSize * Scale.Z;
	CachedStroke.TorusInnerRadius         = 0.5f * BaseSize * FMath::Min3(Scale.X, Scale.Y, Scale.Z);
	CachedStroke.BrushLength              = BaseSize * Scale.Z;
	CachedStroke.HoleShape                = HoleShape.ShapeType;

#if WITH_EDITORONLY_DATA
	if (DiggerManager)
	{
		if (URuntimeVirtualTexture* RVT = DiggerManager->ResolveDiggerRVTForPosition(GetActorLocation()))
			ConfigureRVTRendering(RVT);
	}
#endif
}

// Full update: prepares shape data AND assigns the mesh.
// Only call this when you are certain the voxel geometry is already committed
// (i.e. from VoxelChunk::OnMeshReady, never from AddHoleToChunk or spawn time).
void ADynamicHole::UpdateHoleMesh()
{
	if (!OwningChunk || !DiggerManager || !DiggerManager->HoleShapeLibrary)
		return;

	// Metadata only — no mesh assignment here.
	// The mesh is assigned by VoxelChunk::OnMeshReady.
	PrepareShapeData();
}



void ADynamicHole::SetMeshForShape(EHoleShapeType ShapeType)
{
	if (DiggerManager)
	{
		UHoleShapeLibrary* HoleLibrary = DiggerManager->HoleShapeLibrary;
		if (HoleLibrary)
		{
			UStaticMesh* NewMesh = HoleLibrary->GetMeshForShape(ShapeType);
			if (NewMesh)
			{
				HoleMeshComponent->SetStaticMesh(NewMesh);
			}
		}
	}
}




void ADynamicHole::SyncWithChunk()
{
	if (OwningChunk)
	{
		FVector HoleLocation = GetActorLocation();
		FRotator HoleRotation = GetActorRotation();
		FVector HoleScale = GetActorScale();
		OwningChunk->SaveHoleData(HoleLocation, HoleRotation, HoleScale);
	}
}

void ADynamicHole::SetDiggerManager(ADiggerManager* InDiggerManager)
{
	DiggerManager = InDiggerManager;
#if WITH_EDITORONLY_DATA
	if (DiggerManager)
	{
		if (URuntimeVirtualTexture* RVT = DiggerManager->ResolveDiggerRVTForPosition(GetActorLocation()))
			ConfigureRVTRendering(RVT);
	}
#endif
}