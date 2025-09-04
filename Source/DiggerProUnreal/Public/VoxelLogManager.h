#pragma once

#include <CoreMinimal.h>

class VoxelLogManager
{
public:
    // Aggregates a log message for a given category (e.g., DiggerVerbose, DiggerChunk, etc.)
    static void AggregateLog(const FString& Category, const FString& Message);

    // Flushes all aggregated logs for a given category (outputs a single log message)
    static void FlushAggregatedLog(const FString& Category);

    // Optionally, flush all categories
    static void FlushAll();

private:
    static TMap<FString, FString> AggregatedLogs;
};

