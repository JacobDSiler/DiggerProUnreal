#pragma once

#include "CoreMinimal.h"
#include "SocketIOClientComponent.h"
#include "SocketIOLobbyManager.generated.h"

UCLASS()
class SOCKETIOCLIENT_API USocketIOLobbyManager : public UObject
{
    GENERATED_BODY()

public:
    UFUNCTION()
    void Initialize(UWorld* WorldContext);

    UFUNCTION(BlueprintCallable, Category="Lobby")
    bool IsConnected() const;

    UFUNCTION(BlueprintCallable, Category="Lobby")
    void CreateLobby(const FString& LobbyName);

    UFUNCTION(BlueprintCallable, Category="Lobby")
    void JoinLobby(const FString& LobbyId);

    UFUNCTION(BlueprintCallable, Category="Lobby")
    void OnConnected(FString SocketId, FString SessionId, bool bIsReconnection); // No const&

    // Similarly for disconnect:
    UFUNCTION(BlueprintCallable, Category="Lobby")
    void OnDisconnected(TEnumAsByte<ESIOConnectionCloseReason> Reason); // Not int32


    UPROPERTY()
    USocketIOClientComponent* SocketIOClient;

    UFUNCTION()
    void HandleConnected(const FString& SocketId, const FString& SessionId, bool bReconnected);

    // Use int32, not EIOCloseReason
    UFUNCTION()
    void HandleDisconnected(int32 Code);

    bool bIsConnected = false;
    
private:
    // Connection state
    UPROPERTY()
    FString CurrentSocketId;
    
    UPROPERTY()
    FString CurrentSessionId;

public:
    // Delegate declarations
    DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FOnLobbyConnectedSignature, FString, SocketId, FString, SessionId);
    DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnLobbyDisconnectedSignature, TEnumAsByte<ESIOConnectionCloseReason>, Reason);

    UPROPERTY(BlueprintAssignable)
    FOnLobbyConnectedSignature OnLobbyConnected;

    UPROPERTY(BlueprintAssignable)
    FOnLobbyDisconnectedSignature OnLobbyDisconnected;
};
