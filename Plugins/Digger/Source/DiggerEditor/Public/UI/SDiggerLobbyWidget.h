#pragma once

#include "CoreMinimal.h"
#include "Widgets/SCompoundWidget.h"
#include "Widgets/Input/SEditableTextBox.h"
#include "UObject/StrongObjectPtr.h"

// CRITICAL FIX: TStrongObjectPtr requires the full definition, not just a forward declaration.
#if WITH_SOCKETIO
#include "SocketIOLobbyManager.h"
#endif

enum class EDiggerConnectTier : uint8
{
    Free,
    Indie,
    IndieSub,
    Enterprise,
    EnterpriseSub
};

class SDiggerLobbyWidget : public SCompoundWidget
{
public:
    SLATE_BEGIN_ARGS(SDiggerLobbyWidget) {}
    SLATE_END_ARGS()

    void Construct(const FArguments& InArgs);
    virtual ~SDiggerLobbyWidget();

private:
    // --- UI Logic ---
    TSharedRef<SWidget> MakeDiggerConnectLicensingPanel();
    TSharedRef<SWidget> MakeNetworkingWidget();
    TSharedRef<SWidget> MakeNetworkingHelpWidget();

    // --- State Variables ---
    FString LicenseEmail;
    FString LicenseKey;
    FString LoggedInUser;
    
    EDiggerConnectTier CurrentTier = EDiggerConnectTier::Free;
    int32 ConcurrentUsersCap = 2;
    int32 CurrentActiveUsers = 0; 

    TSharedPtr<SEditableTextBox> LobbyNameTextBox;
    TSharedPtr<SEditableTextBox> LobbyIdTextBox;
    TSharedPtr<SEditableTextBox> UsernameTextBox;
    TSharedPtr<SWindow> LoginWindow;

    // --- Managers ---
#if WITH_SOCKETIO
    // Kept alive via StrongPtr so GC doesn't kill it
    TStrongObjectPtr<USocketIOLobbyManager> SocketIOLobbyManager;
#endif

    // --- Logic Functions ---
    void CreateLobbyManager(UWorld* WorldContext);
    void ConnectToLobbyServer();
    void ShutdownNetworking();
    bool IsSocketIOPluginAvailable() const;

    // Licensing
    void LoadLicenseFromConfig();
    void SaveLicenseToConfig() const;
    void ApplyTierCapsFromLicense();
    FText GetTierDisplayText() const;
    FText GetConcurrentText() const;
    bool IsUpgradeVisible(EDiggerConnectTier TargetTier) const;

    // Callbacks
    FReply OnValidateLicenseClicked();
    FReply OnUpgradeTierClicked(EDiggerConnectTier TargetTier);
    FReply ShowLoginModal();
    FReply OnConnectClicked();
    FReply OnCreateLobbyClicked();
    FReply OnJoinLobbyClicked();

    bool IsConnectButtonEnabled() const;
    bool IsCreateLobbyEnabled() const;
    bool IsJoinLobbyEnabled() const;
};