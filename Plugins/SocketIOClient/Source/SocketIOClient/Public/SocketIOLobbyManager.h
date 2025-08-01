#pragma once

#include "CoreMinimal.h"
#include "UObject/NoExportTypes.h"
#include "SocketIOClientComponent.h"
#include "SocketIOLobbyManager.generated.h"

DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FOnLobbyConnected, const FString&, SocketId, const FString&, SessionId);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnLobbyDisconnected, TEnumAsByte<ESIOConnectionCloseReason>, Reason);

UCLASS(BlueprintType, Blueprintable)
class SOCKETIOCLIENT_API USocketIOLobbyManager : public UObject
{
    GENERATED_BODY()

public:
    USocketIOLobbyManager();

    UFUNCTION(BlueprintCallable, Category = "SocketIO|Lobby")
    void Initialize(UWorld* WorldContext);

    UFUNCTION(BlueprintCallable, Category = "SocketIO|Lobby")
    bool IsConnected() const;

    UFUNCTION(BlueprintCallable, Category = "SocketIO|Lobby")
    void CreateLobby(const FString& LobbyName);

    UFUNCTION(BlueprintCallable, Category = "SocketIO|Lobby")
    void JoinLobby(const FString& LobbyId);

    UFUNCTION(BlueprintCallable, Category = "SocketIO|Lobby")
    FString GetSocketId() const { return CurrentSocketId; }

    UFUNCTION(BlueprintCallable, Category = "SocketIO|Lobby")
    FString GetSessionId() const { return CurrentSessionId; }

    UPROPERTY(BlueprintAssignable, Category = "SocketIO|Lobby")
    FOnLobbyConnected OnLobbyConnected;

    UPROPERTY(BlueprintAssignable, Category = "SocketIO|Lobby")
    FOnLobbyDisconnected OnLobbyDisconnected;

protected:
    UFUNCTION()
    void HandleConnected(FString SocketId, FString SessionId, bool bReconnected);
    
    UFUNCTION()
    void HandleDisconnected(TEnumAsByte<ESIOConnectionCloseReason> Reason);

    UFUNCTION()
    void AttemptReconnection();
    void HandleConnectionError(const FString& ErrorMessage);

private:
    UPROPERTY(Transient)
    USocketIOClientComponent* SocketIOClient;

    UPROPERTY(Transient)
    bool bIsConnected;

    UPROPERTY(Transient)
    bool bIsInitialized;

    UPROPERTY(Transient)
    FString CurrentSocketId;

    UPROPERTY(Transient)
    FString CurrentSessionId;

    UPROPERTY(Transient)
    int32 ReconnectionAttempts;

    FTimerHandle ReconnectionTimer;

    void OnConnected(FString SocketId, FString SessionId, bool bIsReconnection);
    void OnDisconnected(TEnumAsByte<ESIOConnectionCloseReason> Reason);
};