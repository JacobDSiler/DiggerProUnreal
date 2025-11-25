#include "DiggerModuleEditor.h"
#include "Modules/ModuleManager.h"
#include "EditorModeRegistry.h"
#include "DiggerEdMode.h"
#include "AssetToolsModule.h"
#include "DiggerFeatureFlags.h"
#include "IAssetTools.h"
// If you later use UCustomSDFBrushFactory
// #include "UCustomSDFBrushFactory.h"


#define LOCTEXT_NAMESPACE "FDiggerEditorModule"

void FDiggerEditorModule::StartupModule()
{
    // Get the editor.ini flag settings.
    FDiggerFeatureFlags::LoadFlagsFromPluginConfig();
    
    // Register the mode.
    FEditorModeRegistry::Get().RegisterMode<FDiggerEdMode>(
        FDiggerEdMode::EM_DiggerEdModeId,
        LOCTEXT("DiggerEdModeName", "Digger Mode"),
        FSlateIcon(),
        true);
}

void FDiggerEditorModule::ShutdownModule()
{
    FEditorModeRegistry::Get().UnregisterMode(FDiggerEdMode::EM_DiggerEdModeId);
}

#undef LOCTEXT_NAMESPACE

IMPLEMENT_MODULE(FDiggerEditorModule, DiggerEditor)
