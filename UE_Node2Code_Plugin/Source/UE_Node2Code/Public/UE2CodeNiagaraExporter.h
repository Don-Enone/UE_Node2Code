#pragma once

#include "CoreMinimal.h"
#include "UE2CodeMaterialExporter.h"

class UNiagaraScript;

class UE_NODE2CODE_API FUE2CodeNiagaraExporter
{
public:
	static bool ExportNiagaraFunctionScriptAssetPathToText(const FString& ScriptAssetPath, const FString& OutputFilePath, const FUE2CodeExportOptions& Options, FString& OutError);
	static bool ExportNiagaraFunctionScriptToText(UNiagaraScript* Script, const FString& OutputFilePath, const FUE2CodeExportOptions& Options, FString& OutError);
	static bool ExportNiagaraFunctionScriptToString(UNiagaraScript* Script, const FUE2CodeExportOptions& Options, FString& OutText, FString& OutError);

	static bool ExportNiagaraModuleScriptAssetPathToText(const FString& ScriptAssetPath, const FString& OutputFilePath, const FUE2CodeExportOptions& Options, FString& OutError);
	static bool ExportNiagaraModuleScriptToText(UNiagaraScript* Script, const FString& OutputFilePath, const FUE2CodeExportOptions& Options, FString& OutError);
	static bool ExportNiagaraModuleScriptToString(UNiagaraScript* Script, const FUE2CodeExportOptions& Options, FString& OutText, FString& OutError);
};
