#pragma once

#include "CoreMinimal.h"
#include "Engine/Blueprint.h"
#include "UE2CodeBlueprintExporter.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "Materials/MaterialFunctionInterface.h"
#include "Materials/MaterialInterface.h"
#include "NiagaraScript.h"
#include "UE2CodeMaterialExporter.h"
#include "UE2CodeNiagaraExporter.h"
#include "UE2CodeBlueprintLibrary.generated.h"

UCLASS()
class UE_NODE2CODE_API UUE2CodeBlueprintLibrary : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()

public:
	UFUNCTION(BlueprintCallable, Category = "UE Node2Code|Blueprint")
	static bool ExportBlueprintAssetPathToText(const FString& BlueprintAssetPath, const FString& OutputFilePath, FUE2CodeExportOptions Options, FString& OutError);

	UFUNCTION(BlueprintCallable, Category = "UE Node2Code|Blueprint")
	static bool ExportBlueprintToText(UBlueprint* Blueprint, const FString& OutputFilePath, FUE2CodeExportOptions Options, FString& OutError);

	UFUNCTION(BlueprintCallable, Category = "UE Node2Code|Blueprint")
	static bool ExportBlueprintToString(UBlueprint* Blueprint, FUE2CodeExportOptions Options, FString& OutText, FString& OutError);

	UFUNCTION(BlueprintCallable, Category = "UE Node2Code|Material")
	static bool ExportMaterialAssetPathToText(const FString& MaterialAssetPath, const FString& OutputFilePath, FUE2CodeExportOptions Options, FString& OutError);

	UFUNCTION(BlueprintCallable, Category = "UE Node2Code|Material")
	static bool ExportMaterialToText(UMaterialInterface* Material, const FString& OutputFilePath, FUE2CodeExportOptions Options, FString& OutError);

	UFUNCTION(BlueprintCallable, Category = "UE Node2Code|Material")
	static bool ExportMaterialToString(UMaterialInterface* Material, FUE2CodeExportOptions Options, FString& OutText, FString& OutError);

	UFUNCTION(BlueprintCallable, Category = "UE Node2Code|Material Function")
	static bool ExportMaterialFunctionAssetPathToText(const FString& MaterialFunctionAssetPath, const FString& OutputFilePath, FUE2CodeExportOptions Options, FString& OutError);

	UFUNCTION(BlueprintCallable, Category = "UE Node2Code|Material Function")
	static bool ExportMaterialFunctionToText(UMaterialFunctionInterface* MaterialFunction, const FString& OutputFilePath, FUE2CodeExportOptions Options, FString& OutError);

	UFUNCTION(BlueprintCallable, Category = "UE Node2Code|Material Function")
	static bool ExportMaterialFunctionToString(UMaterialFunctionInterface* MaterialFunction, FUE2CodeExportOptions Options, FString& OutText, FString& OutError);

	UFUNCTION(BlueprintCallable, Category = "UE Node2Code|Material")
	static bool ExportMaterialPropertyToText(UMaterialInterface* Material, TEnumAsByte<EMaterialProperty> MaterialProperty, const FString& OutputFilePath, FUE2CodeExportOptions Options, FString& OutError);

	UFUNCTION(BlueprintCallable, Category = "UE Node2Code|Material")
	static bool ExportMaterialNodeByNameToText(UMaterialInterface* Material, const FString& ExpressionObjectName, const FString& OutputFilePath, FUE2CodeExportOptions Options, FString& OutError);

	UFUNCTION(BlueprintCallable, Category = "UE Node2Code|Niagara Function Script")
	static bool ExportNiagaraFunctionScriptAssetPathToText(const FString& ScriptAssetPath, const FString& OutputFilePath, FUE2CodeExportOptions Options, FString& OutError);

	UFUNCTION(BlueprintCallable, Category = "UE Node2Code|Niagara Function Script")
	static bool ExportNiagaraFunctionScriptToText(UNiagaraScript* Script, const FString& OutputFilePath, FUE2CodeExportOptions Options, FString& OutError);

	UFUNCTION(BlueprintCallable, Category = "UE Node2Code|Niagara Function Script")
	static bool ExportNiagaraFunctionScriptToString(UNiagaraScript* Script, FUE2CodeExportOptions Options, FString& OutText, FString& OutError);

	UFUNCTION(BlueprintCallable, Category = "UE Node2Code|Niagara Module Script")
	static bool ExportNiagaraModuleScriptAssetPathToText(const FString& ScriptAssetPath, const FString& OutputFilePath, FUE2CodeExportOptions Options, FString& OutError);

	UFUNCTION(BlueprintCallable, Category = "UE Node2Code|Niagara Module Script")
	static bool ExportNiagaraModuleScriptToText(UNiagaraScript* Script, const FString& OutputFilePath, FUE2CodeExportOptions Options, FString& OutError);

	UFUNCTION(BlueprintCallable, Category = "UE Node2Code|Niagara Module Script")
	static bool ExportNiagaraModuleScriptToString(UNiagaraScript* Script, FUE2CodeExportOptions Options, FString& OutText, FString& OutError);
};
