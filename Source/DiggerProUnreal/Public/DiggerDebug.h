#pragma once

#include "CoreMinimal.h"

/** Plain-old-data descriptor used to expose a debug flag and its bool pointer. */
struct FDiggerDebugFlag
{
    FDiggerDebugFlag() = default;
    FDiggerDebugFlag(const FName& InKey, bool* InValue)
        : Key(InKey)
        , Value(InValue)
    {
    }

    FName Key;
    bool* Value = nullptr;
};

/**
 * Singleton container that owns the Digger debug flags so that the editor UI
 * can discover and mutate them at runtime instead of relying on global
 * variables defined in a header.
 */
class FDiggerDebug
{
public:
    using FFlagRegistry = TMap<FName, bool*>;
    using FFlagList = TArray<FDiggerDebugFlag>;

    /** Retrieve the singleton instance. */
    static FDiggerDebug& Get();

    /** Map of all debug flag names to the underlying bool pointers. */
    const FFlagRegistry& GetFlagRegistry() const { return FlagRegistry; }
    FFlagRegistry& GetFlagRegistry() { return FlagRegistry; }

    /** Ordered list of all registered flags (useful for UI enumeration). */
    const FFlagList& GetAllFlags() const { return FlagList; }

    /** Find a particular debug flag by name. Returns nullptr when not found. */
    bool* FindFlag(const FName& FlagName);

    // --- Debug switches -----------------------------------------------------
    bool Verbose = false;
    bool Performance = false;
    bool Cache = false;
    bool Normals = false;
    bool Error = false;
    bool Holes = false;
    bool Landscape = false;
    bool VoxelConv = false;
    bool Mesh = false;
    bool Islands = false;
    bool Brush = false;
    bool Context = false;
    bool UserConv = false;
    bool IO = false;
    bool Threads = false;
    bool Space = false;
    bool Chunks = false;
    bool Voxels = false;
    bool Casts = false;
    bool Delegates = false;
    bool Manager = false;
    bool Caves = false;
    bool Lights = false;
    bool Flags = false;
    bool VoxelModificationReports = false;
    bool Seams = false;

private:
    FDiggerDebug();

    void RegisterFlag(const TCHAR* FlagName, bool& FlagRef);

    FFlagRegistry FlagRegistry;
    FFlagList FlagList;
};

namespace DiggerDebug
{
    inline FDiggerDebug& Get() { return FDiggerDebug::Get(); }

    using FDiggerDebugFlag = ::FDiggerDebugFlag;

    inline bool& Verbose = FDiggerDebug::Get().Verbose;
    inline bool& Performance = FDiggerDebug::Get().Performance;
    inline bool& Cache = FDiggerDebug::Get().Cache;
    inline bool& Normals = FDiggerDebug::Get().Normals;
    inline bool& Error = FDiggerDebug::Get().Error;
    inline bool& Holes = FDiggerDebug::Get().Holes;
    inline bool& Landscape = FDiggerDebug::Get().Landscape;
    inline bool& VoxelConv = FDiggerDebug::Get().VoxelConv;
    inline bool& Mesh = FDiggerDebug::Get().Mesh;
    inline bool& Islands = FDiggerDebug::Get().Islands;
    inline bool& Brush = FDiggerDebug::Get().Brush;
    inline bool& Context = FDiggerDebug::Get().Context;
    inline bool& UserConv = FDiggerDebug::Get().UserConv;
    inline bool& IO = FDiggerDebug::Get().IO;
    inline bool& Threads = FDiggerDebug::Get().Threads;
    inline bool& Space = FDiggerDebug::Get().Space;
    inline bool& Chunks = FDiggerDebug::Get().Chunks;
    inline bool& Voxels = FDiggerDebug::Get().Voxels;
    inline bool& Casts = FDiggerDebug::Get().Casts;
    inline bool& Delegates = FDiggerDebug::Get().Delegates;
    inline bool& Manager = FDiggerDebug::Get().Manager;
    inline bool& Caves = FDiggerDebug::Get().Caves;
    inline bool& Lights = FDiggerDebug::Get().Lights;
    inline bool& Flags = FDiggerDebug::Get().Flags;
    inline bool& VoxelModificationReports = FDiggerDebug::Get().VoxelModificationReports;
    inline bool& Seams = FDiggerDebug::Get().Seams;

    inline FDiggerDebug::FFlagRegistry& GetFlagRegistry()
    {
        return FDiggerDebug::Get().GetFlagRegistry();
    }

    inline const TArray<FDiggerDebugFlag>& GetAllFlags()
    {
        return FDiggerDebug::Get().GetAllFlags();
    }

    inline bool* FindFlag(const FName& FlagName)
    {
        return FDiggerDebug::Get().FindFlag(FlagName);
    }
}
