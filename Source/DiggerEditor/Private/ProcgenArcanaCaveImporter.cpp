// ProcgenArcanaCaveImporter.cpp
#include "ProcgenArcanaCaveImporter.h"
#include "Components/SplineComponent.h"
#include "Engine/World.h"
#include "Engine/Engine.h"
#include "HAL/PlatformFilemanager.h"
#include "Internationalization/Regex.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Math/UnrealMathUtility.h"



FMultiSplineCaveData UProcgenArcanaCaveImporter::ParseSVGToMultiSplineData(const FString& SVGContent, const FProcgenArcanaImportSettings& Settings)
{
    FMultiSplineCaveData CaveData;
    
    // Step 1: Extract all paths from SVG
    TArray<FSVGPath> RawPaths = ExtractAllCavePaths(SVGContent);
    if (RawPaths.Num() == 0)
    {
        UE_LOG(LogTemp, Warning, TEXT("[MultiSpline] No paths found in SVG, falling back to single path"));
        
        // Fallback to single path method
        TArray<FSVGPathPoint> SinglePath = ExtractMainCavePath(SVGContent);
        if (SinglePath.Num() > 2)
        {
            FSVGPath FallbackPath;
            for (const FSVGPathPoint& Point : SinglePath)
            {
                FallbackPath.Points.Add(FVector2D(Point.Position.X, Point.Position.Y));
            }
            FallbackPath.Type = ECavePassageType::MainTunnel;
            RawPaths.Add(FallbackPath);
        }
    }
    
    // Step 2: Build connectivity graph
    TArray<FSVGPathNode> PathNodes = BuildPathGraph(RawPaths, Settings.CaveScale * 0.1f);
    
    // Step 3: Classify passage types and estimate widths
    ClassifyPassageTypes(RawPaths, PathNodes);
    EstimatePassageWidths(RawPaths, Settings);
    
    // Step 4: Convert paths to spline data
    for (int32 i = 0; i < RawPaths.Num(); i++)
    {
        FCaveSplineData SplineData = ConvertPathToSplineData(RawPaths[i], Settings);
        SplineData.Name = FString::Printf(TEXT("%s_Spline_%d"), 
                                         *FPaths::GetBaseFilename(Settings.SVGFilePath), i);
        CaveData.Splines.Add(SplineData);
    }
    
    // Step 5: Create junctions from nodes
    for (int32 i = 0; i < PathNodes.Num(); i++)
    {
        const FSVGPathNode& Node = PathNodes[i];
        if (Node.bIsJunction || Node.bIsEntrance || Node.bIsExit)
        {
            FCaveJunction Junction;
            Junction.Location = FVector(Node.Position.X * Settings.CaveScale, 
                                      Node.Position.Y * Settings.CaveScale, 0.0f);
            Junction.ConnectedSplineIDs = Node.ConnectedPaths;
            Junction.Radius = Settings.CaveScale * 0.5f;
            
            if (Node.bIsEntrance)
            {
                Junction.Type = ECaveJunctionType::Entrance;
                CaveData.EntrancePoints.Add(Junction.Location);
            }
            else if (Node.bIsExit)
            {
                Junction.Type = ECaveJunctionType::Exit;
                CaveData.ExitPoints.Add(Junction.Location);
            }
            else if (Node.ConnectedPaths.Num() > 2)
            {
                Junction.Type = ECaveJunctionType::Intersection;
            }
            else if (Node.ConnectedPaths.Num() == 1)
            {
                Junction.Type = ECaveJunctionType::DeadEnd;
            }
            
            CaveData.Junctions.Add(Junction);
        }
    }
    
    // Step 6: Calculate bounding box
    if (CaveData.Splines.Num() > 0)
    {
        FVector Min(FLT_MAX), Max(-FLT_MAX);
        for (const FCaveSplineData& Spline : CaveData.Splines)
        {
            for (const FVector& Point : Spline.Points)
            {
                Min = FVector::Min(Min, Point);
                Max = FVector::Max(Max, Point);
            }
        }
        CaveData.BoundingBoxMin = Min;
        CaveData.BoundingBoxMax = Max;
    }
    
    return CaveData;
}

TArray<FSVGPath> UProcgenArcanaCaveImporter::ExtractAllCavePaths(const FString& SVGContent)
{
    TArray<FSVGPath> Paths;
    
    // Look for multiple <path> elements in the SVG
    FString SearchString = TEXT("<path");
    int32 SearchIndex = 0;
    
    while (true)
    {
        int32 PathStart = SVGContent.Find(SearchString, ESearchCase::IgnoreCase, ESearchDir::FromStart, SearchIndex);
        if (PathStart == INDEX_NONE)
            break;
            
        int32 PathEnd = SVGContent.Find(TEXT(">"), ESearchCase::IgnoreCase, ESearchDir::FromStart, PathStart);
        if (PathEnd == INDEX_NONE)
            break;
            
        FString PathElement = SVGContent.Mid(PathStart, PathEnd - PathStart + 1);
        
        // Extract the 'd' attribute (path data)
        FString PathData;
        int32 DStart = PathElement.Find(TEXT("d=\""), ESearchCase::IgnoreCase);
        if (DStart != INDEX_NONE)
        {
            DStart += 3; // Skip 'd="'
            int32 DEnd = PathElement.Find(TEXT("\""), ESearchCase::IgnoreCase, ESearchDir::FromStart, DStart);
            if (DEnd != INDEX_NONE)
            {
                PathData = PathElement.Mid(DStart, DEnd - DStart);
                
                // Parse this path data into points
                FSVGPath NewPath;
                NewPath.Points = ParseSVGPathData(PathData);
                
                if (NewPath.Points.Num() > 2) // Only add paths with enough points
                {
                    Paths.Add(NewPath);
                }
            }
        }
        
        SearchIndex = PathEnd + 1;
    }
    
    UE_LOG(LogTemp, Log, TEXT("[MultiSpline] Extracted %d paths from SVG"), Paths.Num());
    return Paths;
}

TArray<FSVGPathNode> UProcgenArcanaCaveImporter::BuildPathGraph(const TArray<FSVGPath>& Paths, float JunctionTolerance)
{
    TArray<FSVGPathNode> Nodes;
    
    // Collect all endpoints and potential junction points
    TArray<FVector2D> AllPoints;
    TArray<TPair<int32, int32>> PointToPath; // PathIndex, PointIndex
    
    for (int32 PathIdx = 0; PathIdx < Paths.Num(); PathIdx++)
    {
        const FSVGPath& Path = Paths[PathIdx];
        
        // Add start and end points
        if (Path.Points.Num() > 0)
        {
            AllPoints.Add(Path.Points[0]);
            PointToPath.Add(TPair<int32, int32>(PathIdx, 0));
            
            if (Path.Points.Num() > 1)
            {
                AllPoints.Add(Path.Points.Last());
                PointToPath.Add(TPair<int32, int32>(PathIdx, Path.Points.Num() - 1));
            }
        }
    }
    
    // Find junction points (where multiple paths meet)
    for (int32 i = 0; i < AllPoints.Num(); i++)
    {
        FSVGPathNode Node;
        Node.Position = AllPoints[i];
        
        // Find all points within tolerance
        TArray<int32> NearbyPaths;
        for (int32 j = 0; j < AllPoints.Num(); j++)
        {
            if (i != j && FVector2D::Distance(AllPoints[i], AllPoints[j]) <= JunctionTolerance)
            {
                int32 PathIndex = PointToPath[j].Key;
                NearbyPaths.AddUnique(PathIndex);
            }
        }
        
        if (NearbyPaths.Num() > 1)
        {
            Node.bIsJunction = true;
            Node.ConnectedPaths = NearbyPaths;
        }
        
        // Simple entrance/exit detection (endpoints with only one connection)
        if (NearbyPaths.Num() == 1)
        {
            // This is a potential entrance or exit
            Node.bIsEntrance = true; // We'll refine this later
        }
        
        if (Node.bIsJunction || Node.bIsEntrance)
        {
            Nodes.Add(Node);
        }
    }
    
    return Nodes;
}

void UProcgenArcanaCaveImporter::ClassifyPassageTypes(TArray<FSVGPath>& Paths, const TArray<FSVGPathNode>& Nodes)
{
    // Simple classification based on path length and connectivity
    for (FSVGPath& Path : Paths)
    {
        float PathLength = 0.0f;
        for (int32 i = 1; i < Path.Points.Num(); i++)
        {
            PathLength += FVector2D::Distance(Path.Points[i-1], Path.Points[i]);
        }
        
        // Classify based on length (this is a simple heuristic)
        if (PathLength > 1000.0f)
        {
            Path.Type = ECavePassageType::MainTunnel;
        }
        else if (PathLength > 500.0f)
        {
            Path.Type = ECavePassageType::SideBranch;
        }
        else
        {
            Path.Type = ECavePassageType::Connector;
        }
    }
    
    // Mark the longest path as main tunnel
    if (Paths.Num() > 0)
    {
        int32 LongestPathIndex = 0;
        float LongestLength = 0.0f;
        
        for (int32 i = 0; i < Paths.Num(); i++)
        {
            float Length = 0.0f;
            for (int32 j = 1; j < Paths[i].Points.Num(); j++)
            {
                Length += FVector2D::Distance(Paths[i].Points[j-1], Paths[i].Points[j]);
            }
            
            if (Length > LongestLength)
            {
                LongestLength = Length;
                LongestPathIndex = i;
            }
        }
        
        Paths[LongestPathIndex].Type = ECavePassageType::MainTunnel;
    }
}

void UProcgenArcanaCaveImporter::EstimatePassageWidths(TArray<FSVGPath>& Paths, const FProcgenArcanaImportSettings& Settings)
{
    for (FSVGPath& Path : Paths)
    {
        switch (Path.Type)
        {
            case ECavePassageType::MainTunnel:
                Path.EstimatedWidth = Settings.CaveScale * 2.0f;
                break;
            case ECavePassageType::SideBranch:
                Path.EstimatedWidth = Settings.CaveScale * 1.5f;
                break;
            case ECavePassageType::Chamber:
                Path.EstimatedWidth = Settings.CaveScale * 3.0f;
                break;
            default:
                Path.EstimatedWidth = Settings.CaveScale * 1.0f;
                break;
        }
    }
}

FCaveSplineData UProcgenArcanaCaveImporter::ConvertPathToSplineData(const FSVGPath& Path, const FProcgenArcanaImportSettings& Settings)
{
    FCaveSplineData SplineData;
    SplineData.Type = Path.Type;
    SplineData.Width = Path.EstimatedWidth;
    SplineData.Height = Path.EstimatedWidth * 1.5f; // Height is 1.5x width
    
    // Convert 2D points to 3D with height profile
    for (const FVector2D& Point2D : Path.Points)
    {
        FVector Point3D(Point2D.X * Settings.CaveScale, Point2D.Y * Settings.CaveScale, 0.0f);
        SplineData.Points.Add(Point3D);
    }
    
    // Apply height profile (reuse existing logic)
    FVector2D EntrancePoint = Path.Points.Num() > 0 ? Path.Points[0] : FVector2D::ZeroVector;
    SplineData.Points = ApplyHeightProfile(SplineData.Points, Settings, EntrancePoint);
    
    return SplineData;
}

AActor* UProcgenArcanaCaveImporter::CreateMultiSplineCaveActor(const FMultiSplineCaveData& CaveData, UWorld* World, const FVector& PivotOffset)
{
    if (!World || CaveData.Splines.Num() == 0)
    {
        return nullptr;
    }
    
    // Create the main actor
    FActorSpawnParameters SpawnParams;
    SpawnParams.Name = MakeUniqueObjectName(World, AActor::StaticClass(), 
                                           *FString::Printf(TEXT("MultiSplineCave_%s"), 
                                           *FPaths::GetBaseFilename(CaveData.SourceSVGPath)));
    
    AActor* CaveActor = World->SpawnActor<AActor>(AActor::StaticClass(), SpawnParams);
    if (!CaveActor)
    {
        return nullptr;
    }
    
    // Create root component
    USceneComponent* RootComp = NewObject<USceneComponent>(CaveActor);
    CaveActor->SetRootComponent(RootComp);
    RootComp->RegisterComponentWithWorld(World);
    
    // Create spline components for each passage
    for (int32 i = 0; i < CaveData.Splines.Num(); i++)
    {
        const FCaveSplineData& SplineData = CaveData.Splines[i];
        
        USplineComponent* SplineComp = CreateSplineComponentFromData(SplineData, CaveActor);
        if (SplineComp)
        {
            SplineComp->AttachToComponent(RootComp, FAttachmentTransformRules::KeepWorldTransform);
            SplineComp->RegisterComponentWithWorld(World);
            
            // Set component name for easy identification
            SplineComp->Rename(*FString::Printf(TEXT("Spline_%s_%d"), 
                               *UEnum::GetValueAsString(SplineData.Type), i));
        }
    }
    
    // Apply pivot offset
    CaveActor->SetActorLocation(-PivotOffset);
    
    // Set up actor properties
    CaveActor->SetActorLabel(SpawnParams.Name.ToString());
    CaveActor->Tags.Add(TEXT("ProgenCave"));
    CaveActor->Tags.Add(TEXT("MultiSpline"));
    CaveActor->Tags.Add(TEXT("ProcgenArcanaImport"));
    CaveActor->SetFolderPath(TEXT("Procgen Caves"));
    
    UE_LOG(LogTemp, Log, TEXT("[MultiSpline] Created cave actor with %d splines"), CaveData.Splines.Num());
    
    return CaveActor;
}

USplineComponent* UProcgenArcanaCaveImporter::CreateSplineComponentFromData(const FCaveSplineData& SplineData, UObject* Owner)
{
    USplineComponent* SplineComp = NewObject<USplineComponent>(Owner);
    if (!SplineComp)
    {
        return nullptr;
    }
    
    // Setup spline points
    SplineComp->ClearSplinePoints();
    for (int32 i = 0; i < SplineData.Points.Num(); i++)
    {
        SplineComp->AddSplinePoint(SplineData.Points[i], ESplineCoordinateSpace::Local);
        SplineComp->SetSplinePointType(i, ESplinePointType::Curve);
    }
    
    // Update and configure
    SplineComp->UpdateSpline();
    SplineComp->SetMobility(EComponentMobility::Movable);
    SplineComp->bDrawDebug = true;
    SplineComp->SetVisibility(true);
    
    // Color-code splines by type
    FLinearColor SplineColor = FLinearColor::Green;
    switch (SplineData.Type)
    {
        case ECavePassageType::MainTunnel:   SplineColor = FLinearColor::Green; break;
        case ECavePassageType::SideBranch:   SplineColor = FLinearColor::Yellow; break;
        case ECavePassageType::Chamber:      SplineColor = FLinearColor::Blue; break;
        case ECavePassageType::Connector:    SplineColor = FLinearColor::Red; break;
        default: SplineColor = FLinearColor::White; break;
    }
    
    // Note: SplineComp->SetDefaultUpVector() or other color settings might be available
    // depending on your Unreal version
    
    return SplineComp;
}

// Helper method to parse SVG path data (you may already have this)
TArray<FVector2D> UProcgenArcanaCaveImporter::ParseSVGPathData(const FString& PathData)
{
    TArray<FVector2D> Points;
    
    // Simple SVG path parser - you can expand this based on your needs
    // This assumes the path data contains move-to (M) and line-to (L) commands
    
    TArray<FString> Commands;
    PathData.ParseIntoArray(Commands, TEXT(" "), true);
    
    FVector2D CurrentPoint(0, 0);
    
    for (int32 i = 0; i < Commands.Num(); i++)
    {
        FString Command = Commands[i].TrimStartAndEnd();
        
        if (Command.StartsWith(TEXT("M")) && i + 1 < Commands.Num())
        {
            // Move to command
            FString XStr = Command.Mid(1);
            FString YStr = Commands[++i];
            
            CurrentPoint.X = FCString::Atof(*XStr);
            CurrentPoint.Y = FCString::Atof(*YStr);
            Points.Add(CurrentPoint);
        }
        else if (Command.StartsWith(TEXT("L")) && i + 1 < Commands.Num())
        {
            // Line to command
            FString XStr = Command.Mid(1);
            FString YStr = Commands[++i];
            
            CurrentPoint.X = FCString::Atof(*XStr);
            CurrentPoint.Y = FCString::Atof(*YStr);
            Points.Add(CurrentPoint);
        }
        else if (Command.Contains(TEXT(",")) && !Command.StartsWith(TEXT("M")) && !Command.StartsWith(TEXT("L")))
        {
            // Coordinate pair
            TArray<FString> Coords;
            Command.ParseIntoArray(Coords, TEXT(","), true);
            
            if (Coords.Num() >= 2)
            {
                CurrentPoint.X = FCString::Atof(*Coords[0]);
                CurrentPoint.Y = FCString::Atof(*Coords[1]);
                Points.Add(CurrentPoint);
            }
        }
    }
    
    return Points;
}

// Add these safer versions to your UProcgenArcanaCaveImporter class:

FMultiSplineCaveData UProcgenArcanaCaveImporter::ParseSVGToMultiSplineDataSafe(const FString& SVGContent, const FProcgenArcanaImportSettings& Settings)
{
    FMultiSplineCaveData CaveData;
    
    UE_LOG(LogTemp, Warning, TEXT("[MULTISPLINE PARSE] Starting safe parsing..."));
    
    // Step 1: Extract all paths from SVG with limits
    TArray<FSVGPath> RawPaths;
    try
    {
        RawPaths = ExtractAllCavePathsSafe(SVGContent, 50); // Limit to 50 paths max
        UE_LOG(LogTemp, Warning, TEXT("[MULTISPLINE PARSE] Extracted %d raw paths"), RawPaths.Num());
    }
    catch (...)
    {
        UE_LOG(LogTemp, Error, TEXT("[MULTISPLINE PARSE] Exception in ExtractAllCavePathsSafe"));
        return CaveData;
    }
    
    if (RawPaths.Num() == 0)
    {
        UE_LOG(LogTemp, Warning, TEXT("[MULTISPLINE PARSE] No paths found, using fallback single path"));
        
        // Fallback to single path method
        TArray<FSVGPathPoint> SinglePath = ExtractMainCavePath(SVGContent);
        if (SinglePath.Num() > 2)
        {
            FSVGPath FallbackPath;
            for (const FSVGPathPoint& Point : SinglePath)
            {
                FallbackPath.Points.Add(Point.Position);
            }
            FallbackPath.Type = ECavePassageType::MainTunnel;
            RawPaths.Add(FallbackPath);
        }
    }
    
    // Limit the number of paths to prevent infinite loops
    if (RawPaths.Num() > 20)
    {
        UE_LOG(LogTemp, Warning, TEXT("[MULTISPLINE PARSE] Too many paths (%d), limiting to 20"), RawPaths.Num());
        RawPaths.SetNum(20);
    }
    
    // Step 2: Build connectivity graph with timeout protection
    TArray<FSVGPathNode> PathNodes;
    try
    {
        PathNodes = BuildPathGraphSafe(RawPaths, Settings.CaveScale * 0.1f, 100); // Limit to 100 nodes max
        UE_LOG(LogTemp, Warning, TEXT("[MULTISPLINE PARSE] Built path graph with %d nodes"), PathNodes.Num());
    }
    catch (...)
    {
        UE_LOG(LogTemp, Error, TEXT("[MULTISPLINE PARSE] Exception in BuildPathGraphSafe"));
        // Continue without junctions
    }
    
    // Step 3: Classify and estimate (safe versions)
    try
    {
        ClassifyPassageTypes(RawPaths, PathNodes);
        EstimatePassageWidths(RawPaths, Settings);
        UE_LOG(LogTemp, Warning, TEXT("[MULTISPLINE PARSE] Classification and width estimation completed"));
    }
    catch (...)
    {
        UE_LOG(LogTemp, Error, TEXT("[MULTISPLINE PARSE] Exception in classification"));
    }
    
    // Step 4: Convert paths to spline data
    for (int32 i = 0; i < RawPaths.Num(); i++)
    {
        try
        {
            FCaveSplineData SplineData = ConvertPathToSplineData(RawPaths[i], Settings);
            SplineData.Name = FString::Printf(TEXT("%s_Spline_%d"), 
                                             *FPaths::GetBaseFilename(Settings.SVGFilePath), i);
            CaveData.Splines.Add(SplineData);
            
            UE_LOG(LogTemp, Warning, TEXT("[MULTISPLINE PARSE] Converted spline %d: %d points"), i, SplineData.Points.Num());
        }
        catch (...)
        {
            UE_LOG(LogTemp, Error, TEXT("[MULTISPLINE PARSE] Exception converting path %d"), i);
        }
    }
    
    // Step 5: Create junctions (limited)
    int32 MaxJunctions = 50;
    for (int32 i = 0; i < PathNodes.Num() && CaveData.Junctions.Num() < MaxJunctions; i++)
    {
        const FSVGPathNode& Node = PathNodes[i];
        if (Node.bIsJunction || Node.bIsEntrance || Node.bIsExit)
        {
            FCaveJunction Junction;
            Junction.Location = FVector(Node.Position.X * Settings.CaveScale, 
                                      Node.Position.Y * Settings.CaveScale, 0.0f);
            Junction.ConnectedSplineIDs = Node.ConnectedPaths;
            Junction.Radius = Settings.CaveScale * 0.5f;
            
            if (Node.bIsEntrance)
            {
                Junction.Type = ECaveJunctionType::Entrance;
                CaveData.EntrancePoints.Add(Junction.Location);
            }
            else if (Node.bIsExit)
            {
                Junction.Type = ECaveJunctionType::Exit;
                CaveData.ExitPoints.Add(Junction.Location);
            }
            else if (Node.ConnectedPaths.Num() > 2)
            {
                Junction.Type = ECaveJunctionType::Intersection;
            }
            else if (Node.ConnectedPaths.Num() == 1)
            {
                Junction.Type = ECaveJunctionType::DeadEnd;
            }
            
            CaveData.Junctions.Add(Junction);
        }
    }
    
    // Step 6: Calculate bounding box
    if (CaveData.Splines.Num() > 0)
    {
        FVector Min(FLT_MAX), Max(-FLT_MAX);
        for (const FCaveSplineData& Spline : CaveData.Splines)
        {
            for (const FVector& Point : Spline.Points)
            {
                Min = FVector::Min(Min, Point);
                Max = FVector::Max(Max, Point);
            }
        }
        CaveData.BoundingBoxMin = Min;
        CaveData.BoundingBoxMax = Max;
    }
    
    UE_LOG(LogTemp, Warning, TEXT("[MULTISPLINE PARSE] Safe parsing completed successfully"));
    
    return CaveData;
}

// Safe version of ExtractAllCavePaths with limits
TArray<FSVGPath> UProcgenArcanaCaveImporter::ExtractAllCavePathsSafe(const FString& SVGContent, int32 MaxPaths)
{
    TArray<FSVGPath> Paths;
    
    UE_LOG(LogTemp, Warning, TEXT("[EXTRACT PATHS] Starting safe path extraction, max paths: %d"), MaxPaths);
    
    // Look for multiple <path> elements in the SVG
    FString SearchString = TEXT("<path");
    int32 SearchIndex = 0;
    int32 PathCount = 0;
    
    while (PathCount < MaxPaths)
    {
        int32 PathStart = SVGContent.Find(SearchString, ESearchCase::IgnoreCase, ESearchDir::FromStart, SearchIndex);
        if (PathStart == INDEX_NONE)
            break;
            
        int32 PathEnd = SVGContent.Find(TEXT(">"), ESearchCase::IgnoreCase, ESearchDir::FromStart, PathStart);
        if (PathEnd == INDEX_NONE)
            break;
            
        FString PathElement = SVGContent.Mid(PathStart, PathEnd - PathStart + 1);
        
        // Safety check - prevent extremely long path elements
        if (PathElement.Len() > 10000)
        {
            UE_LOG(LogTemp, Warning, TEXT("[EXTRACT PATHS] Skipping oversized path element (%d chars)"), PathElement.Len());
            SearchIndex = PathEnd + 1;
            continue;
        }
        
        // Extract the 'd' attribute (path data)
        FString PathData;
        int32 DStart = PathElement.Find(TEXT("d=\""), ESearchCase::IgnoreCase);
        if (DStart != INDEX_NONE)
        {
            DStart += 3; // Skip 'd="'
            int32 DEnd = PathElement.Find(TEXT("\""), ESearchCase::IgnoreCase, ESearchDir::FromStart, DStart);
            if (DEnd != INDEX_NONE)
            {
                PathData = PathElement.Mid(DStart, DEnd - DStart);
                
                // Safety check for path data length
                if (PathData.Len() > 5000)
                {
                    UE_LOG(LogTemp, Warning, TEXT("[EXTRACT PATHS] Skipping oversized path data (%d chars)"), PathData.Len());
                    SearchIndex = PathEnd + 1;
                    continue;
                }
                
                // Parse this path data into points with safety limits
                FSVGPath NewPath;
                NewPath.Points = ParseSVGPathDataSafe(PathData, 500); // Limit to 500 points per path
                
                if (NewPath.Points.Num() > 2) // Only add paths with enough points
                {
                    Paths.Add(NewPath);
                    PathCount++;
                    UE_LOG(LogTemp, Warning, TEXT("[EXTRACT PATHS] Added path %d with %d points"), PathCount, NewPath.Points.Num());
                }
            }
        }
        
        SearchIndex = PathEnd + 1;
        
        // Safety break if search index is getting too large
        if (SearchIndex > SVGContent.Len() - 10)
            break;
    }
    
    UE_LOG(LogTemp, Log, TEXT("[EXTRACT PATHS] Safe extraction completed: %d paths found"), Paths.Num());
    return Paths;
}

// Safe version of BuildPathGraph with limits
TArray<FSVGPathNode> UProcgenArcanaCaveImporter::BuildPathGraphSafe(const TArray<FSVGPath>& Paths, float JunctionTolerance, int32 MaxNodes)
{
    TArray<FSVGPathNode> Nodes;
    
    if (Paths.Num() == 0)
    {
        return Nodes;
    }
    
    UE_LOG(LogTemp, Warning, TEXT("[BUILD GRAPH] Starting safe graph building with %d paths, max nodes: %d"), Paths.Num(), MaxNodes);
    
    // Collect all endpoints and potential junction points
    TArray<FVector2D> AllPoints;
    TArray<TPair<int32, int32>> PointToPath; // PathIndex, PointIndex
    
    for (int32 PathIdx = 0; PathIdx < Paths.Num(); PathIdx++)
    {
        const FSVGPath& Path = Paths[PathIdx];
        
        // Add start and end points
        if (Path.Points.Num() > 0)
        {
            AllPoints.Add(Path.Points[0]);
            PointToPath.Add(TPair<int32, int32>(PathIdx, 0));
            
            if (Path.Points.Num() > 1)
            {
                AllPoints.Add(Path.Points.Last());
                PointToPath.Add(TPair<int32, int32>(PathIdx, Path.Points.Num() - 1));
            }
        }
        
        // Safety limit - don't process too many points
        if (AllPoints.Num() > MaxNodes * 2)
        {
            UE_LOG(LogTemp, Warning, TEXT("[BUILD GRAPH] Reached point limit, breaking early"));
            break;
        }
    }
    
    UE_LOG(LogTemp, Warning, TEXT("[BUILD GRAPH] Collected %d points from paths"), AllPoints.Num());
    
    // Find junction points (where multiple paths meet) with O(n^2) limit
    int32 ProcessedNodes = 0;
    for (int32 i = 0; i < AllPoints.Num() && ProcessedNodes < MaxNodes; i++)
    {
        FSVGPathNode Node;
        Node.Position = AllPoints[i];
        
        // Find all points within tolerance
        TArray<int32> NearbyPaths;
        for (int32 j = 0; j < AllPoints.Num() && NearbyPaths.Num() < 10; j++) // Limit nearby paths
        {
            if (i != j && FVector2D::Distance(AllPoints[i], AllPoints[j]) <= JunctionTolerance)
            {
                int32 PathIndex = PointToPath[j].Key;
                NearbyPaths.AddUnique(PathIndex);
            }
        }
        
        if (NearbyPaths.Num() > 1)
        {
            Node.bIsJunction = true;
            Node.ConnectedPaths = NearbyPaths;
        }
        
        // Simple entrance/exit detection (endpoints with only one connection)
        if (NearbyPaths.Num() == 1)
        {
            // This is a potential entrance or exit
            Node.bIsEntrance = true; // We'll refine this later
        }
        
        if (Node.bIsJunction || Node.bIsEntrance)
        {
            Nodes.Add(Node);
            ProcessedNodes++;
        }
    }
    
    UE_LOG(LogTemp, Warning, TEXT("[BUILD GRAPH] Safe graph building completed: %d nodes created"), Nodes.Num());
    return Nodes;
}

// Safe version of ParseSVGPathData with limits
TArray<FVector2D> UProcgenArcanaCaveImporter::ParseSVGPathDataSafe(const FString& PathData, int32 MaxPoints)
{
    TArray<FVector2D> Points;
    
    if (PathData.IsEmpty())
    {
        return Points;
    }
    
    // Simple SVG path parser with safety limits
    TArray<FString> Commands;
    PathData.ParseIntoArray(Commands, TEXT(" "), true);
    
    // Limit the number of commands to prevent infinite loops
    if (Commands.Num() > MaxPoints * 3) // 3 commands per point should be plenty
    {
        UE_LOG(LogTemp, Warning, TEXT("[PARSE PATH] Too many commands (%d), limiting to %d"), Commands.Num(), MaxPoints * 3);
        Commands.SetNum(MaxPoints * 3);
    }
    
    FVector2D CurrentPoint(0, 0);
    int32 ProcessedPoints = 0;
    
    for (int32 i = 0; i < Commands.Num() && ProcessedPoints < MaxPoints; i++)
    {
        FString Command = Commands[i].TrimStartAndEnd();
        
        if (Command.StartsWith(TEXT("M")) && i + 1 < Commands.Num())
        {
            // Move to command
            FString XStr = Command.Mid(1);
            FString YStr = Commands[++i];
            
            CurrentPoint.X = FCString::Atof(*XStr);
            CurrentPoint.Y = FCString::Atof(*YStr);
            Points.Add(CurrentPoint);
            ProcessedPoints++;
        }
        else if (Command.StartsWith(TEXT("L")) && i + 1 < Commands.Num())
        {
            // Line to command
            FString XStr = Command.Mid(1);
            FString YStr = Commands[++i];
            
            CurrentPoint.X = FCString::Atof(*XStr);
            CurrentPoint.Y = FCString::Atof(*YStr);
            Points.Add(CurrentPoint);
            ProcessedPoints++;
        }
        else if (Command.Contains(TEXT(",")) && !Command.StartsWith(TEXT("M")) && !Command.StartsWith(TEXT("L")))
        {
            // Coordinate pair
            TArray<FString> Coords;
            Command.ParseIntoArray(Coords, TEXT(","), true);
            
            if (Coords.Num() >= 2)
            {
                CurrentPoint.X = FCString::Atof(*Coords[0]);
                CurrentPoint.Y = FCString::Atof(*Coords[1]);
                Points.Add(CurrentPoint);
                ProcessedPoints++;
            }
        }
    }
    
    return Points;
}


////
// Add these safer versions to your UProcgenArcanaCaveImporter class:

FMultiSplineCaveData UProcgenArcanaCaveImporter::ImportMultiSplineCaveFromSVG(const FProcgenArcanaImportSettings& Settings)
{
    UE_LOG(LogTemp, Warning, TEXT("[MULTISPLINE] Starting ImportMultiSplineCaveFromSVG..."));
    
    FMultiSplineCaveData CaveData;
    
    // Load SVG content with error checking
    FString SVGContent = LoadSVGFile(Settings.SVGFilePath);
    if (SVGContent.IsEmpty())
    {
        UE_LOG(LogTemp, Error, TEXT("[MULTISPLINE] Failed to load SVG file: %s"), *Settings.SVGFilePath);
        return CaveData;
    }
    
    UE_LOG(LogTemp, Warning, TEXT("[MULTISPLINE] SVG loaded, content length: %d"), SVGContent.Len());
    
    CaveData.SourceSVGPath = Settings.SVGFilePath;
    
    // Add timeout protection and try-catch
    try
    {
        // Parse SVG to multi-spline data with limits
        CaveData = ParseSVGToMultiSplineDataSafe(SVGContent, Settings);
        
        UE_LOG(LogTemp, Warning, TEXT("[MULTISPLINE] Parsing completed: %d splines, %d junctions"), 
               CaveData.Splines.Num(), CaveData.Junctions.Num());
    }
    catch (...)
    {
        UE_LOG(LogTemp, Error, TEXT("[MULTISPLINE] Exception during parsing! Falling back to single spline."));
        
        // Create fallback single spline from existing method
        TArray<FSVGPathPoint> SinglePath = ExtractMainCavePath(SVGContent);
        if (SinglePath.Num() > 2)
        {
            FCaveSplineData FallbackSpline;
            FallbackSpline.Type = ECavePassageType::MainTunnel;
            FallbackSpline.Width = Settings.CaveScale * 2.0f;
            FallbackSpline.Height = FallbackSpline.Width * 1.5f;
            FallbackSpline.Name = TEXT("MainTunnel_Fallback");
            
            for (const FSVGPathPoint& Point : SinglePath)
            {
                FallbackSpline.Points.Add(FVector(Point.Position.X * Settings.CaveScale, 
                                                 Point.Position.Y * Settings.CaveScale, 0.0f));
            }
            
            // Apply height profile
            FVector2D EntrancePoint = SinglePath.Num() > 0 ? SinglePath[0].Position : FVector2D::ZeroVector;
            FallbackSpline.Points = ApplyHeightProfile(FallbackSpline.Points, Settings, EntrancePoint);
            
            CaveData.Splines.Add(FallbackSpline);
            
            // Add basic entrance/exit
            if (FallbackSpline.Points.Num() > 0)
            {
                CaveData.EntrancePoints.Add(FallbackSpline.Points[0]);
                CaveData.ExitPoints.Add(FallbackSpline.Points.Last());
            }
        }
    }
    
    UE_LOG(LogTemp, Log, TEXT("[MULTISPLINE] Final result: %d splines with %d junctions"), 
           CaveData.Splines.Num(), CaveData.Junctions.Num());
    
    return CaveData;
}


UProcgenArcanaCaveImporter::UProcgenArcanaCaveImporter()
{
    CachedEntrancePoint = FVector2D::ZeroVector;
}

USplineComponent* UProcgenArcanaCaveImporter::ImportCaveFromSVG(const FProcgenArcanaImportSettings& Settings, UWorld* World, AActor* Owner)
{
    if (!World)
    {
        UE_LOG(LogTemp, Error, TEXT("[ProcgenArcanaImporter] World is null"));
        return nullptr;
    }

    // Load and parse SVG
    FString SVGContent = LoadSVGFile(Settings.SVGFilePath);
    if (SVGContent.IsEmpty())
    {
        UE_LOG(LogTemp, Error, TEXT("[ProcgenArcanaImporter] Failed to load SVG file: %s"), *Settings.SVGFilePath);
        return nullptr;
    }

    // Parse the SVG path
    TArray<FSVGPathPoint> BoundaryPath = ExtractMainCavePath(SVGContent);
    if (BoundaryPath.Num() < 3)
    {
        UE_LOG(LogTemp, Error, TEXT("[ProcgenArcanaImporter] Failed to extract valid cave path from SVG"));
        return nullptr;
    }

    UE_LOG(LogTemp, Log, TEXT("[ProcgenArcanaImporter] Extracted %d boundary points"), BoundaryPath.Num());

    // Simplify the path
    TArray<FSVGPathPoint> SimplifiedPath = SimplifyPath(BoundaryPath, Settings.SimplificationLevel, Settings.MaxSplinePoints);
    UE_LOG(LogTemp, Log, TEXT("[ProcgenArcanaImporter] Simplified to %d points"), SimplifiedPath.Num());

    // Generate centerline
    TArray<FVector> CenterlinePoints = GenerateCenterline(SimplifiedPath);
    if (CenterlinePoints.Num() < 2)
    {
        UE_LOG(LogTemp, Error, TEXT("[ProcgenArcanaImporter] Failed to generate valid centerline"));
        return nullptr;
    }

    // Detect entrance
    FVector2D EntrancePoint;
    if (Settings.bAutoDetectEntrance)
    {
        EntrancePoint = DetectEntranceFromSVG(SVGContent);
        if (EntrancePoint == FVector2D::ZeroVector)
        {
            // Fallback to first point of the path
            EntrancePoint = CenterlinePoints.Num() > 0 ? FVector2D(CenterlinePoints[0].X, CenterlinePoints[0].Y) : FVector2D::ZeroVector;
        }
    }
    else
    {
        EntrancePoint = Settings.ManualEntrancePoint;
    }

    // Apply height profile
    TArray<FVector> FinalPoints = ApplyHeightProfile(CenterlinePoints, Settings, EntrancePoint);

    // FIXED: Create spline component properly
    USplineComponent* SplineComponent = NewObject<USplineComponent>();
    if (!SplineComponent)
    {
        UE_LOG(LogTemp, Error, TEXT("[ProcgenArcanaImporter] Failed to create spline component"));
        return nullptr;
    }

    // Clear existing points and set up the spline
    SplineComponent->ClearSplinePoints();

    // Add points to spline
    for (int32 i = 0; i < FinalPoints.Num(); i++)
    {
        SplineComponent->AddSplinePoint(FinalPoints[i], ESplineCoordinateSpace::Local);
        SplineComponent->SetSplinePointType(i, ESplinePointType::Curve);
    }

    // Update spline tangents
    SplineComponent->UpdateSpline();

    // Set spline to closed if it's a closed cave system
    bool bIsClosedLoop = FVector::Dist(FinalPoints[0], FinalPoints.Last()) < Settings.CaveScale * 0.1f;
    SplineComponent->SetClosedLoop(bIsClosedLoop);

    UE_LOG(LogTemp, Log, TEXT("[ProcgenArcanaImporter] Successfully created spline with %d points"), FinalPoints.Num());
    
    return SplineComponent;
}



TArray<FVector> UProcgenArcanaCaveImporter::PreviewCaveFromSVG(const FProcgenArcanaImportSettings& Settings)
{
    TArray<FVector> PreviewPoints;

    // Load and parse SVG
    FString SVGContent = LoadSVGFile(Settings.SVGFilePath);
    if (SVGContent.IsEmpty())
    {
        return PreviewPoints;
    }

    // Parse and process the path
    TArray<FSVGPathPoint> BoundaryPath = ExtractMainCavePath(SVGContent);
    TArray<FSVGPathPoint> SimplifiedPath = SimplifyPath(BoundaryPath, Settings.SimplificationLevel, Settings.MaxSplinePoints);
    TArray<FVector> CenterlinePoints = GenerateCenterline(SimplifiedPath);

    // Detect entrance
    FVector2D EntrancePoint = Settings.bAutoDetectEntrance ? 
        DetectEntranceFromSVG(SVGContent) : Settings.ManualEntrancePoint;

    // Apply height profile
    PreviewPoints = ApplyHeightProfile(CenterlinePoints, Settings, EntrancePoint);

    return PreviewPoints;
}


// Update the DetectEntranceFromSVG function for this format:
FVector2D UProcgenArcanaCaveImporter::DetectEntranceFromSVG(const FString& SVGContent)
{
    // For Procgen format, find entrances from the line network
    TArray<TPair<FVector2D, FVector2D>> LineSegments;
    
    FRegexPattern LinePattern(TEXT("M\\s*([+-]?\\d*\\.?\\d+)[\\s,]+([+-]?\\d*\\.?\\d+)\\s*L\\s*([+-]?\\d*\\.?\\d+)[\\s,]+([+-]?\\d*\\.?\\d+)"));
    FRegexMatcher LineMatcher(LinePattern, SVGContent);

    while (LineMatcher.FindNext())
    {
        float StartX = FCString::Atof(*LineMatcher.GetCaptureGroup(1));
        float StartY = FCString::Atof(*LineMatcher.GetCaptureGroup(2));
        float EndX = FCString::Atof(*LineMatcher.GetCaptureGroup(3));
        float EndY = FCString::Atof(*LineMatcher.GetCaptureGroup(4));

        LineSegments.Add(TPair<FVector2D, FVector2D>(FVector2D(StartX, StartY), FVector2D(EndX, EndY)));
    }
    
    TArray<FVector2D> AllPoints;
    for (const auto& Segment : LineSegments)
    {
        AllPoints.AddUnique(Segment.Key);
        AllPoints.AddUnique(Segment.Value);
    }
    
    TArray<FVector2D> Entrances = FindEntrances(AllPoints, LineSegments);
    
    if (Entrances.Num() > 0)
    {
        UE_LOG(LogTemp, Log, TEXT("[ProcgenArcanaImporter] Found entrance at: %f, %f"), Entrances[0].X, Entrances[0].Y);
        return Entrances[0];
    }
    
    // Fallback to first point
    if (AllPoints.Num() > 0)
    {
        UE_LOG(LogTemp, Warning, TEXT("[ProcgenArcanaImporter] Using first point as entrance: %f, %f"), AllPoints[0].X, AllPoints[0].Y);
        return AllPoints[0];
    }
    
    UE_LOG(LogTemp, Warning, TEXT("[ProcgenArcanaImporter] Could not detect entrance"));
    return FVector2D::ZeroVector;
}

FString UProcgenArcanaCaveImporter::LoadSVGFile(const FString& FilePath)
{
    FString SVGContent;
    if (!FFileHelper::LoadFileToString(SVGContent, *FilePath))
    {
        UE_LOG(LogTemp, Error, TEXT("[ProcgenArcanaImporter] Failed to load file: %s"), *FilePath);
        return FString();
    }
    return SVGContent;
}

// =============================================================================
// Enhanced SVG Parser for Procgen/Arcana Line-Based Format
// =============================================================================

// Replace the ExtractMainCavePath function in ProcgenArcanaCaveImporter.cpp:

TArray<FSVGPathPoint> UProcgenArcanaCaveImporter::ExtractMainCavePath(const FString& SVGContent)
{
    TArray<FSVGPathPoint> PathPoints;

    UE_LOG(LogTemp, Log, TEXT("[ProcgenArcanaImporter] Analyzing Procgen/Arcana line-based SVG format"));

    // Step 1: Extract all line segments from the SVG
    TArray<FVector2D> AllLinePoints;
    TArray<TPair<FVector2D, FVector2D>> LineSegments;

    // Parse individual line segments (M x,y L x,y format)
    FRegexPattern LinePattern(TEXT("M\\s*([+-]?\\d*\\.?\\d+)[\\s,]+([+-]?\\d*\\.?\\d+)\\s*L\\s*([+-]?\\d*\\.?\\d+)[\\s,]+([+-]?\\d*\\.?\\d+)"));
    FRegexMatcher LineMatcher(LinePattern, SVGContent);

    while (LineMatcher.FindNext())
    {
        float StartX = FCString::Atof(*LineMatcher.GetCaptureGroup(1));
        float StartY = FCString::Atof(*LineMatcher.GetCaptureGroup(2));
        float EndX = FCString::Atof(*LineMatcher.GetCaptureGroup(3));
        float EndY = FCString::Atof(*LineMatcher.GetCaptureGroup(4));

        FVector2D StartPoint(StartX, StartY);
        FVector2D EndPoint(EndX, EndY);

        LineSegments.Add(TPair<FVector2D, FVector2D>(StartPoint, EndPoint));
        AllLinePoints.AddUnique(StartPoint);
        AllLinePoints.AddUnique(EndPoint);
    }

    UE_LOG(LogTemp, Log, TEXT("[ProcgenArcanaImporter] Found %d line segments, %d unique points"), 
           LineSegments.Num(), AllLinePoints.Num());

    if (LineSegments.Num() == 0)
    {
        UE_LOG(LogTemp, Error, TEXT("[ProcgenArcanaImporter] No line segments found in SVG"));
        return PathPoints;
    }

    // Step 2: Find the bounds to identify the cave outline
    FVector2D MinBounds(FLT_MAX, FLT_MAX);
    FVector2D MaxBounds(-FLT_MAX, -FLT_MAX);

    for (const FVector2D& Point : AllLinePoints)
    {
        MinBounds.X = FMath::Min(MinBounds.X, Point.X);
        MinBounds.Y = FMath::Min(MinBounds.Y, Point.Y);
        MaxBounds.X = FMath::Max(MaxBounds.X, Point.X);
        MaxBounds.Y = FMath::Max(MaxBounds.Y, Point.Y);
    }

    UE_LOG(LogTemp, Log, TEXT("[ProcgenArcanaImporter] Cave bounds: Min(%.2f, %.2f) Max(%.2f, %.2f)"), 
           MinBounds.X, MinBounds.Y, MaxBounds.X, MaxBounds.Y);

    // Step 3: Connect line segments to form continuous paths
    TArray<FVector2D> ContinuousPath = ConnectLineSegments(LineSegments);

    if (ContinuousPath.Num() < 3)
    {
        UE_LOG(LogTemp, Warning, TEXT("[ProcgenArcanaImporter] Could not form continuous path, using boundary points"));
        ContinuousPath = CreateBoundaryPath(AllLinePoints, MinBounds, MaxBounds);
    }

    // Step 4: Convert to FSVGPathPoint array
    float TotalDistance = 0.0f;
    for (int32 i = 0; i < ContinuousPath.Num(); i++)
    {
        FSVGPathPoint PathPoint(ContinuousPath[i]);
        
        if (i > 0)
        {
            TotalDistance += FVector2D::Distance(ContinuousPath[i], ContinuousPath[i-1]);
        }
        
        PathPoint.DistanceFromStart = TotalDistance;
        PathPoint.bIsImportant = IsImportantPoint(ContinuousPath[i], LineSegments);
        PathPoints.Add(PathPoint);
    }

    UE_LOG(LogTemp, Log, TEXT("[ProcgenArcanaImporter] Generated continuous path with %d points"), PathPoints.Num());
    return PathPoints;
}

TArray<FVector2D> UProcgenArcanaCaveImporter::ConnectLineSegments(const TArray<TPair<FVector2D, FVector2D>>& LineSegments)
{
    TArray<FVector2D> ConnectedPath;
    
    if (LineSegments.Num() == 0)
    {
        return ConnectedPath;
    }

    // Start with the first line segment
    TArray<TPair<FVector2D, FVector2D>> RemainingSegments = LineSegments;
    TPair<FVector2D, FVector2D> CurrentSegment = RemainingSegments[0];
    RemainingSegments.RemoveAt(0);

    ConnectedPath.Add(CurrentSegment.Key);
    ConnectedPath.Add(CurrentSegment.Value);

    FVector2D LastPoint = CurrentSegment.Value;
    const float ConnectionTolerance = 5.0f; // Tolerance for connecting nearby points

    // Try to connect segments
    while (RemainingSegments.Num() > 0)
    {
        bool bFoundConnection = false;
        
        for (int32 i = 0; i < RemainingSegments.Num(); i++)
        {
            const TPair<FVector2D, FVector2D>& Segment = RemainingSegments[i];
            
            // Check if this segment connects to our current path
            if (FVector2D::Distance(LastPoint, Segment.Key) < ConnectionTolerance)
            {
                // Connect from Key to Value
                ConnectedPath.Add(Segment.Value);
                LastPoint = Segment.Value;
                RemainingSegments.RemoveAt(i);
                bFoundConnection = true;
                break;
            }
            else if (FVector2D::Distance(LastPoint, Segment.Value) < ConnectionTolerance)
            {
                // Connect from Value to Key (reverse direction)
                ConnectedPath.Add(Segment.Key);
                LastPoint = Segment.Key;
                RemainingSegments.RemoveAt(i);
                bFoundConnection = true;
                break;
            }
        }

        // If no connection found, we might have separate cave sections
        if (!bFoundConnection)
        {
            UE_LOG(LogTemp, Warning, TEXT("[ProcgenArcanaImporter] Found disconnected cave section, %d segments remaining"), 
                   RemainingSegments.Num());
            
            // Start a new section if we have a reasonable path already
            if (ConnectedPath.Num() > 10)
            {
                break;
            }
            
            // Otherwise try to start from a remaining segment
            if (RemainingSegments.Num() > 0)
            {
                CurrentSegment = RemainingSegments[0];
                RemainingSegments.RemoveAt(0);
                ConnectedPath.Add(CurrentSegment.Key);
                ConnectedPath.Add(CurrentSegment.Value);
                LastPoint = CurrentSegment.Value;
            }
        }
    }

    return ConnectedPath;
}

TArray<FVector2D> UProcgenArcanaCaveImporter::CreateBoundaryPath(const TArray<FVector2D>& AllPoints, const FVector2D& MinBounds, const FVector2D& MaxBounds)
{
    TArray<FVector2D> BoundaryPath;
    
    // Create a simplified boundary path using the outermost points
    // This is a fallback when we can't connect the line segments properly
    
    FVector2D Center = (MinBounds + MaxBounds) * 0.5f;
    
    // Sort points by angle from center to create a rough outline
    TArray<FVector2D> SortedPoints = AllPoints;
    SortedPoints.Sort([Center](const FVector2D& A, const FVector2D& B) {
        float AngleA = FMath::Atan2(A.Y - Center.Y, A.X - Center.X);
        float AngleB = FMath::Atan2(B.Y - Center.Y, B.X - Center.X);
        return AngleA < AngleB;
    });
    
    // Take every Nth point to create a simplified boundary
    int32 StepSize = FMath::Max(1, SortedPoints.Num() / 50); // Aim for ~50 boundary points
    
    for (int32 i = 0; i < SortedPoints.Num(); i += StepSize)
    {
        BoundaryPath.Add(SortedPoints[i]);
    }
    
    // Close the loop
    if (BoundaryPath.Num() > 0 && FVector2D::Distance(BoundaryPath[0], BoundaryPath.Last()) > 10.0f)
    {
        BoundaryPath.Add(BoundaryPath[0]);
    }
    
    UE_LOG(LogTemp, Log, TEXT("[ProcgenArcanaImporter] Created boundary path with %d points"), BoundaryPath.Num());
    return BoundaryPath;
}

bool UProcgenArcanaCaveImporter::IsImportantPoint(const FVector2D& Point, const TArray<TPair<FVector2D, FVector2D>>& LineSegments)
{
    // Count how many line segments connect to this point
    int32 ConnectionCount = 0;
    
    const float Tolerance = 2.0f;
    
    for (const TPair<FVector2D, FVector2D>& Segment : LineSegments)
    {
        if (FVector2D::Distance(Point, Segment.Key) < Tolerance ||
            FVector2D::Distance(Point, Segment.Value) < Tolerance)
        {
            ConnectionCount++;
        }
    }
    
    // Points with 3+ connections are likely corners or junctions (important)
    return ConnectionCount >= 3;
}

TArray<FVector2D> UProcgenArcanaCaveImporter::FindEntrances(const TArray<FVector2D>& BoundaryPath, const TArray<TPair<FVector2D, FVector2D>>& LineSegments)
{
    TArray<FVector2D> Entrances;
    
    // For line-based caves, entrances are typically points that only connect to one line segment
    // (dead ends in the line network)
    
    const float Tolerance = 2.0f;
    
    for (const FVector2D& Point : BoundaryPath)
    {
        int32 ConnectionCount = 0;
        
        for (const TPair<FVector2D, FVector2D>& Segment : LineSegments)
        {
            if (FVector2D::Distance(Point, Segment.Key) < Tolerance ||
                FVector2D::Distance(Point, Segment.Value) < Tolerance)
            {
                ConnectionCount++;
            }
        }
        
        // Points with only 1 connection are likely entrances
        if (ConnectionCount == 1)
        {
            Entrances.Add(Point);
        }
    }
    
    UE_LOG(LogTemp, Log, TEXT("[ProcgenArcanaImporter] Found %d potential entrances"), Entrances.Num());
    return Entrances;
}


bool UProcgenArcanaCaveImporter::IsDecorative(const FString& PathElement)
{
    // Check for decorative indicators
    if (PathElement.Contains(TEXT("stroke-width=\"0.3\"")) ||
        PathElement.Contains(TEXT("stroke-width=\"1.2\"")) ||
        PathElement.Contains(TEXT("opacity=\"0.2\"")) ||
        PathElement.Contains(TEXT("grid")) ||
        PathElement.Contains(TEXT("decoration")))
    {
        return true;
    }
    
    return false;
}

TArray<FVector2D> UProcgenArcanaCaveImporter::ParseSVGPathCommands(const FString& PathData)
{
    TArray<FVector2D> Points;
    
    // Split path data by commands (M, L, etc.)
    FRegexPattern CommandPattern(TEXT("([MLHVCSQTAZmlhvcsqtaz])([^MLHVCSQTAZmlhvcsqtaz]*)"));
    FRegexMatcher CommandMatcher(CommandPattern, PathData);

    FVector2D CurrentPos(0, 0);
    FVector2D LastMovePos(0, 0);

    while (CommandMatcher.FindNext())
    {
        FString Command = CommandMatcher.GetCaptureGroup(1);
        FString Params = CommandMatcher.GetCaptureGroup(2).TrimStartAndEnd();

        // Parse coordinate pairs
        FRegexPattern CoordPattern(TEXT("([+-]?\\d*\\.?\\d+)[\\s,]+([+-]?\\d*\\.?\\d+)"));
        FRegexMatcher CoordMatcher(CoordPattern, Params);

        if (Command == TEXT("M")) // MoveTo (absolute)
        {
            if (CoordMatcher.FindNext())
            {
                CurrentPos.X = FCString::Atof(*CoordMatcher.GetCaptureGroup(1));
                CurrentPos.Y = FCString::Atof(*CoordMatcher.GetCaptureGroup(2));
                LastMovePos = CurrentPos;
                Points.Add(CurrentPos);
            }
        }
        else if (Command == TEXT("L")) // LineTo (absolute)
        {
            while (CoordMatcher.FindNext())
            {
                CurrentPos.X = FCString::Atof(*CoordMatcher.GetCaptureGroup(1));
                CurrentPos.Y = FCString::Atof(*CoordMatcher.GetCaptureGroup(2));
                Points.Add(CurrentPos);
            }
        }
        else if (Command == TEXT("l")) // LineTo (relative)
        {
            while (CoordMatcher.FindNext())
            {
                float DeltaX = FCString::Atof(*CoordMatcher.GetCaptureGroup(1));
                float DeltaY = FCString::Atof(*CoordMatcher.GetCaptureGroup(2));
                CurrentPos.X += DeltaX;
                CurrentPos.Y += DeltaY;
                Points.Add(CurrentPos);
            }
        }
        else if (Command == TEXT("Z") || Command == TEXT("z")) // ClosePath
        {
            if (Points.Num() > 0 && CurrentPos != LastMovePos)
            {
                Points.Add(LastMovePos); // Close the path
                CurrentPos = LastMovePos;
            }
        }
        // Add more command types as needed (C for curves, etc.)
    }

    return Points;
}

TArray<FSVGPathPoint> UProcgenArcanaCaveImporter::SimplifyPath(const TArray<FSVGPathPoint>& OriginalPath, float SimplificationLevel, int32 MaxPoints)
{
    if (OriginalPath.Num() <= MaxPoints)
    {
        return OriginalPath;
    }

    // Calculate tolerance based on simplification level
    float PathBounds = 0.0f;
    for (int32 i = 1; i < OriginalPath.Num(); i++)
    {
        PathBounds = FMath::Max(PathBounds, FVector2D::Distance(OriginalPath[i].Position, OriginalPath[0].Position));
    }
    
    float Tolerance = (SimplificationLevel * PathBounds) * 0.01f; // 1% to 10% of bounds

    // Apply Douglas-Peucker algorithm
    TArray<FSVGPathPoint> SimplifiedPath = DouglasPeucker(OriginalPath, Tolerance);

    // If still too many points, use distance-based reduction
    if (SimplifiedPath.Num() > MaxPoints)
    {
        TArray<FSVGPathPoint> FinalPath;
        float StepSize = static_cast<float>(SimplifiedPath.Num() - 1) / static_cast<float>(MaxPoints - 1);
        
        for (int32 i = 0; i < MaxPoints; i++)
        {
            int32 Index = FMath::RoundToInt(i * StepSize);
            Index = FMath::Clamp(Index, 0, SimplifiedPath.Num() - 1);
            FinalPath.Add(SimplifiedPath[Index]);
        }
        
        return FinalPath;
    }

    return SimplifiedPath;
}

TArray<FSVGPathPoint> UProcgenArcanaCaveImporter::DouglasPeucker(const TArray<FSVGPathPoint>& Points, float Tolerance)
{
    if (Points.Num() < 3)
    {
        return Points;
    }

    TArray<bool> KeepPoints;
    KeepPoints.SetNumZeroed(Points.Num());
    KeepPoints[0] = true; // Always keep first point
    KeepPoints.Last() = true; // Always keep last point

    DouglasPeuckerRecursive(Points, 0, Points.Num() - 1, Tolerance, KeepPoints);

    TArray<FSVGPathPoint> SimplifiedPoints;
    for (int32 i = 0; i < Points.Num(); i++)
    {
        if (KeepPoints[i])
        {
            SimplifiedPoints.Add(Points[i]);
        }
    }

    return SimplifiedPoints;
}

void UProcgenArcanaCaveImporter::DouglasPeuckerRecursive(const TArray<FSVGPathPoint>& Points, int32 StartIndex, int32 EndIndex, float Tolerance, TArray<bool>& KeepPoints)
{
    if (EndIndex - StartIndex < 2)
    {
        return;
    }

    float MaxDistance = 0.0f;
    int32 MaxIndex = -1;

    // Find the point with maximum distance from the line
    for (int32 i = StartIndex + 1; i < EndIndex; i++)
    {
        float Distance = PerpendicularDistance(Points[i], Points[StartIndex], Points[EndIndex]);
        if (Distance > MaxDistance)
        {
            MaxDistance = Distance;
            MaxIndex = i;
        }
    }

    // If max distance is greater than tolerance, recursively simplify
    if (MaxDistance > Tolerance && MaxIndex != -1)
    {
        KeepPoints[MaxIndex] = true;
        DouglasPeuckerRecursive(Points, StartIndex, MaxIndex, Tolerance, KeepPoints);
        DouglasPeuckerRecursive(Points, MaxIndex, EndIndex, Tolerance, KeepPoints);
    }
}

float UProcgenArcanaCaveImporter::PerpendicularDistance(const FSVGPathPoint& Point, const FSVGPathPoint& LineStart, const FSVGPathPoint& LineEnd)
{
    FVector2D LineVector = LineEnd.Position - LineStart.Position;
    FVector2D PointVector = Point.Position - LineStart.Position;

    if (LineVector.SizeSquared() < SMALL_NUMBER)
    {
        return FVector2D::Distance(Point.Position, LineStart.Position);
    }

    float t = FVector2D::DotProduct(PointVector, LineVector) / LineVector.SizeSquared();
    t = FMath::Clamp(t, 0.0f, 1.0f);

    FVector2D Projection = LineStart.Position + (t * LineVector);
    return FVector2D::Distance(Point.Position, Projection);
}

TArray<FVector> UProcgenArcanaCaveImporter::GenerateCenterline(const TArray<FSVGPathPoint>& BoundaryPath)
{
    TArray<FVector> CenterlinePoints;

    // For now, implement a simple centerline by using the boundary path directly
    // In a more advanced implementation, this would use Voronoi diagrams or medial axis transform
    
    for (const FSVGPathPoint& Point : BoundaryPath)
    {
        FVector2D UE5Point = ConvertSVGToUE5Coordinates(Point.Position, 1.0f); // Scale applied later
        CenterlinePoints.Add(FVector(UE5Point.X, UE5Point.Y, 0.0f));
    }

    return CenterlinePoints;
}

FVector2D UProcgenArcanaCaveImporter::ConvertSVGToUE5Coordinates(const FVector2D& SVGPoint, float Scale)
{
    // SVG has Y increasing downward, UE5 has Y increasing upward
    // Also apply scaling
    return FVector2D(SVGPoint.X * Scale, -SVGPoint.Y * Scale);
}

TArray<FVector> UProcgenArcanaCaveImporter::ApplyHeightProfile(const TArray<FVector>& CenterlinePoints, const FProcgenArcanaImportSettings& Settings, const FVector2D& EntrancePoint)
{
    TArray<FVector> HeightAdjustedPoints;

    // First, apply the base scale to all points
    for (int32 i = 0; i < CenterlinePoints.Num(); i++)
    {
        FVector ScaledPoint = CenterlinePoints[i] * Settings.CaveScale;
        HeightAdjustedPoints.Add(ScaledPoint);
    }

    // Apply height calculations based on selected mode
    for (int32 i = 0; i < HeightAdjustedPoints.Num(); i++)
    {
        float Height = 0.0f;

        switch (Settings.HeightMode)
        {
            case EHeightMode::Flat:
                Height = CalculateFlatHeight(i, HeightAdjustedPoints, Settings);
                break;
            case EHeightMode::Descending:
                Height = CalculateDescendingHeight(i, HeightAdjustedPoints, Settings, EntrancePoint);
                break;
            case EHeightMode::Rolling:
                Height = CalculateRollingHeight(i, HeightAdjustedPoints, Settings, EntrancePoint);
                break;
            case EHeightMode::Mountainous:
                Height = CalculateMountainousHeight(i, HeightAdjustedPoints, Settings, EntrancePoint);
                break;
            default:
                Height = CalculateDescendingHeight(i, HeightAdjustedPoints, Settings, EntrancePoint);
                break;
        }

        HeightAdjustedPoints[i].Z = Height;
    }

    return HeightAdjustedPoints;
}

float UProcgenArcanaCaveImporter::CalculateFlatHeight(int32 PointIndex, const TArray<FVector>& Points, const FProcgenArcanaImportSettings& Settings)
{
    // Small random variation for flat caves
    float RandomVariation = FMath::FRandRange(-5.0f, 5.0f);
    return RandomVariation;
}

float UProcgenArcanaCaveImporter::CalculateDescendingHeight(int32 PointIndex, const TArray<FVector>& Points, const FProcgenArcanaImportSettings& Settings, const FVector2D& EntrancePoint)
{
    if (Points.Num() < 2)
    {
        return 0.0f;
    }

    // Find closest point to entrance (this becomes our elevation reference)
    int32 EntranceIndex = 0;
    float MinDistance = FVector2D::Distance(EntrancePoint, FVector2D(Points[0].X, Points[0].Y));
    
    for (int32 i = 1; i < Points.Num(); i++)
    {
        float Distance = FVector2D::Distance(EntrancePoint, FVector2D(Points[i].X, Points[i].Y));
        if (Distance < MinDistance)
        {
            MinDistance = Distance;
            EntranceIndex = i;
        }
    }

    // Calculate path distance from entrance
    float PathDistance = CalculateTotalPathDistance(Points, EntranceIndex, PointIndex);
    
    // Base descent
    float BaseHeight = -PathDistance * Settings.DescentRate;
    
    // Add variation
    float Noise = FMath::PerlinNoise1D(PointIndex * 0.1f) * Settings.HeightVariation * 0.5f;
    
    return BaseHeight + Noise;
}

float UProcgenArcanaCaveImporter::CalculateRollingHeight(int32 PointIndex, const TArray<FVector>& Points, const FProcgenArcanaImportSettings& Settings, const FVector2D& EntrancePoint)
{
    float BaseHeight = CalculateDescendingHeight(PointIndex, Points, Settings, EntrancePoint);
    
    // Add sine wave variation
    float WaveHeight = FMath::Sin(PointIndex * 0.2f) * Settings.HeightVariation * 0.3f;
    
    return BaseHeight + WaveHeight;
}

float UProcgenArcanaCaveImporter::CalculateMountainousHeight(int32 PointIndex, const TArray<FVector>& Points, const FProcgenArcanaImportSettings& Settings, const FVector2D& EntrancePoint)
{
    float BaseHeight = CalculateDescendingHeight(PointIndex, Points, Settings, EntrancePoint);
    
    // Add dramatic height variations
    float MountainNoise = FMath::PerlinNoise1D(PointIndex * 0.05f) * Settings.HeightVariation;
    float DetailNoise = FMath::PerlinNoise1D(PointIndex * 0.3f) * Settings.HeightVariation * 0.2f;
    
    return BaseHeight + MountainNoise + DetailNoise;
}

float UProcgenArcanaCaveImporter::CalculateTotalPathDistance(const TArray<FVector>& Points, int32 StartIndex, int32 EndIndex)
{
    if (StartIndex == EndIndex)
    {
        return 0.0f;
    }

    float TotalDistance = 0.0f;
    int32 Step = (EndIndex > StartIndex) ? 1 : -1;
    
    for (int32 i = StartIndex; i != EndIndex; i += Step)
    {
        int32 NextIndex = i + Step;
        if (NextIndex >= 0 && NextIndex < Points.Num())
        {
            TotalDistance += FVector::Dist(Points[i], Points[NextIndex]);
        }
    }

    return TotalDistance;
}
