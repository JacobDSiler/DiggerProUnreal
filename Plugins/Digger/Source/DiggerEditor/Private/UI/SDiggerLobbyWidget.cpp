#include "SDiggerLobbyWidget.h"
#include "DiggerFeatureFlags.h"
#include "Misc/ConfigCacheIni.h"
#include "Widgets/Layout/SExpandableArea.h"
#include "Widgets/Layout/SScrollBox.h"
#include "Widgets/Layout/SWrapBox.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Text/STextBlock.h"
#include "Widgets/Notifications/SProgressBar.h"
#include "Framework/Application/SlateApplication.h"
#include "Interfaces/IPluginManager.h"
#include "Editor.h"

// Conditionally include SocketIO if available in build
#if WITH_SOCKETIO
#include "SocketIOLobbyManager.h"
#endif

#define LOCTEXT_NAMESPACE "SDiggerLobbyWidget"

void SDiggerLobbyWidget::Construct(const FArguments& InArgs)
{
    // Initialize Manager
#if WITH_SOCKETIO
    SocketIOLobbyManager.Reset(NewObject<USocketIOLobbyManager>(GetTransientPackage()));
#endif

    const bool bHasPlugin = IsSocketIOPluginAvailable();

    ChildSlot
    [
        SNew(SVerticalBox)
        + SVerticalBox::Slot().AutoHeight().Padding(8, 12, 8, 4)
        [
            MakeDiggerConnectLicensingPanel()
        ]

        // 1) Google/Username login
        + SVerticalBox::Slot().AutoHeight().Padding(8)
        [
            SNew(SButton)
            .IsEnabled(bHasPlugin)
            .OnClicked_Lambda([this, bHasPlugin]() {
                return bHasPlugin ? ShowLoginModal() : FReply::Handled();
            })
            [
                SNew(STextBlock)
                .Text(FText::FromString("Sign in with Google"))
            ]
        ]

        // 2) Connect button
#if WITH_SOCKETIO
        + SVerticalBox::Slot().AutoHeight().Padding(8)
        [
            SNew(SButton)
            .IsEnabled(bHasPlugin)
            .OnClicked_Lambda([this]() -> FReply
            {
                ConnectToLobbyServer();
                return FReply::Handled();
            })
            [
                SNew(STextBlock)
                .Text(FText::FromString("Connect to Lobby Server"))
            ]
        ]
#endif

        // 3) Networking panel
        + SVerticalBox::Slot().AutoHeight().Padding(8)
        [
#if WITH_SOCKETIO
            bHasPlugin
                ? MakeNetworkingWidget()
                : MakeNetworkingHelpWidget()
#else
            MakeNetworkingHelpWidget()
#endif
        ]
    ];
}

SDiggerLobbyWidget::~SDiggerLobbyWidget()
{
    ShutdownNetworking();
}

bool SDiggerLobbyWidget::IsSocketIOPluginAvailable() const
{
    return IPluginManager::Get().FindPlugin("SocketIOClient").IsValid();
}

void SDiggerLobbyWidget::ShutdownNetworking()
{
#if WITH_SOCKETIO
    // Logic to close socket connection if active
    if(SocketIOLobbyManager.IsValid())
    {
        // Add disconnect logic here if exposed in USocketIOLobbyManager
    }
#endif
}

void SDiggerLobbyWidget::ConnectToLobbyServer()
{
#if WITH_SOCKETIO
    UWorld* World = GEditor->GetEditorWorldContext().World();
    if (!World) return;

    if (!SocketIOLobbyManager.IsValid())
    {
        SocketIOLobbyManager.Reset(NewObject<USocketIOLobbyManager>(World));
    }

    if (SocketIOLobbyManager.IsValid())
    {
        SocketIOLobbyManager->Initialize(World);
    }
#endif
}

// ... [Copy-Paste remaining Logic functions like OnValidateLicenseClicked, MakeDiggerConnectLicensingPanel from original Toolkit here] ...
// IMPORTANT: Replace 'FDiggerEdModeToolkit::' with 'SDiggerLobbyWidget::'
// I will include one as example, assume the rest are copied exactly to save space in this response.

void SDiggerLobbyWidget::LoadLicenseFromConfig()
{
    GConfig->GetString(TEXT("/Script/Digger.DiggerConnect"), TEXT("LicenseEmail"), LicenseEmail, GGameIni);
    GConfig->GetString(TEXT("/Script/Digger.DiggerConnect"), TEXT("LicenseKey"),   LicenseKey,   GGameIni);
    ApplyTierCapsFromLicense();
}

void SDiggerLobbyWidget::SaveLicenseToConfig() const
{
    GConfig->SetString(TEXT("/Script/Digger.DiggerConnect"), TEXT("LicenseEmail"), *LicenseEmail, GGameIni);
    GConfig->SetString(TEXT("/Script/Digger.DiggerConnect"), TEXT("LicenseKey"),   *LicenseKey,   GGameIni);
    GConfig->Flush(false, GGameIni);
}

void SDiggerLobbyWidget::ApplyTierCapsFromLicense()
{
    if (LicenseKey.IsEmpty())
    {
        CurrentTier = EDiggerConnectTier::Free;
        ConcurrentUsersCap = 2;
        return;
    }
    if (LicenseKey.StartsWith(TEXT("INDI-PERP-"))) { CurrentTier = EDiggerConnectTier::Indie; ConcurrentUsersCap = 5; }
    else if (LicenseKey.StartsWith(TEXT("ENTR-PERP-"))) { CurrentTier = EDiggerConnectTier::Enterprise; ConcurrentUsersCap = 9999; }
    else if (LicenseKey.StartsWith(TEXT("INDI-SUB-"))) { CurrentTier = EDiggerConnectTier::IndieSub; ConcurrentUsersCap = 5; }
    else if (LicenseKey.StartsWith(TEXT("ENTR-SUB-"))) { CurrentTier = EDiggerConnectTier::EnterpriseSub; ConcurrentUsersCap = 9999; }
    else { CurrentTier = EDiggerConnectTier::Free; ConcurrentUsersCap = 2; }
}

FReply SDiggerLobbyWidget::OnValidateLicenseClicked()
{
    SaveLicenseToConfig();
    ApplyTierCapsFromLicense();
    return FReply::Handled();
}

FText SDiggerLobbyWidget::GetTierDisplayText() const
{
    switch (CurrentTier)
    {
        case EDiggerConnectTier::Free:            return FText::FromString(TEXT("Free (Non-Commercial)"));
        case EDiggerConnectTier::Indie:           return FText::FromString(TEXT("Indie (Perpetual)"));
        case EDiggerConnectTier::Enterprise:      return FText::FromString(TEXT("Enterprise (Perpetual)"));
        case EDiggerConnectTier::IndieSub:        return FText::FromString(TEXT("Indie (Subscription)"));
        case EDiggerConnectTier::EnterpriseSub:   return FText::FromString(TEXT("Enterprise (Subscription)"));
        default:                                   return FText::FromString(TEXT("Unknown"));
    }
}

FText SDiggerLobbyWidget::GetConcurrentText() const
{
    const FString Cap = (ConcurrentUsersCap >= 9999) ? TEXT("Unlimited") : FString::FromInt(ConcurrentUsersCap);
    return FText::FromString(FString::Printf(TEXT("Concurrent Users: %d / %s"), CurrentActiveUsers, *Cap));
}

bool SDiggerLobbyWidget::IsUpgradeVisible(EDiggerConnectTier TargetTier) const
{
    auto Rank = [](EDiggerConnectTier T)->int32 {
        switch (T) {
            case EDiggerConnectTier::Free:          return 0;
            case EDiggerConnectTier::IndieSub:      return 1;
            case EDiggerConnectTier::Indie:         return 2;
            case EDiggerConnectTier::EnterpriseSub: return 3;
            case EDiggerConnectTier::Enterprise:    return 4;
            default:                                return 0;
        }
    };
    return Rank(TargetTier) > Rank(CurrentTier);
}

FReply SDiggerLobbyWidget::OnUpgradeTierClicked(EDiggerConnectTier TargetTier)
{
    return FReply::Handled();
}

TSharedRef<SWidget> SDiggerLobbyWidget::MakeDiggerConnectLicensingPanel()
{
    LoadLicenseFromConfig();

    return SNew(SBorder)
        .Padding(8)
        .BorderImage(FAppStyle::Get().GetBrush("ToolPanel.GroupBorder"))
        [
            SNew(SVerticalBox)
            + SVerticalBox::Slot().AutoHeight().Padding(0,0,0,6)
            [
                SNew(STextBlock).Text(FText::FromString(TEXT("DiggerConnect Licensing"))).Font(FCoreStyle::GetDefaultFontStyle("Bold", 12))
            ]
            + SVerticalBox::Slot().AutoHeight().Padding(0,0,0,4)
            [
                SNew(STextBlock).Text(this, &SDiggerLobbyWidget::GetTierDisplayText)
            ]
            + SVerticalBox::Slot().AutoHeight().Padding(0,0,0,8)
            [
                SNew(STextBlock).Text(this, &SDiggerLobbyWidget::GetConcurrentText).ColorAndOpacity(FSlateColor(FLinearColor(0.8f,0.8f,0.8f)))
            ]
            // License email/key entry
            + SVerticalBox::Slot().AutoHeight().Padding(0,0,0,4)
            [
                SNew(SHorizontalBox)
                + SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(0,0,8,0)
                [
                    SNew(STextBlock).Text(FText::FromString(TEXT("Email")))
                ]
                + SHorizontalBox::Slot().FillWidth(1.f)
                [
                    SNew(SEditableTextBox)
                    .Text_Lambda([this]{ return FText::FromString(LicenseEmail); })
                    .OnTextCommitted_Lambda([this](const FText& T, ETextCommit::Type){ LicenseEmail = T.ToString(); })
                ]
            ]
            + SVerticalBox::Slot().AutoHeight().Padding(0,0,0,8)
            [
                SNew(SHorizontalBox)
                + SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(0,0,8,0)
                [
                    SNew(STextBlock).Text(FText::FromString(TEXT("License Key")))
                ]
                + SHorizontalBox::Slot().FillWidth(1.f)
                [
                    SNew(SEditableTextBox)
                    .IsPassword(true)
                    .Text_Lambda([this]{ return FText::FromString(LicenseKey); })
                    .OnTextCommitted_Lambda([this](const FText& T, ETextCommit::Type){ LicenseKey = T.ToString(); })
                ]
            ]
            + SVerticalBox::Slot().AutoHeight().Padding(0,0,0,10)
            [
                SNew(SButton).Text(FText::FromString(TEXT("Validate / Activate"))).HAlign(HAlign_Center).OnClicked(this, &SDiggerLobbyWidget::OnValidateLicenseClicked)
            ]
            // Upgrade buttons
            + SVerticalBox::Slot().AutoHeight()
            [
                SNew(SWrapBox).UseAllottedWidth(true)
                + SWrapBox::Slot().Padding(0,4)[ SNew(SBox).Visibility_Lambda([this]{ return IsUpgradeVisible(EDiggerConnectTier::Indie) ? EVisibility::Visible : EVisibility::Collapsed; })[ SNew(SButton).Text(FText::FromString(TEXT("Upgrade: Indie"))).OnClicked(this, &SDiggerLobbyWidget::OnUpgradeTierClicked, EDiggerConnectTier::Indie) ] ]
                // ... (Add other buttons similarly)
            ]
        ];
}

FReply SDiggerLobbyWidget::ShowLoginModal()
{
    if (LoginWindow.IsValid()) return FReply::Handled();

    UsernameTextBox = SNew(SEditableTextBox).HintText(LOCTEXT("UsernameHint", "Enter username…"));

    LoginWindow = SNew(SWindow)
        .Title(LOCTEXT("LoginWindowTitle", "Login"))
        .ClientSize(FVector2D(300, 100))
        .Content()
        [
            SNew(SBorder).Padding(8)
            [
                SNew(SVerticalBox)
                + SVerticalBox::Slot().AutoHeight()[ SNew(STextBlock).Text(LOCTEXT("LoginPrompt", "Please choose a username:")) ]
                + SVerticalBox::Slot().AutoHeight()[ UsernameTextBox.ToSharedRef() ]
                + SVerticalBox::Slot().AutoHeight().HAlign(HAlign_Right)[ SNew(SButton).Text(LOCTEXT("LoginOk", "OK")).OnClicked(this, &SDiggerLobbyWidget::OnConnectClicked) ]
            ]
        ];

    FSlateApplication::Get().AddWindow(LoginWindow.ToSharedRef());
    return FReply::Handled();
}

FReply SDiggerLobbyWidget::OnConnectClicked() { ConnectToLobbyServer(); return FReply::Handled(); }
bool SDiggerLobbyWidget::IsConnectButtonEnabled() const { return !LoggedInUser.IsEmpty(); }

#if WITH_SOCKETIO
TSharedRef<SWidget> SDiggerLobbyWidget::MakeNetworkingWidget()
{
    return SNew(SExpandableArea)
        .AreaTitle(FText::FromString("Lobby Setup"))
        .InitiallyCollapsed(true)
        .BodyContent()
        [
            SNew(SVerticalBox)
            + SVerticalBox::Slot().AutoHeight().Padding(4)
            [
                SNew(SHorizontalBox)
                + SHorizontalBox::Slot().AutoWidth()[ SNew(STextBlock).Text(FText::FromString("New Lobby Name:")) ]
                + SHorizontalBox::Slot().FillWidth(1.0f)[ SAssignNew(LobbyNameTextBox, SEditableTextBox) ]
                + SHorizontalBox::Slot().AutoWidth()[ SNew(SButton).Text(FText::FromString("Create")).OnClicked(this, &SDiggerLobbyWidget::OnCreateLobbyClicked).IsEnabled(this, &SDiggerLobbyWidget::IsCreateLobbyEnabled) ]
            ]
            + SVerticalBox::Slot().AutoHeight().Padding(4)
            [
                SNew(SHorizontalBox)
                + SHorizontalBox::Slot().AutoWidth()[ SNew(STextBlock).Text(FText::FromString("Join Lobby ID:")) ]
                + SHorizontalBox::Slot().FillWidth(1.0f)[ SAssignNew(LobbyIdTextBox, SEditableTextBox) ]
                + SHorizontalBox::Slot().AutoWidth()[ SNew(SButton).Text(FText::FromString("Join")).OnClicked(this, &SDiggerLobbyWidget::OnJoinLobbyClicked).IsEnabled(this, &SDiggerLobbyWidget::IsJoinLobbyEnabled) ]
            ]
        ];
}

FReply SDiggerLobbyWidget::OnCreateLobbyClicked()
{
    if (LobbyNameTextBox.IsValid() && SocketIOLobbyManager.IsValid())
    {
        SocketIOLobbyManager->CreateLobby(LobbyNameTextBox->GetText().ToString());
    }
    return FReply::Handled();
}

FReply SDiggerLobbyWidget::OnJoinLobbyClicked()
{
    if (LobbyIdTextBox.IsValid() && SocketIOLobbyManager.IsValid())
    {
        SocketIOLobbyManager->JoinLobby(LobbyIdTextBox->GetText().ToString());
    }
    return FReply::Handled();
}

bool SDiggerLobbyWidget::IsCreateLobbyEnabled() const { return SocketIOLobbyManager.IsValid() && SocketIOLobbyManager->IsConnected() && LobbyNameTextBox.IsValid() && !LobbyNameTextBox->GetText().IsEmpty(); }
bool SDiggerLobbyWidget::IsJoinLobbyEnabled() const { return SocketIOLobbyManager.IsValid() && SocketIOLobbyManager->IsConnected() && LobbyIdTextBox.IsValid() && !LobbyIdTextBox->GetText().IsEmpty(); }
#endif

TSharedRef<SWidget> SDiggerLobbyWidget::MakeNetworkingHelpWidget()
{
    return SNew(SVerticalBox)
        + SVerticalBox::Slot().AutoHeight().Padding(4)[ SNew(STextBlock).Text(FText::FromString("Multiplayer features require the SocketIO plugin.")) ];
}

#undef LOCTEXT_NAMESPACE