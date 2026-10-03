#pragma once

#include "CoreMinimal.h"
#include "SceneTypes.h"
#include "UE2CodeMaterialExporter.generated.h"

class UMaterialExpression;
class UMaterialFunctionInterface;
class UMaterialInterface;

USTRUCT(BlueprintType)
struct UE_NODE2CODE_API FUE2CodeExportOptions
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "UE Node2Code", meta = (DisplayName = "Expand Material Functions", ToolTip = "Legacy Material-only switch. NodeHierarchyDepth is the common hierarchy control for all graph types."))
	bool bExpandMaterialFunctions = true;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "UE Node2Code")
	bool bExportUnreferencedMaterialExpressions = false;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "UE Node2Code", meta = (ClampMin = "0", DisplayName = "Node Hierarchy Depth", ToolTip = "Applies to every supported graph. 0 recursively expands called graphs, 1 keeps only the root graph, and N expands through hierarchy layer N."))
	int32 NodeHierarchyDepth = 0;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "UE Node2Code", meta = (ClampMin = "1", DisplayName = "Maximum Hierarchy Depth Safety Limit", ToolTip = "Safety limit used by recursive hierarchy expansion when NodeHierarchyDepth is 0."))
	int32 MaxFunctionDepth = 32;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "UE Node2Code")
	bool bIncludeDebugMetadata = false;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "UE Node2Code")
	bool bIncludeDefaultLikeProperties = false;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "UE Node2Code", meta = (DisplayName = "Selected Node Ids", ToolTip = "When not empty, Blueprint, Material, Material Function and Niagara Script exports keep only these root-graph nodes. Each entry matches a node GUID, object name or object path. Graphs called by selected nodes still follow NodeHierarchyDepth."))
	TArray<FString> SelectedNodeIds;
};

class UE_NODE2CODE_API FUE2CodeMaterialExporter
{
public:
	static bool ExportMaterialAssetPathToText(const FString& MaterialAssetPath, const FString& OutputFilePath, const FUE2CodeExportOptions& Options, FString& OutError);
	static bool ExportMaterialToText(UMaterialInterface* Material, const FString& OutputFilePath, const FUE2CodeExportOptions& Options, FString& OutError);
	static bool ExportMaterialToString(UMaterialInterface* Material, const FUE2CodeExportOptions& Options, FString& OutText, FString& OutError);

	static bool ExportMaterialFunctionAssetPathToText(const FString& MaterialFunctionAssetPath, const FString& OutputFilePath, const FUE2CodeExportOptions& Options, FString& OutError);
	static bool ExportMaterialFunctionToText(UMaterialFunctionInterface* MaterialFunction, const FString& OutputFilePath, const FUE2CodeExportOptions& Options, FString& OutError);
	static bool ExportMaterialFunctionToString(UMaterialFunctionInterface* MaterialFunction, const FUE2CodeExportOptions& Options, FString& OutText, FString& OutError);

	static bool ExportMaterialPropertyToText(UMaterialInterface* Material, EMaterialProperty MaterialProperty, const FString& OutputFilePath, const FUE2CodeExportOptions& Options, FString& OutError);
	static bool ExportMaterialPropertyToString(UMaterialInterface* Material, EMaterialProperty MaterialProperty, const FUE2CodeExportOptions& Options, FString& OutText, FString& OutError);

	static bool ExportMaterialNodeByNameToText(UMaterialInterface* Material, const FString& ExpressionObjectName, const FString& OutputFilePath, const FUE2CodeExportOptions& Options, FString& OutError);
	static bool ExportMaterialNodeByNameToString(UMaterialInterface* Material, const FString& ExpressionObjectName, const FUE2CodeExportOptions& Options, FString& OutText, FString& OutError);

	static bool ExportMaterialExpressionToString(UMaterialExpression* RootExpression, const FUE2CodeExportOptions& Options, FString& OutText, FString& OutError);

	static bool ParseMaterialProperty(const FString& PropertyName, EMaterialProperty& OutProperty);
	static FString MaterialPropertyToString(EMaterialProperty Property);
	static FString NormalizeMaterialAssetPath(const FString& InputPath);
};
