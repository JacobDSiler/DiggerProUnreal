// ProcgenArcana Header File.
#pragma once

#include "CoreMinimal.h"
#include "UObject/NoExportTypes.h"
#include "Components/SplineComponent.h"
#include "Engine/World.h"
#include "ProcgenArcanaCaveImporter.generated.h"


UENUM(BlueprintType)
enum class ECavePassageType : uint8
{
    MainTunnel      UMETA(DisplayName = "Main Tunnel"),
    SideBranch      UMETA(DisplayName = "Side Branch"),
    Chamber         UMETA(DisplayName = "Chamber"),
    Connector       UMETA(DisplayName = "Connector"),
    Entrance        UMETA(DisplayName = "Entrance"),
    Exit            UMETA(DisplayName = "Exit")
};

UENUM(BlueprintType)  
enum class ECaveJunctionType : uint8
{
    Split           UMETA(DisplayName = "Split"),
    Merge           UMETA(DisplayName = "Merge"),
    Intersection    UMETA(DisplayName = "Intersection"),
    Entrance        UMETA(DisplayName = "Entrance"),
    Exit            UMETA(DisplayName = "Exit"),
    DeadEnd         UMETA(DisplayName = "Dead End")
};

USTRUCT(BlueprintType)
struct FCaveJunction
{
    GENERATED_USTRUCT_BODY()

    UPROPERTY(EditAnywhere, BlueprintReadWrite)
    FVector Location = FVector::ZeroVector;
    
    UPROPERTY(EditAnywhere, BlueprintReadWrite)
    TArray<int32> ConnectedSplineIDs;
    
    UPROPERTY(EditAnywhere, BlueprintReadWrite)
    ECaveJunctionType Type = ECaveJunctionType::Intersection;
    
    UPROPERTY(EditAnywhere, BlueprintReadWrite)
    float Radius = 100.0f;

    FCaveJunction()
    {
        Location = FVector::ZeroVector;
        Type = ECaveJunctionType::Intersection;
        Radius = 100.0f;
    }
};

// Forward declarations for multi-spline processing
USTRUCT(BlueprintType)
struct FSVGPathNode
{
    GENERATED_USTRUCT_BODY()

    UPROPERTY(EditAnywhere, BlueprintReadWrite)
    FVector2D Position = FVector2D::ZeroVector;
    
    UPROPERTY(EditAnywhere, BlueprintReadWrite)
    TArray<int32> ConnectedPaths;
    
    UPROPERTY(EditAnywhere, BlueprintReadWrite)
    bool bIsJunction = false;
    
    UPROPERTY(EditAnywhere, BlueprintReadWrite)
    bool bIsEntrance = false;
    
    UPROPERTY(EditAnywhere, BlueprintReadWrite)
    bool bIsExit = false;
    
    UPROPERTY(EditAnywhere, BlueprintReadWrite)
    float DistanceToEntrance = 0.0f;

    FSVGPathNode()
    {
        Position = FVector2D::ZeroVector;
        bIsJunction = false;
        bIsEntrance = false;
        bIsExit = false;
        DistanceToEntrance = 0.0f;
    }
};


USTRUCT(BlueprintType)
struct FSVGPath
{
    GENERATED_USTRUCT_BODY()

    UPROPERTY(EditAnywhere, BlueprintReadWrite)
    TArray<FVector2D> Points;
    
    UPROPERTY(EditAnywhere, BlueprintReadWrite)
    int32 StartNodeID = -1;
    
    UPROPERTY(EditAnywhere, BlueprintReadWrite)
    int32 EndNodeID = -1;
    
    UPROPERTY(EditAnywhere, BlueprintReadWrite)
    float EstimatedWidth = 200.0f;
    
    UPROPERTY(EditAnywhere, BlueprintReadWrite)
    ECavePassageType Type = ECavePassageType::MainTunnel;
    
    UPROPERTY(EditAnywhere, BlueprintReadWrite)
    FString PathID;

    FSVGPath()
    {
        StartNodeID = -1;
        EndNodeID = -1;
        EstimatedWidth = 200.0f;
        Type = ECavePassageType::MainTunnel;
        PathID = TEXT("");
    }
};

USTRUCT(BlueprintType)
struct FCaveSplineData
{
    GENERATED_USTRUCT_BODY()

    UPROPERTY(EditAnywhere, BlueprintReadWrite)
    TArray<FVector> Points;
    
    UPROPERTY(EditAnywhere, BlueprintReadWrite)  
    float Width = 200.0f;
    
    UPROPERTY(EditAnywhere, BlueprintReadWrite)
    float Height = 300.0f;
    
    UPROPERTY(EditAnywhere, BlueprintReadWrite)
    ECavePassageType Type = ECavePassageType::MainTunnel;
    
    UPROPERTY(EditAnywhere, BlueprintReadWrite)
    int32 StartJunctionID = -1;
    
    UPROPERTY(EditAnywhere, BlueprintReadWrite)
    int32 EndJunctionID = -1;
    
    UPROPERTY(EditAnywhere, BlueprintReadWrite)
    FString Name = TEXT("Passage");

    FCaveSplineData()
    {
        Width = 200.0f;
        Height = 300.0f;
        Type = ECavePassageType::MainTunnel;
        StartJunctionID = -1;
        EndJunctionID = -1;
        Name = TEXT("Passage");
    }
};

USTRUCT(BlueprintType)
struct FMultiSplineCaveData
{
    GENERATED_USTRUCT_BODY()

    UPROPERTY(EditAnywhere, BlueprintReadWrite)
    TArray<FCaveSplineData> Splines;
    
    UPROPERTY(EditAnywhere, BlueprintReadWrite)
    TArray<FCaveJunction> Junctions;
    
    UPROPERTY(EditAnywhere, BlueprintReadWrite)
    TArray<FVector> EntrancePoints;
    
    UPROPERTY(EditAnywhere, BlueprintReadWrite)
    TArray<FVector> ExitPoints;
    
    UPROPERTY(EditAnywhere, BlueprintReadWrite)
    FVector BoundingBoxMin = FVector::ZeroVector;
    
    UPROPERTY(EditAnywhere, BlueprintReadWrite)
    FVector BoundingBoxMax = FVector::ZeroVector;
    
    UPROPERTY(EditAnywhere, BlueprintReadWrite)
    FString SourceSVGPath;

    FMultiSplineCaveData()
    {
        BoundingBoxMin = FVector::ZeroVector;
        BoundingBoxMax = FVector::ZeroVector;
    }
};









UENUM(BlueprintType)
enum class EHeightMode : uint8
{
    Flat        UMETA(DisplayName = "Flat Cave"),
    Descending  UMETA(DisplayName = "Descending Cave"),
    Rolling     UMETA(DisplayName = "Rolling Hills"),
    Mountainous UMETA(DisplayName = "Mountainous"),
    Custom      UMETA(DisplayName = "Custom")
};

USTRUCT(BlueprintType)
struct FProcgenArcanaImportSettings
{
    GENERATED_BODY()

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Import")
    FString SVGFilePath;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Import", meta = (ClampMin = "10.0", ClampMax = "1000.0"))
    float CaveScale = 100.0f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Import", meta = (ClampMin = "0.0", ClampMax = "1.0"))
    float SimplificationLevel = 0.5f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Import", meta = (ClampMin = "10", ClampMax = "1000"))
    int32 MaxSplinePoints = 200;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Height")
    EHeightMode HeightMode = EHeightMode::Descending;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Height", meta = (ClampMin = "0.0", ClampMax = "500.0"))
    float HeightVariation = 50.0f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Height", meta = (ClampMin = "0.0", ClampMax = "2.0"))
    float DescentRate = 0.3f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Height")
    bool bAutoDetectEntrance = true;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Height", meta = (EditCondition = "!bAutoDetectEntrance"))
    FVector2D ManualEntrancePoint = FVector2D::ZeroVector;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Import")
    bool bPreviewMode = false;
};

USTRUCT()
struct FSVGPathPoint
{
    GENERATED_BODY()

    FVector2D Position;
    float DistanceFromStart = 0.0f;
    bool bIsImportant = false; // For keeping key points during simplification

    FSVGPathPoint() : Position(FVector2D::ZeroVector) {}
    FSVGPathPoint(FVector2D InPosition) : Position(InPosition) {}
};

/**
 * Utility class for importing cave systems from ProcgenArcana's SVG map generator files
 */
UCLASS(BlueprintType)
class DIGGEREDITOR_API UProcgenArcanaCaveImporter : public UObject
{
    GENERATED_BODY()

public:
    UProcgenArcanaCaveImporter();

    /**
     * Main import function - creates a spline component from SVG file
     */
    UFUNCTION(BlueprintCallable, Category = "ProcgenArcana Import")
    class USplineComponent* ImportCaveFromSVG(const FProcgenArcanaImportSettings& Settings, UWorld* World, AActor* Owner = nullptr);

    /**
     * Preview function - returns path points without creating spline (for visualization)
     */
    UFUNCTION(BlueprintCallable, Category = "ProcgenArcana Import")
    TArray<FVector> PreviewCaveFromSVG(const FProcgenArcanaImportSettings& Settings);

    /**
     * Utility function to detect entrance from SVG markers
     */
    UFUNCTION(BlueprintCallable, Category = "ProcgenArcana Import")
    FVector2D DetectEntranceFromSVG(const FString& SVGContent);

public:
    /**
     * Multi-spline import function - creates multiple connected splines from SVG
     */
    UFUNCTION(BlueprintCallable, Category = "ProcgenArcana Import")
    FMultiSplineCaveData ImportMultiSplineCaveFromSVG(const FProcgenArcanaImportSettings& Settings);
    
    /**
     * Create actor with multiple spline components from cave data
     */
    UFUNCTION(BlueprintCallable, Category = "ProcgenArcana Import") 
    AActor* CreateMultiSplineCaveActor(const FMultiSplineCaveData& CaveData, UWorld* World, const FVector& PivotOffset = FVector::ZeroVector);

    // Add to your UProcgenArcanaCaveImporter class in the .h file:
private:
    // Safe multi-spline processing methods with limits
    FMultiSplineCaveData ParseSVGToMultiSplineDataSafe(const FString& SVGContent, const FProcgenArcanaImportSettings& Settings);
    TArray<FSVGPath> ExtractAllCavePathsSafe(const FString& SVGContent, int32 MaxPaths = 50);
    TArray<FSVGPathNode> BuildPathGraphSafe(const TArray<FSVGPath>& Paths, float JunctionTolerance, int32 MaxNodes = 100);
    TArray<FVector2D> ParseSVGPathDataSafe(const FString& PathData, int32 MaxPoints = 500);
    
    
    // Multi-spline processing methods
    FMultiSplineCaveData ParseSVGToMultiSplineData(const FString& SVGContent, const FProcgenArcanaImportSettings& Settings);
    TArray<FSVGPath> ExtractAllCavePaths(const FString& SVGContent);
    TArray<FSVGPathNode> BuildPathGraph(const TArray<FSVGPath>& Paths, float JunctionTolerance = 50.0f);
    TArray<FVector2D> ParseSVGPathData(const FString& PathData);
    void ClassifyPassageTypes(TArray<FSVGPath>& Paths, const TArray<FSVGPathNode>& Nodes);
    void EstimatePassageWidths(TArray<FSVGPath>& Paths, const FProcgenArcanaImportSettings& Settings);
    FCaveSplineData ConvertPathToSplineData(const FSVGPath& Path, const FProcgenArcanaImportSettings& Settings);
    USplineComponent* CreateSplineComponentFromData(const FCaveSplineData& SplineData, UObject* Owner);


protected:
    // Core processing functions
    FString LoadSVGFile(const FString& FilePath);
    TArray<FSVGPathPoint> ParseSVGPath(const FString& SVGContent);
    TArray<FSVGPathPoint> SimplifyPath(const TArray<FSVGPathPoint>& OriginalPath, float SimplificationLevel, int32 MaxPoints);
    TArray<FVector> GenerateCenterline(const TArray<FSVGPathPoint>& BoundaryPath);
    TArray<FVector> ApplyHeightProfile(const TArray<FVector>& CenterlinePoints, const FProcgenArcanaImportSettings& Settings, const FVector2D& EntrancePoint);

    // Height calculation functions
    float CalculateFlatHeight(int32 PointIndex, const TArray<FVector>& Points, const FProcgenArcanaImportSettings& Settings);
    float CalculateDescendingHeight(int32 PointIndex, const TArray<FVector>& Points, const FProcgenArcanaImportSettings& Settings, const FVector2D& EntrancePoint);
    float CalculateRollingHeight(int32 PointIndex, const TArray<FVector>& Points, const FProcgenArcanaImportSettings& Settings, const FVector2D& EntrancePoint);
    float CalculateMountainousHeight(int32 PointIndex, const TArray<FVector>& Points, const FProcgenArcanaImportSettings& Settings, const FVector2D& EntrancePoint);

    // Utility functions
    float CalculatePathDistance(const FVector2D& Start, const FVector2D& End);
    float CalculateTotalPathDistance(const TArray<FVector>& Points, int32 StartIndex, int32 EndIndex);
    FVector2D ConvertSVGToUE5Coordinates(const FVector2D& SVGPoint, float Scale);
    TArray<FSVGPathPoint> ExtractMainCavePath(const FString& SVGContent);
    FVector2D FindClosestPointOnPath(const TArray<FVector>& Path, const FVector2D& TargetPoint);

    // SVG parsing helpers
    FString ExtractPathData(const FString& SVGContent);
    TArray<FVector2D> ParseSVGPathCommands(const FString& PathData);
    bool IsDecorative(const FString& PathElement);

private:
    TArray<FVector2D> ConnectLineSegments(const TArray<TPair<FVector2D, FVector2D>>& LineSegments);
    TArray<FVector2D> CreateBoundaryPath(const TArray<FVector2D>& AllPoints, const FVector2D& MinBounds, const FVector2D& MaxBounds);
    bool IsImportantPoint(const FVector2D& Point, const TArray<TPair<FVector2D, FVector2D>>& LineSegments);
    TArray<FVector2D> FindEntrances(const TArray<FVector2D>& BoundaryPath, const TArray<TPair<FVector2D, FVector2D>>& LineSegments);

    // Douglas-Peucker algorithm for path simplification
    TArray<FSVGPathPoint> DouglasPeucker(const TArray<FSVGPathPoint>& Points, float Tolerance);
    void DouglasPeuckerRecursive(const TArray<FSVGPathPoint>& Points, int32 StartIndex, int32 EndIndex, float Tolerance, TArray<bool>& KeepPoints);
    float PerpendicularDistance(const FSVGPathPoint& Point, const FSVGPathPoint& LineStart, const FSVGPathPoint& LineEnd);

    // Member variables for caching
    UPROPERTY()
    TArray<FSVGPathPoint> CachedBoundaryPath;
    
    UPROPERTY()
    FVector2D CachedEntrancePoint;
    
    UPROPERTY()
    FString CachedSVGContent;
};

