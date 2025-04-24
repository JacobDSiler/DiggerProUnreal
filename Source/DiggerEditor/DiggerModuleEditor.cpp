#include "DiggerModuleEditor.h"
#include "Modules/ModuleManager.h"
#include "EditorModeRegistry.h"
#include "DiggerEdMode.h"

#define LOCTEXT_NAMESPACE "FDiggerEditorModule"

void FDiggerEditorModule::StartupModule()
{
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
