#include "SocketIOLobbyManager.h"
#include "Engine/World.h"
#include "TimerManager.h"
#include "Engine/Engine.h"
#include "Async/Async.h"
#include "SIOJsonValue.h"
#include "SIOJsonObject.h"
#include "SocketIOFunctionLibrary.h"

USocketIOLobbyManager::USocketIOLobbyManager()
{
    SocketIOClient = nullptr;
    bIsConnected = false;
    bIsInitialized = false;
    CurrentSocketId = TEXT("");
    CurrentSessionId = TEXT("");
    ReconnectionAttempts = 0;
}

void USocketIOLobbyManager::Initialize(UWorld* WorldContext)
{
    if (!WorldContext)
    {
        UE_LOG(LogTemp, Warning, TEXT("%s: Invalid WorldContext"), *FString(__FUNCTION__));
        return;
    }

    if (!IsInGameThread())
    {
        AsyncTask(ENamedThreads::GameThread, [this, WorldContext]() {
            Initialize(WorldContext);
        });
        return;
    }

    if (bIsInitialized && SocketIOClient && bIsConnected)
    {
        UE_LOG(LogTemp, Log, TEXT("%s: Already initialized and connected"), *FString(__FUNCTION__));
        return;
    }

    // Reset state
    bIsConnected = false;
    CurrentSocketId.Empty();
    CurrentSessionId.Empty();
    ReconnectionAttempts = 0;

    // Clean up existing client
    if (SocketIOClient)
    {
        UE_LOG(LogTemp, Log, TEXT("%s: Cleaning up previous SocketIOClient"), *FString(__FUNCTION__));
        SocketIOClient->OnConnected.RemoveAll(this);
        SocketIOClient->OnDisconnected.RemoveAll(this);
        SocketIOClient->ConditionalBeginDestroy();
        SocketIOClient = nullptr;
    }

    // Create new client component using helper to ensure proper initialization
    SocketIOClient = USocketIOFunctionLibrary::ConstructSocketIOComponent(WorldContext);
    if (!SocketIOClient)
    {
        UE_LOG(LogTemp, Error, TEXT("%s: Failed to create SocketIOClientComponent"), *FString(__FUNCTION__));
        return;
    }

    // Configure client
    SocketIOClient->bLimitConnectionToGameWorld = false; // Ensure editor connections work
    SocketIOClient->bVerboseConnectionLog = true; // For debugging
    
    // Bind events
    SocketIOClient->OnConnected.AddDynamic(this, &USocketIOLobbyManager::HandleConnected);
    SocketIOClient->OnDisconnected.AddDynamic(this, &USocketIOLobbyManager::HandleDisconnected);

    bIsInitialized = true;
    
    // Delayed connection to ensure proper initialization
    FTimerHandle DelayedConnectTimer;
    WorldContext->GetTimerManager().SetTimer(
        DelayedConnectTimer,
        [this]()
        {
            if (IsValid(SocketIOClient))
            {
                UE_LOG(LogTemp, Log, TEXT("Attempting to connect to Socket.IO server"));
                
                // Create proper JSON payload if needed
                USIOJsonObject* QueryParams = NewObject<USIOJsonObject>();
                USIOJsonObject* Headers = NewObject<USIOJsonObject>();
                
                // Add any custom query/header parameters here
                // Headers->SetStringField(TEXT("Authorization"), TEXT("Bearer token"));
                
                SocketIOClient->Connect(
                    TEXT("http://127.0.0.1:3001"),
                    TEXT("/"), // Path
                    TEXT(""), // Auth token
                    QueryParams,
                    Headers
                );
            }
        },
        0.5f, // 500ms delay
        false
    );
}

// ... [rest of your existing methods unchanged] ...

bool USocketIOLobbyManager::IsConnected() const
{
    return bIsConnected;
}

void USocketIOLobbyManager::CreateLobby(const FString& LobbyName)
{
    if (!IsConnected() || !SocketIOClient) return;

    USIOJsonValue* JsonValue = USIOJsonValue::ConstructJsonValueString(this, LobbyName);
    SocketIOClient->Emit(TEXT("createLobby"), JsonValue);
}

void USocketIOLobbyManager::JoinLobby(const FString& LobbyId)
{
    if (!IsConnected() || !SocketIOClient) return;

    USIOJsonValue* JsonValue = USIOJsonValue::ConstructJsonValueString(this, LobbyId);
    SocketIOClient->Emit(TEXT("joinLobby"), JsonValue);
}

void USocketIOLobbyManager::HandleConnected(FString SocketId, FString SessionId, bool bReconnected)
{
    OnConnected(SocketId, SessionId, bReconnected);
}

void USocketIOLobbyManager::HandleDisconnected(TEnumAsByte<ESIOConnectionCloseReason> Reason)
{
    OnDisconnected(Reason);
}

void USocketIOLobbyManager::AttemptReconnection()
{
    if (IsValid(SocketIOClient))
    {
        SocketIOClient->Connect(TEXT("http://127.0.0.1:3001"));
    }
}

void USocketIOLobbyManager::HandleConnectionError(const FString& ErrorMessage)
{
    UE_LOG(LogTemp, Error, TEXT("Socket.IO Connection Error: %s"), *ErrorMessage);
    // Handle reconnection or UI feedback here
}

void USocketIOLobbyManager::OnConnected(FString SocketId, FString SessionId, bool bIsReconnection)
{
    CurrentSocketId = SocketId;
    CurrentSessionId = SessionId;
    bIsConnected = true;
    ReconnectionAttempts = 0;

    OnLobbyConnected.Broadcast(SocketId, SessionId);

    if (SocketIOClient)
    {
        SocketIOClient->Emit(TEXT("getLobbyList"));
    }
}

void USocketIOLobbyManager::OnDisconnected(TEnumAsByte<ESIOConnectionCloseReason> Reason)
{
    bIsConnected = false;
    CurrentSocketId.Empty();
    CurrentSessionId.Empty();

    if (ReconnectionAttempts < 3)
    {
        ReconnectionAttempts++;
        if (UWorld* World = GetWorld())
        {
            World->GetTimerManager().SetTimer(
                ReconnectionTimer,
                this,
                &USocketIOLobbyManager::AttemptReconnection,
                5.0f,
                false
            );
        }
    }

    OnLobbyDisconnected.Broadcast(Reason);
}