#pragma once

#include "CoreMinimal.h"
#include "SceneTypes.h"
#include "UE2CodeMaterialExporter.generated.h"

class UMaterialExpression;
class UMaterialInterface;

USTRUCT(BlueprintType)
struct UE_NODE2CODE_API FUE2CodeExportOptions
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "UE Node2Code")
	bool bExpandMaterialFunctions = true;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "UE Node2Code")
	bool bExportUnreferencedMaterialExpressions = true;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "UE Node2Code", meta = (ClampMin = "0"))
	int32 NodeHierarchyDepth = 0;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "UE Node2Code", meta = (ClampMin = "1"))
	int32 MaxFunctionDepth = 32;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "UE Node2Code")
	bool bIncludeDebugMetadata = false;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "UE Node2Code")
	bool bIncludeDefaultLikeProperties = false;
};

class UE_NODE2CODE_API FUE2CodeMaterialExporter
{
public:
	static bool ExportMaterialAssetPathToText(const FString& MaterialAssetPath, const FString& OutputFilePath, const FUE2CodeExportOptions& Options, FString& OutError);
	static bool ExportMaterialToText(UMaterialInterface* Material, const FString& OutputFilePath, const FUE2CodeExportOptions& Options, FString& OutError);
	static bool ExportMaterialToString(UMaterialInterface* Material, const FUE2CodeExportOptions& Options, FString& OutText, FString& OutError);

	static bool ExportMaterialPropertyToText(UMaterialInterface* Material, EMaterialProperty MaterialProperty, const FString& OutputFilePath, const FUE2CodeExportOptions& Options, FString& OutError);
	static bool ExportMaterialPropertyToString(UMaterialInterface* Material, EMaterialProperty MaterialProperty, const FUE2CodeExportOptions& Options, FString& OutText, FString& OutError);

	static bool ExportMaterialNodeByNameToText(UMaterialInterface* Material, const FString& ExpressionObjectName, const FString& OutputFilePath, const FUE2CodeExportOptions& Options, FString& OutError);
	static bool ExportMaterialNodeByNameToString(UMaterialInterface* Material, const FString& ExpressionObjectName, const FUE2CodeExportOptions& Options, FString& OutText, FString& OutError);

	static bool ExportMaterialExpressionToString(UMaterialExpression* RootExpression, const FUE2CodeExportOptions& Options, FString& OutText, FString& OutError);

	static bool ParseMaterialProperty(const FString& PropertyName, EMaterialProperty& OutProperty);
	static FString MaterialPropertyToString(EMaterialProperty Property);
	static FString NormalizeMaterialAssetPath(const FString& InputPath);
};
