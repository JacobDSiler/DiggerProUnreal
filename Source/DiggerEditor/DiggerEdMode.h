#pragma once

#include "CoreMinimal.h"
#include "EdMode.h"

class ADiggerManager;

class FDiggerEdMode : public FEdMode
{
public:
	const static FEditorModeID EM_DiggerEdModeId;

	FDiggerEdMode();  // ✅ Add this
	virtual ~FDiggerEdMode(); // ✅ And this

	virtual void Enter() override;
	virtual void Exit() override;
	virtual void Tick(FEditorViewportClient* ViewportClient, float DeltaTime) override;
	virtual void AddReferencedObjects(FReferenceCollector& Collector) override;
	virtual bool UsesToolkits() const override;
	// Your custom stuff
	ADiggerManager* FindDiggerManager(); // Add this for Step 2
};
