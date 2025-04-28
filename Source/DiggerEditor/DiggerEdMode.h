#pragma once

#include "CoreMinimal.h"
#include "DiggerEdModeToolkit.h"
#include "EditorModeManager.h"
#include "DiggerEdModeToolkit.h"
#include "EdMode.h"

class FDiggerEdModeToolkit;
class ADiggerManager;

class FDiggerEdMode : public FEdMode
{
public:
	const static FEditorModeID EM_DiggerEdModeId;

	FDiggerEdMode();  // ✅ Add this
	virtual ~FDiggerEdMode(); // ✅ And this

	virtual void Enter() override;
	bool GetMouseWorldHit(FEditorViewportClient* ViewportClient, FVector& OutHitLocation, FHitResult& OutHit);
	virtual void Render(const FSceneView* View, FViewport* Viewport, FPrimitiveDrawInterface* PDI) override;
	bool HandleClick(FEditorViewportClient* InViewportClient, HHitProxy* HitProxy, const FViewportClick& Click);
	virtual void Exit() override;
	virtual void Tick(FEditorViewportClient* ViewportClient, float DeltaTime) override;
	virtual void AddReferencedObjects(FReferenceCollector& Collector) override;
	virtual bool UsesToolkits() const override;
	// Your custom stuff
	ADiggerManager* FindDiggerManager(); // Add this for Step 2

	
	// Helper to get your custom toolkit
	TSharedPtr<FDiggerEdModeToolkit> GetDiggerToolkit()
	{
		return StaticCastSharedPtr<FDiggerEdModeToolkit>(GetToolkit());
	}

};
