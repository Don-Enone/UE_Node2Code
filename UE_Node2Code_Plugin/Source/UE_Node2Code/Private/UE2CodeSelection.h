#pragma once

#include "CoreMinimal.h"
#include "UE2CodeMaterialExporter.h"

// Shared "export selected nodes only" filter. Every exporter applies it to its
// root graph(s) only; called-graph definitions are always exported whole.
namespace UE2CodeSelection
{
	struct FNodeSelection
	{
		explicit FNodeSelection(const FUE2CodeExportOptions& Options)
		{
			for (const FString& Id : Options.SelectedNodeIds)
			{
				const FString Trimmed = Id.TrimStartAndEnd();
				if (!Trimmed.IsEmpty()) { Ids.Add(Trimmed); }
			}
		}

		bool IsActive() const { return Ids.Num() > 0; }

		// FString hashing and comparison are case-insensitive.
		bool Contains(const UObject* Object, const FGuid& Guid) const
		{
			if (!Object) { return false; }
			if (Guid.IsValid() && (Ids.Contains(Guid.ToString()) || Ids.Contains(Guid.ToString(EGuidFormats::DigitsWithHyphens)))) { return true; }
			return Ids.Contains(Object->GetName()) || Ids.Contains(Object->GetPathName());
		}

		TSet<FString> Ids;
	};

	static FString HeaderLine(int32 NodeCount)
	{
		return FString::Printf(TEXT("selection: nodes=%d"), NodeCount);
	}

	static FString NoMatchError(const FString& AssetName)
	{
		return FString::Printf(TEXT("None of the selected nodes were found in '%s'. Save or apply the asset if the nodes are new, or reselect them."), *AssetName);
	}
}
