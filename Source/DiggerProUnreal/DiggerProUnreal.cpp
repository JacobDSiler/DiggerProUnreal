

#include "DiggerProUnreal.h"
#include "Modules/ModuleManager.h"
#include "Interfaces/IPluginManager.h"
#include "Misc/Paths.h"
#include "ShaderCore.h"

class FDiggerProUnrealModule : public IModuleInterface
{
public:
	virtual void StartupModule() override
	{
		// Map /Digger → <module>/Shaders
		const FString ShaderDir = FPaths::ConvertRelativePathToFull(FPaths::ProjectDir() / TEXT("Source/DiggerProUnreal/Shaders"));
		AddShaderSourceDirectoryMapping(TEXT("/DiggerProUnreal"), ShaderDir);
	}
	virtual void ShutdownModule() override {}
};
IMPLEMENT_MODULE(FDiggerProUnrealModule, DiggerProUnreal)


// TODO: Confirm shader directory mapping works in packaged build
//   -> Test in standalone + packaged game
//   -> Adjust AddShaderSourceDirectoryMapping if needed for plugin vs project
