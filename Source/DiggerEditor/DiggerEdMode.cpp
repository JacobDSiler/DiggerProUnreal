#include "DiggerEdMode.h"
#include "DiggerEdModeToolkit.h"
#include "EditorModeManager.h"           // For FEditorModeTools
#include "Toolkits/ToolkitManager.h"     // For GetToolkitHost()
#include "C:\Users\serpe\Documents\Unreal Projects\DiggerProUnreal\Source\DiggerProUnreal\Public\DiggerManager.h" // ✅ Needed for ADiggerManager

class FEditorViewportClient;
class UWorld;
// Define the Editor Mode ID
const FEditorModeID FDiggerEdMode::EM_DiggerEdModeId = TEXT("EM_DiggerEdMode");

FDiggerEdMode::FDiggerEdMode() {}
FDiggerEdMode::~FDiggerEdMode() {}

void FDiggerEdMode::Enter()
{
	FEdMode::Enter();

	if (!Toolkit.IsValid())
	{
		Toolkit = MakeShareable(new FDiggerEdModeToolkit);
		Toolkit->Init(Owner->GetToolkitHost());
	}
}

void FDiggerEdMode::Exit()
{
	if (Toolkit.IsValid())
	{
		FToolkitManager::Get().CloseToolkit(Toolkit.ToSharedRef());
		Toolkit.Reset();
	}

	FEdMode::Exit();
}

void FDiggerEdMode::AddReferencedObjects(FReferenceCollector& Collector)
{
	FEdMode::AddReferencedObjects(Collector);
	// Add UObject references here if you start using them
}

void FDiggerEdMode::Tick(FEditorViewportClient* ViewportClient, float DeltaTime)
{
	FEdMode::Tick(ViewportClient, DeltaTime);
	// Add any tool update logic here
}

bool FDiggerEdMode::UsesToolkits() const
{
	return true;
}

ADiggerManager* FDiggerEdMode::FindDiggerManager()
{
	UWorld* World = GEditor->GetEditorWorldContext().World();
	for (TActorIterator<ADiggerManager> It(World); It; ++It)
	{
		return *It;
	}
	return nullptr;
}
