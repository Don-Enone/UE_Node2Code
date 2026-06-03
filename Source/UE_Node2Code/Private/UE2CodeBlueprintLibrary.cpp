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

bool UUE2CodeBlueprintLibrary::ExportMaterialPropertyToText(UMaterialInterface* Material, TEnumAsByte<EMaterialProperty> MaterialProperty, const FString& OutputFilePath, FUE2CodeExportOptions Options, FString& OutError)
{
	return FUE2CodeMaterialExporter::ExportMaterialPropertyToText(Material, MaterialProperty.GetValue(), OutputFilePath, Options, OutError);
}

bool UUE2CodeBlueprintLibrary::ExportMaterialNodeByNameToText(UMaterialInterface* Material, const FString& ExpressionObjectName, const FString& OutputFilePath, FUE2CodeExportOptions Options, FString& OutError)
{
	return FUE2CodeMaterialExporter::ExportMaterialNodeByNameToText(Material, ExpressionObjectName, OutputFilePath, Options, OutError);
}
