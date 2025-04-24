// DiggerEdModeToolkit.h
#pragma once

#include "EditorModeManager.h"
#include "Toolkits/BaseToolkit.h"

class FDiggerEdModeToolkit : public FModeToolkit
{
public:
	virtual void Init(const TSharedPtr<IToolkitHost>& InitToolkitHost) override;
	virtual FName GetToolkitFName() const override;
	virtual FText GetBaseToolkitName() const override;
	virtual class FEdMode* GetEditorMode() const override;
	virtual TSharedPtr<SWidget> GetInlineContent() const override;

private:
	TSharedPtr<SVerticalBox> ToolkitWidget;
};
