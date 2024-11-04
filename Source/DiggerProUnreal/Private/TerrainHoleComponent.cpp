// TerrainHoleComponent.cpp
#include "TerrainHoleComponent.h"
#include "LandscapeInfo.h"
#include "LandscapeLayerInfoObject.h"
#include "LandscapeDataAccess.h"
#include "LandscapeEdit.h"

UTerrainHoleComponent::UTerrainHoleComponent()
{
    PrimaryComponentTick.bCanEverTick = false;
}

void UTerrainHoleComponent::CreateHoleFromMesh(ALandscape* TargetLandscape, UStaticMeshComponent* MeshComponent)
{
    if (!TargetLandscape || !MeshComponent)
    {
        UE_LOG(LogTemp, Warning, TEXT("Invalid TargetLandscape or MeshComponent."));
        return;
    }

    ULandscapeInfo* LandscapeInfo = TargetLandscape->GetLandscapeInfo();
    if (!LandscapeInfo)
    {
        UE_LOG(LogTemp, Warning, TEXT("LandscapeInfo is null."));
        return;
    }

    FLandscapeEditDataInterface LandscapeEdit(LandscapeInfo);
    FTransform LandscapeTransform = TargetLandscape->GetActorTransform();

    TArray<FVector> ProjectedPoints;
    ProjectMeshOntoLandscape(TargetLandscape, MeshComponent, ProjectedPoints);

    int32 MinX = MAX_int32, MinY = MAX_int32;
    int32 MaxX = MIN_int32, MaxY = MIN_int32;

    for (const FVector& Point : ProjectedPoints)
    {
        FVector LocalPoint = LandscapeTransform.InverseTransformPosition(Point);
        int32 LandX, LandY;
        //LandscapeEdit.WorldToComponentPosition(LocalPoint, LandX, LandY);
        int32 LandscapeComponentSizeQuads = TargetLandscape->ComponentSizeQuads;
        LandX = FMath::RoundToInt(LocalPoint.X / LandscapeComponentSizeQuads);
        LandY = FMath::RoundToInt(LocalPoint.Y / LandscapeComponentSizeQuads);

        
        MinX = FMath::Min(MinX, LandX);
        MinY = FMath::Min(MinY, LandY);
        MaxX = FMath::Max(MaxX, LandX);
        MaxY = FMath::Max(MaxY, LandY);
    }

    // Ensure you have the layer info object
    ULandscapeLayerInfoObject* LayerInfo = nullptr; // Obtain this based on your project setup

    if (!LayerInfo)
    {
        UE_LOG(LogTemp, Error, TEXT("LayerInfo object is null. Ensure it is set."));
        return;
    }

    const int32 Width = MaxX - MinX + 1;
    const int32 Height = MaxY - MinY + 1;
    TArray<uint8> AlphaData;
    AlphaData.Init(0, Width * Height); // Initialize with 0 to make holes

    LandscapeEdit.SetAlphaData(
        LayerInfo,
        MinX, MinY, MaxX, MaxY,
        AlphaData.GetData(),
        Width,
        ELandscapeLayerPaintingRestriction::None,
        true, // Update bounds
        true  // Update collision
    );

    LandscapeEdit.Flush();
}



void UTerrainHoleComponent::ProjectMeshOntoLandscape(ALandscape* Landscape, UStaticMeshComponent* MeshComponent, TArray<FVector>& OutProjectedPoints)
{
    FBox MeshBounds = MeshComponent->Bounds.GetBox();
    
    // Get mesh vertices
    if (UStaticMesh* StaticMesh = MeshComponent->GetStaticMesh())
    {
        if (StaticMesh->GetRenderData())
        {
            FPositionVertexBuffer& VertexBuffer = StaticMesh->GetRenderData()->LODResources[0].VertexBuffers.PositionVertexBuffer;
            
            for (uint32 VertexIndex = 0; VertexIndex < VertexBuffer.GetNumVertices(); VertexIndex++)
            {
                FVector Vertex = FVector(VertexBuffer.VertexPosition(VertexIndex));
                FVector WorldVertex = MeshComponent->GetComponentTransform().TransformPosition(Vertex);
                
                // Project point onto landscape
                FVector Start = WorldVertex + FVector(0, 0, 1000);
                FVector End = WorldVertex - FVector(0, 0, 1000);
                
                FHitResult HitResult;
                FCollisionQueryParams QueryParams;
                QueryParams.AddIgnoredActor(MeshComponent->GetOwner());
                
                if (Landscape->GetWorld()->LineTraceSingleByChannel(HitResult, Start, End, ECC_Visibility, QueryParams))
                {
                    OutProjectedPoints.Add(HitResult.Location);
                }
            }
        }
    }
}

bool UTerrainHoleComponent::IsPointInsideMeshBounds(const FVector& Point, UStaticMeshComponent* MeshComponent)
{
    FBox MeshBounds = MeshComponent->Bounds.GetBox();
    return MeshBounds.IsInsideOrOn(Point);
}