#pragma once

#include "CoreMinimal.h"
#include "UE2CodeMaterialExporter.h"

class UBlueprint;

class UE_NODE2CODE_API FUE2CodeBlueprintExporter
{
public:
	static bool ExportBlueprintAssetPathToText(const FString& BlueprintAssetPath, const FString& OutputFilePath, const FUE2CodeExportOptions& Options, FString& OutError);
	static bool ExportBlueprintToText(UBlueprint* Blueprint, const FString& OutputFilePath, const FUE2CodeExportOptions& Options, FString& OutError);
	static bool ExportBlueprintToString(UBlueprint* Blueprint, const FUE2CodeExportOptions& Options, FString& OutText, FString& OutError);
};
