// DiggerEdModeToolkit.cpp
#include "DiggerEdModeToolkit.h"
#include "DiggerEdMode.h"
#include "C:\Users\serpe\Documents\Unreal Projects\DiggerProUnreal\Source\DiggerProUnreal\Public\DiggerManager.h" // ✅ Needed for ADiggerManager
#include "EditorModeManager.h"
#include "Widgets/Text/STextBlock.h"
#include "Widgets/Input/SButton.h"

#define LOCTEXT_NAMESPACE "FDiggerEdModeToolkit"

void FDiggerEdModeToolkit::Init(const TSharedPtr<IToolkitHost>& InitToolkitHost)
{
	ToolkitWidget = SNew(SVerticalBox)
	+ SVerticalBox::Slot().AutoHeight().Padding(5)
	[
		SNew(STextBlock).Text(LOCTEXT("DiggerLabel", "Digger Tool Active!"))
	]
	+ SVerticalBox::Slot().AutoHeight().Padding(5)
	[
		SNew(SButton)
		.Text(LOCTEXT("TestBrush", "Dig Hole"))
		.OnClicked_Lambda([]() -> FReply
		{
			// 🔍 Try to get the current digger mode
			if (FDiggerEdMode* Mode = (FDiggerEdMode*)GLevelEditorModeTools().GetActiveMode(FDiggerEdMode::EM_DiggerEdModeId))
			{
				// 🔧 Find the digger manager in the world
				if (ADiggerManager* Digger = Mode->FindDiggerManager())
				{
					UE_LOG(LogTemp, Warning, TEXT("Calling CreateSingleHole() on DiggerManager"));
					Digger->ApplyBrushInEditor();//(FVector(0, 0, 0)); // You can later update this to use the mouse location or brush config
				}
				else
				{
					UE_LOG(LogTemp, Warning, TEXT("No DiggerManager found!"));
				}
			}
			else
			{
				UE_LOG(LogTemp, Warning, TEXT("FDiggerEdMode not active!"));
			}
			return FReply::Handled();
		})
	];

	FModeToolkit::Init(InitToolkitHost);
}

FName FDiggerEdModeToolkit::GetToolkitFName() const { return FName("DiggerEdMode"); }
FText FDiggerEdModeToolkit::GetBaseToolkitName() const { return LOCTEXT("ToolkitName", "Digger Toolkit"); }
FEdMode* FDiggerEdModeToolkit::GetEditorMode() const { return GLevelEditorModeTools().GetActiveMode(FDiggerEdMode::EM_DiggerEdModeId); }
TSharedPtr<SWidget> FDiggerEdModeToolkit::GetInlineContent() const { return ToolkitWidget; }

#undef LOCTEXT_NAMESPACE
