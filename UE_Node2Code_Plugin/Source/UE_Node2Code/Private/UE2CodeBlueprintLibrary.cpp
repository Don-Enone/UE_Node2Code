#include "UE2CodeBlueprintLibrary.h"

bool UUE2CodeBlueprintLibrary::ExportMaterialAssetPathToText(const FString& MaterialAssetPath, const FString& OutputFilePath, FUE2CodeExportOptions Options, FString& OutError)
{
	return FUE2CodeMaterialExporter::ExportMaterialAssetPathToText(MaterialAssetPath, OutputFilePath, Options, OutError);
}

bool UUE2CodeBlueprintLibrary::ExportMaterialToText(UMaterialInterface* Material, const FString& OutputFilePath, FUE2CodeExportOptions Options, FString& OutError)
{
	return FUE2CodeMaterialExporter::ExportMaterialToText(Material, OutputFilePath, Options, OutError);
}

bool UUE2CodeBlueprintLibrary::ExportMaterialToString(UMaterialInterface* Material, FUE2CodeExportOptions Options, FString& OutText, FString& OutError)
{
	return FUE2CodeMaterialExporter::ExportMaterialToString(Material, Options, OutText, OutError);
}

bool UUE2CodeBlueprintLibrary::ExportMaterialFunctionAssetPathToText(const FString& MaterialFunctionAssetPath, const FString& OutputFilePath, FUE2CodeExportOptions Options, FString& OutError)
{
	return FUE2CodeMaterialExporter::ExportMaterialFunctionAssetPathToText(MaterialFunctionAssetPath, OutputFilePath, Options, OutError);
}

bool UUE2CodeBlueprintLibrary::ExportMaterialFunctionToText(UMaterialFunctionInterface* MaterialFunction, const FString& OutputFilePath, FUE2CodeExportOptions Options, FString& OutError)
{
	return FUE2CodeMaterialExporter::ExportMaterialFunctionToText(MaterialFunction, OutputFilePath, Options, OutError);
}

bool UUE2CodeBlueprintLibrary::ExportMaterialFunctionToString(UMaterialFunctionInterface* MaterialFunction, FUE2CodeExportOptions Options, FString& OutText, FString& OutError)
{
	return FUE2CodeMaterialExporter::ExportMaterialFunctionToString(MaterialFunction, Options, OutText, OutError);
}

bool UUE2CodeBlueprintLibrary::ExportMaterialPropertyToText(UMaterialInterface* Material, TEnumAsByte<EMaterialProperty> MaterialProperty, const FString& OutputFilePath, FUE2CodeExportOptions Options, FString& OutError)
{
	return FUE2CodeMaterialExporter::ExportMaterialPropertyToText(Material, MaterialProperty.GetValue(), OutputFilePath, Options, OutError);
}

bool UUE2CodeBlueprintLibrary::ExportMaterialNodeByNameToText(UMaterialInterface* Material, const FString& ExpressionObjectName, const FString& OutputFilePath, FUE2CodeExportOptions Options, FString& OutError)
{
	return FUE2CodeMaterialExporter::ExportMaterialNodeByNameToText(Material, ExpressionObjectName, OutputFilePath, Options, OutError);
}

bool UUE2CodeBlueprintLibrary::ExportNiagaraFunctionScriptAssetPathToText(const FString& ScriptAssetPath, const FString& OutputFilePath, FUE2CodeExportOptions Options, FString& OutError)
{
	return FUE2CodeNiagaraExporter::ExportNiagaraFunctionScriptAssetPathToText(ScriptAssetPath, OutputFilePath, Options, OutError);
}

bool UUE2CodeBlueprintLibrary::ExportNiagaraFunctionScriptToText(UNiagaraScript* Script, const FString& OutputFilePath, FUE2CodeExportOptions Options, FString& OutError)
{
	return FUE2CodeNiagaraExporter::ExportNiagaraFunctionScriptToText(Script, OutputFilePath, Options, OutError);
}

bool UUE2CodeBlueprintLibrary::ExportNiagaraFunctionScriptToString(UNiagaraScript* Script, FUE2CodeExportOptions Options, FString& OutText, FString& OutError)
{
	return FUE2CodeNiagaraExporter::ExportNiagaraFunctionScriptToString(Script, Options, OutText, OutError);
}

bool UUE2CodeBlueprintLibrary::ExportNiagaraModuleScriptAssetPathToText(const FString& ScriptAssetPath, const FString& OutputFilePath, FUE2CodeExportOptions Options, FString& OutError)
{
	return FUE2CodeNiagaraExporter::ExportNiagaraModuleScriptAssetPathToText(ScriptAssetPath, OutputFilePath, Options, OutError);
}

bool UUE2CodeBlueprintLibrary::ExportNiagaraModuleScriptToText(UNiagaraScript* Script, const FString& OutputFilePath, FUE2CodeExportOptions Options, FString& OutError)
{
	return FUE2CodeNiagaraExporter::ExportNiagaraModuleScriptToText(Script, OutputFilePath, Options, OutError);
}

bool UUE2CodeBlueprintLibrary::ExportNiagaraModuleScriptToString(UNiagaraScript* Script, FUE2CodeExportOptions Options, FString& OutText, FString& OutError)
{
	return FUE2CodeNiagaraExporter::ExportNiagaraModuleScriptToString(Script, Options, OutText, OutError);
}
