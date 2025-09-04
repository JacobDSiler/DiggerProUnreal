#include "VoxelLogManager.h"

TMap<FString, FString> VoxelLogManager::AggregatedLogs;

void VoxelLogManager::AggregateLog(const FString& Category, const FString& Message)
{
    if (AggregatedLogs.Contains(Category))
    {
        AggregatedLogs[Category] += TEXT("\n") + Message;
    }
    else
    {
        AggregatedLogs.Add(Category, Message);
    }
}

void VoxelLogManager::FlushAggregatedLog(const FString& Category)
{
    if (AggregatedLogs.Contains(Category))
    {
        UE_LOG(LogTemp, Log, TEXT("[%s] %s"), *Category, *AggregatedLogs[Category]);
        AggregatedLogs.Remove(Category);
    }
}

void VoxelLogManager::FlushAll()
{
    for (const auto& Elem : AggregatedLogs)
    {
        UE_LOG(LogTemp, Log, TEXT("[%s] %s"), *Elem.Key, *Elem.Value);
    }
    AggregatedLogs.Empty();
}

