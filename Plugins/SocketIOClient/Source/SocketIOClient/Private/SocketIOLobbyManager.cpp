#include "SocketIOLobbyManager.h"
#include "Engine/World.h"
#include "SocketIOClientComponent.h"

void USocketIOLobbyManager::Initialize(UWorld* WorldContext)
{
    if (!WorldContext)
    {
        UE_LOG(LogTemp, Warning, TEXT("%s: Invalid WorldContext"), *FString(__FUNCTION__));
        return;
    }

    // Only create & bind the component once
    if (SocketIOClient == nullptr)
    {
        // Use the WorldSettings as the Outer for our component
        AWorldSettings* WorldSettings = WorldContext->GetWorldSettings();
        SocketIOClient = NewObject<USocketIOClientComponent>(WorldSettings);
        if (!SocketIOClient)
        {
            UE_LOG(LogTemp, Error, TEXT("%s: Failed to instantiate USocketIOClientComponent"), *FString(__FUNCTION__));
            return;
        }

        SocketIOClient->RegisterComponent();

        // Bind to connect/disconnect events
        SocketIOClient->OnConnected.AddDynamic(this, &USocketIOLobbyManager::OnConnected);
        SocketIOClient->OnDisconnected.AddDynamic(this, &USocketIOLobbyManager::OnDisconnected);

        UE_LOG(LogTemp, Log, TEXT("%s: SocketIOClient component created and delegates bound"), *FString(__FUNCTION__));
    }
    else //if (SocketIOClient->IsConnected())
    {
        UE_LOG(LogTemp, Log, TEXT("%s: Already connected, skipping ConnectWithParams"), *FString(__FUNCTION__));
        return;
    }

    // Build the connection params and kick off the handshake
    FSIOConnectParams Params;
    Params.AddressAndPort = TEXT("http://127.0.0.1:3001");  // <-- point to your Node server
    SocketIOClient->ConnectWithParams(Params);

    UE_LOG(LogTemp, Log, TEXT("%s: Attempting Socket.IO connect to %s"), *FString(__FUNCTION__), *Params.AddressAndPort);
}

/*void USocketIOLobbyManager::BeginPlay()
{
    Super::BeginPlay();

    // Initialize SocketIO component if not set
    if (!SocketIOComponent)
    {
        SocketIOComponent = NewObject<USocketIOClientComponent>(this);
    }

    // Bind events
    SocketIOComponent->OnConnected.AddDynamic(this, &USocketIOLobbyManager::OnConnected);
    SocketIOComponent->OnDisconnected.AddDynamic(this, &USocketIOLobbyManager::OnDisconnected);
    
    // Connect if not auto-connected
    if (!SocketIOComponent->bShouldAutoConnect)
    {
        SocketIOComponent->Connect();
    }
}*/

bool USocketIOLobbyManager::IsConnected() const
{
    if (bIsConnected && !CurrentSocketId.IsEmpty())
        UE_LOG(LogTemp, Warning, TEXT("Connected Successfully!"));
    return bIsConnected && !CurrentSocketId.IsEmpty();
}

void USocketIOLobbyManager::HandleConnected(
    const FString& InSocketId,
    const FString& InSessionId,
    bool bReconnected)
{
    bIsConnected = true;
    UE_LOG(LogTemp, Log, TEXT("Socket.IO Connected: %s"), *InSocketId);
}

void USocketIOLobbyManager::HandleDisconnected(int32 Code)
{
    bIsConnected = false;
    UE_LOG(LogTemp, Warning, TEXT("Socket.IO Disconnected: Code=%d"), Code);
}

void USocketIOLobbyManager::CreateLobby(const FString& LobbyName)
{
    if (!IsConnected()) 
    {
        UE_LOG(LogTemp, Warning, TEXT("Cannot create lobby: not connected"));
        return;
    }

    // Create JSON object and convert to value
    TSharedPtr<FJsonObject> JsonObj = MakeShared<FJsonObject>();
    JsonObj->SetStringField(TEXT("lobbyName"), LobbyName);
    
    // Convert object to value
    TSharedPtr<FJsonValueObject> JsonValue = MakeShared<FJsonValueObject>(JsonObj);
    USIOJsonValue* OutValue = USIOJsonValue::ConstructJsonValue(GetWorld(), JsonValue);

    SocketIOClient->Emit(TEXT("createLobby"), OutValue);
    UE_LOG(LogTemp, Log, TEXT("createLobby('%s')"), *LobbyName);
}

void USocketIOLobbyManager::JoinLobby(const FString& LobbyId)
{
    if (!IsConnected())
    {
        UE_LOG(LogTemp, Warning, TEXT("Cannot join lobby: not connected"));
        return;
    }

    TSharedPtr<FJsonObject> JsonObj = MakeShared<FJsonObject>();
    JsonObj->SetStringField(TEXT("lobbyId"), LobbyId);
    
    TSharedPtr<FJsonValueObject> JsonValue = MakeShared<FJsonValueObject>(JsonObj);
    USIOJsonValue* OutValue = USIOJsonValue::ConstructJsonValue(GetWorld(), JsonValue);

    SocketIOClient->Emit(TEXT("joinLobby"), OutValue);
    UE_LOG(LogTemp, Log, TEXT("joinLobby('%s')"), *LobbyId);
}

void USocketIOLobbyManager::OnConnected(FString SocketId, FString SessionId, bool bIsReconnection)
{
    CurrentSocketId = SocketId;
    CurrentSessionId = SessionId;
    bIsConnected = true;

    UE_LOG(LogTemp, Log, TEXT("SocketIO Connected - ID: %s, Session: %s, Reconnect: %d"), 
        *SocketId, *SessionId, bIsReconnection);

    OnLobbyConnected.Broadcast(SocketId, SessionId);
}

void USocketIOLobbyManager::OnDisconnected(TEnumAsByte<ESIOConnectionCloseReason> Reason)
{
    bIsConnected = false;
    CurrentSocketId.Empty();
    CurrentSessionId.Empty();

    UE_LOG(LogTemp, Log, TEXT("SocketIO Disconnected - Reason: %d"), Reason.GetValue());

    OnLobbyDisconnected.Broadcast(Reason);
}
