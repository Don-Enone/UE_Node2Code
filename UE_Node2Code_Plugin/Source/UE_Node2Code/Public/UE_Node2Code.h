#pragma once

#include "CoreMinimal.h"
#include "Modules/ModuleManager.h"

class IConsoleObject;
class SDockTab;
class FSpawnTabArgs;
struct FToolMenuContext;

class FUE2CodeModule : public IModuleInterface
{
public:
	virtual void StartupModule() override;
	virtual void ShutdownModule() override;

private:
	void RegisterConsoleCommands();
	void UnregisterConsoleCommands();
	void RegisterGui();
	void UnregisterGui();
	void RegisterMenus();
	void OpenExportWindow(const FToolMenuContext& MenuContext);
	TSharedRef<SDockTab> SpawnExportTab(const FSpawnTabArgs& SpawnTabArgs);

	void ExportMaterialCommand(const TArray<FString>& Args);
	void ExportBlueprintCommand(const TArray<FString>& Args);
	void ExportMaterialFunctionCommand(const TArray<FString>& Args);
	void ExportNiagaraFunctionScriptCommand(const TArray<FString>& Args);
	void ExportNiagaraModuleScriptCommand(const TArray<FString>& Args);
	void ExportMaterialPropertyCommand(const TArray<FString>& Args);
	void ExportMaterialNodeCommand(const TArray<FString>& Args);

	TArray<IConsoleObject*> ConsoleCommands;
};
