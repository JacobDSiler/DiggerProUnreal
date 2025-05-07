#pragma once

#include "CoreMinimal.h"

/**
 * Static log aggregator for batching repetitive log messages.
 * Usage:
 *   VoxelLogAggregator::Add(TEXT("Message"));
 *   ...
 *   VoxelLogAggregator::Flush(TEXT("OptionalPrefix"));
 */
class VoxelLogAggregator
{
public:
    // Add a message to the aggregator
    static void Add(const FString& Message)
    {
        AggregatedLog += Message + TEXT("\n");
    }

    // Flush all aggregated messages to the log and clear the buffer
    static void Flush(const FString& Prefix = TEXT("[Aggregated]"))
    {
        if (!AggregatedLog.IsEmpty())
        {
            UE_LOG(LogTemp, Warning, TEXT("%s\n%s"), *Prefix, *AggregatedLog);
            AggregatedLog.Empty();
        }
    }

    // Optionally, clear without logging
    static void Clear()
    {
        AggregatedLog.Empty();
    }

private:
    static FString AggregatedLog;
};
