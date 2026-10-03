#include "UE_Node2Code.h"

#include "DesktopPlatformModule.h"
#include "Editor.h"
#include "Engine/Selection.h"
#include "Framework/Application/SlateApplication.h"
#include "Framework/Docking/TabManager.h"
#include "GraphEditor.h"
#include "HAL/IConsoleManager.h"
#include "IDesktopPlatform.h"
#include "MaterialGraph/MaterialGraph.h"
#include "MaterialGraph/MaterialGraphNode.h"
#include "Materials/Material.h"
#include "Materials/MaterialExpression.h"
#include "Materials/MaterialFunctionInterface.h"
#include "Materials/MaterialInterface.h"
#include "Misc/MessageDialog.h"
#include "Misc/PackageName.h"
#include "Misc/Paths.h"
#include "NiagaraScript.h"
#include "UE2CodeMaterialExporter.h"
#include "UE2CodeNiagaraExporter.h"
#include "UE2CodeBlueprintExporter.h"
#include "Engine/Blueprint.h"
#include "ToolMenus.h"
#include "Toolkits/AssetEditorToolkit.h"
#include "Toolkits/AssetEditorToolkitMenuContext.h"
#include "Toolkits/IToolkitHost.h"
#include "UObject/Package.h"
#include "Widgets/Docking/SDockTab.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Input/SCheckBox.h"
#include "Widgets/Input/SComboBox.h"
#include "Widgets/Input/SEditableTextBox.h"
#include "Widgets/Input/SSpinBox.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Layout/SScrollBox.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/Text/STextBlock.h"

DEFINE_LOG_CATEGORY_STATIC(LogUE2Code, Log, All);

#define LOCTEXT_NAMESPACE "FUE2CodeModule"

namespace
{
	static const FName UE_Node2CodeExportTabName(TEXT("UE_Node2CodeExport"));
	enum class EUE2CodeExportMode : int32
	{
		Material,
		MaterialFunction,
		NiagaraFunctionScript,
		NiagaraModuleScript,
		MaterialProperty,
		MaterialNode,
		Blueprint
	};

	static void ApplyOptionalHierarchyDepth(const TArray<FString>& Args, int32 ArgIndex, FUE2CodeExportOptions& Options)
	{
		if (!Args.IsValidIndex(ArgIndex))
		{
			return;
		}

		int32 ParsedDepth = 0;
		if (LexTryParseString(ParsedDepth, *Args[ArgIndex]))
		{
			Options.NodeHierarchyDepth = FMath::Max(0, ParsedDepth);
		}
	}

	static UMaterialInterface* LoadMaterialForExport(const FString& InputPath, FString& OutError)
	{
		const FString AssetPath = FUE2CodeMaterialExporter::NormalizeMaterialAssetPath(InputPath);
		UMaterialInterface* Material = Cast<UMaterialInterface>(StaticLoadObject(UMaterialInterface::StaticClass(), nullptr, *AssetPath));
		if (!Material && !AssetPath.Contains(TEXT(".")))
		{
			const FString ObjectPath = AssetPath + TEXT(".") + FPackageName::GetShortName(AssetPath);
			Material = Cast<UMaterialInterface>(StaticLoadObject(UMaterialInterface::StaticClass(), nullptr, *ObjectPath));
		}
		if (!Material)
		{
			OutError = FString::Printf(TEXT("Could not load material '%s'."), *AssetPath);
		}
		return Material;
	}

	static FString DefaultOutputPath(const FString& AssetName, bool bSelection)
	{
		const FString FileName = AssetName + (bSelection ? TEXT(".selection.ue2code.txt") : TEXT(".ue2code.txt"));
		return FPaths::ConvertRelativePathToFull(FPaths::ProjectDir() / TEXT("UE_Node2Code") / FileName);
	}

	static UObject* FindSupportedEditedAsset(const FAssetEditorToolkit& Toolkit)
	{
		// Editors list the asset first; later entries can be transient working copies.
		const TArray<UObject*>* EditedObjects = Toolkit.GetObjectsCurrentlyBeingEdited();
		for (UObject* Object : EditedObjects ? *EditedObjects : TArray<UObject*>())
		{
			if (Object && Object->GetOutermost() != GetTransientPackage()
				&& (Object->IsA<UBlueprint>() || Object->IsA<UMaterialInterface>()
					|| Object->IsA<UMaterialFunctionInterface>() || Object->IsA<UNiagaraScript>()))
			{
				return Object;
			}
		}
		return nullptr;
	}

	static void CollectGraphEditorSelection(const TSharedRef<SWidget>& Widget, TSet<UObject*>& OutNodes)
	{
		static const FName GraphEditorType(TEXT("SGraphEditor"));
		if (Widget->GetType() == GraphEditorType)
		{
			for (UObject* Node : StaticCastSharedRef<SGraphEditor>(Widget)->GetSelectedNodes())
			{
				OutNodes.Add(Node);
			}
			return;
		}
		FChildren* Children = Widget->GetChildren();
		for (int32 Index = 0; Children && Index < Children->Num(); ++Index)
		{
			CollectGraphEditorSelection(Children->GetChildAt(Index), OutNodes);
		}
	}

	// Ids understood by FUE2CodeExportOptions::SelectedNodeIds. Material editors edit a
	// duplicate of the asset, so expressions are identified by their preserved object name.
	static TArray<FString> CollectSelectedNodeIds(FAssetEditorToolkit& Toolkit)
	{
		TSet<UObject*> SelectedObjects;
		CollectGraphEditorSelection(Toolkit.GetToolkitHost()->GetParentWidget(), SelectedObjects);
		TArray<FString> Ids;
		for (UObject* Object : SelectedObjects)
		{
			UEdGraphNode* Node = Cast<UEdGraphNode>(Object);
			if (!Node)
			{
				continue;
			}
			if (Node->GetGraph() && Node->GetGraph()->IsA<UMaterialGraph>())
			{
				const UMaterialGraphNode* MaterialNode = Cast<UMaterialGraphNode>(Node);
				if (MaterialNode && MaterialNode->MaterialExpression)
				{
					Ids.AddUnique(MaterialNode->MaterialExpression->GetName());
				}
			}
			else
			{
				Ids.AddUnique(Node->NodeGuid.ToString());
			}
		}
		Ids.Sort();
		return Ids;
	}

	class SUE2CodeExportWidget;
	static TWeakPtr<SUE2CodeExportWidget> GExportWidget;

	class SUE2CodeExportWidget : public SCompoundWidget
	{
	public:
		SLATE_BEGIN_ARGS(SUE2CodeExportWidget) {}
		SLATE_END_ARGS()

		void Construct(const FArguments& InArgs)
		{
			ModeOptions.Add(MakeShared<FString>(TEXT("Material")));
			ModeOptions.Add(MakeShared<FString>(TEXT("Material Function")));
			ModeOptions.Add(MakeShared<FString>(TEXT("Niagara Function Script")));
			ModeOptions.Add(MakeShared<FString>(TEXT("Niagara Module Script")));
			ModeOptions.Add(MakeShared<FString>(TEXT("Material Property")));
			ModeOptions.Add(MakeShared<FString>(TEXT("Material Node")));
			ModeOptions.Add(MakeShared<FString>(TEXT("Blueprint")));
			SelectedMode = ModeOptions[static_cast<int32>(EUE2CodeExportMode::Material)];

			ChildSlot
			[
				SNew(SBorder)
				.Padding(12)
				[
					SNew(SScrollBox)
					+ SScrollBox::Slot()
					[
						SNew(SVerticalBox)
						+ SVerticalBox::Slot()
						.AutoHeight()
						.Padding(0, 0, 0, 10)
						[
							SNew(STextBlock)
							.Text(LOCTEXT("ExportTitle", "UE Node2Code Graph Export"))
						]
						+ SVerticalBox::Slot()
						.AutoHeight()
						.Padding(0, 3)
						[
							MakeTextBoxRow(
								LOCTEXT("GraphAsset", "Graph Asset"),
								SAssignNew(MaterialPathTextBox, SEditableTextBox)
								.Text(FText::FromString(TEXT("/Game/Test/MaterialTest")))
							)
						]
						+ SVerticalBox::Slot()
						.AutoHeight()
						.Padding(0, 3)
						[
							SNew(SHorizontalBox)
							+ SHorizontalBox::Slot()
							.FillWidth(1.0f)
							[
								SNew(STextBlock).Text(FText::GetEmpty())
							]
							+ SHorizontalBox::Slot()
							.AutoWidth()
							.Padding(4, 0)
							[
								SNew(SButton)
								.Text(LOCTEXT("UseSelected", "Use Selected Asset"))
								.OnClicked(this, &SUE2CodeExportWidget::UseSelectedAsset)
							]
						]
						+ SVerticalBox::Slot()
						.AutoHeight()
						.Padding(0, 3)
						[
							MakeTextBoxRow(
								LOCTEXT("OutputFile", "Output File"),
								SAssignNew(OutputPathTextBox, SEditableTextBox)
								.Text(FText::FromString(FPaths::ConvertRelativePathToFull(FPaths::ProjectDir() / TEXT("UE_Node2Code/MaterialExport.ue2code.txt"))))
							)
						]
						+ SVerticalBox::Slot()
						.AutoHeight()
						.Padding(0, 3)
						[
							SNew(SHorizontalBox)
							+ SHorizontalBox::Slot()
							.FillWidth(1.0f)
							[
								SNew(STextBlock).Text(FText::GetEmpty())
							]
							+ SHorizontalBox::Slot()
							.AutoWidth()
							.Padding(4, 0)
							[
								SNew(SButton)
								.Text(LOCTEXT("BrowseOutput", "Browse"))
								.OnClicked(this, &SUE2CodeExportWidget::BrowseOutput)
							]
						]
						+ SVerticalBox::Slot()
						.AutoHeight()
						.Padding(0, 3)
						[
							MakeWidgetRow(
								LOCTEXT("ExportMode", "Export Mode"),
								SAssignNew(ModeComboBox, SComboBox<TSharedPtr<FString>>)
								.OptionsSource(&ModeOptions)
								.InitiallySelectedItem(SelectedMode)
								.OnGenerateWidget_Lambda([](TSharedPtr<FString> Item)
								{
									return SNew(STextBlock).Text(FText::FromString(Item.IsValid() ? *Item : FString()));
								})
								.OnSelectionChanged(this, &SUE2CodeExportWidget::OnModeChanged)
								[
									SNew(STextBlock).Text(this, &SUE2CodeExportWidget::GetSelectedModeText)
								]
							)
						]
						+ SVerticalBox::Slot()
						.AutoHeight()
						.Padding(0, 3)
						[
							SNew(SBox)
							.Visibility(this, &SUE2CodeExportWidget::GetPropertyVisibility)
							[
								MakeTextBoxRow(
									LOCTEXT("MaterialProperty", "Material Property"),
									SAssignNew(PropertyTextBox, SEditableTextBox)
									.Text(FText::FromString(TEXT("MP_BaseColor")))
								)
							]
						]
						+ SVerticalBox::Slot()
						.AutoHeight()
						.Padding(0, 3)
						[
							SNew(SBox)
							.Visibility(this, &SUE2CodeExportWidget::GetNodeVisibility)
							[
								MakeTextBoxRow(
									LOCTEXT("NodeName", "Node Name"),
									SAssignNew(NodeTextBox, SEditableTextBox)
									.Text(FText::FromString(TEXT("MaterialExpressionMaterialFunctionCall_2")))
								)
							]
						]
						+ SVerticalBox::Slot()
						.AutoHeight()
						.Padding(0, 3)
						[
							MakeWidgetRow(
								LOCTEXT("HierarchyDepth", "Node Hierarchy Depth"),
								SAssignNew(HierarchyDepthSpinBox, SSpinBox<int32>)
								.MinValue(0)
								.MaxValue(64)
								.Value(0)
								.ToolTipText(LOCTEXT("HierarchyDepthTip", "Applies to every supported graph. 0 recursively expands called graphs to basic nodes. 1 exports only the current graph. 2 expands direct calls. Higher values expand deeper calls."))
							)
						]
						+ SVerticalBox::Slot()
						.AutoHeight()
						.Padding(0, 8, 0, 0)
						[
							SNew(SBox)
							.Visibility(this, &SUE2CodeExportWidget::GetSelectionVisibility)
							[
								SNew(SHorizontalBox)
								+ SHorizontalBox::Slot()
								.FillWidth(1.0f)
								.VAlign(VAlign_Center)
								[
									SNew(SCheckBox)
									.IsChecked(this, &SUE2CodeExportWidget::GetSelectedOnlyState)
									.OnCheckStateChanged(this, &SUE2CodeExportWidget::OnSelectedOnlyChanged)
									.ToolTipText(LOCTEXT("SelectedOnlyTip", "Export only the nodes selected in the asset editor. Graphs called by those nodes still follow Node Hierarchy Depth; links leaving the selection are marked 'unselected'."))
									[
										SNew(STextBlock).Text(this, &SUE2CodeExportWidget::GetSelectedOnlyLabel)
									]
								]
								+ SHorizontalBox::Slot()
								.AutoWidth()
								.Padding(4, 0)
								[
									SNew(SButton)
									.Text(LOCTEXT("RefreshSelection", "Refresh Selection"))
									.ToolTipText(LOCTEXT("RefreshSelectionTip", "Read the current node selection again from the asset editor this window was opened from."))
									.Visibility(this, &SUE2CodeExportWidget::GetRefreshSelectionVisibility)
									.OnClicked(this, &SUE2CodeExportWidget::RefreshSelection)
								]
							]
						]
						+ SVerticalBox::Slot()
						.AutoHeight()
						.Padding(0, 8, 0, 0)
						[
							SNew(SBox)
							.Visibility(this, &SUE2CodeExportWidget::GetUnreferencedVisibility)
							[
								MakeCheckBoxRow(LOCTEXT("ExportUnreferenced", "Export Unreferenced Material Nodes"), bExportUnreferenced)
							]
						]
						+ SVerticalBox::Slot()
						.AutoHeight()
						.Padding(0, 3)
						[
							MakeCheckBoxRow(LOCTEXT("IncludeDebug", "Include Debug Metadata"), bIncludeDebugMetadata)
						]
						+ SVerticalBox::Slot()
						.AutoHeight()
						.Padding(0, 3)
						[
							MakeCheckBoxRow(LOCTEXT("IncludeDefaults", "Include Default-Like Properties"), bIncludeDefaultLikeProperties)
						]
						+ SVerticalBox::Slot()
						.AutoHeight()
						.Padding(0, 12, 0, 0)
						[
							SNew(SHorizontalBox)
							+ SHorizontalBox::Slot()
							.AutoWidth()
							[
								SNew(SButton)
								.Text(LOCTEXT("ExportButton", "Export"))
								.OnClicked(this, &SUE2CodeExportWidget::ExportNow)
							]
							+ SHorizontalBox::Slot()
							.FillWidth(1.0f)
							.Padding(12, 3, 0, 0)
							[
								SAssignNew(StatusTextBlock, STextBlock)
								.Text(LOCTEXT("ReadyStatus", "Ready"))
							]
						]
					]
				]
			];
		}

		// Called when the window is opened from an asset editor's Window menu.
		void ApplyEditorContext(const TSharedPtr<FAssetEditorToolkit>& Toolkit)
		{
			SourceToolkit = Toolkit;
			ClearSelection();
			UObject* Asset = Toolkit.IsValid() ? FindSupportedEditedAsset(*Toolkit) : nullptr;
			if (!Asset)
			{
				SetStatus(TEXT("The current editor does not edit a supported graph asset."));
				return;
			}
			if (ApplyAsset(Asset))
			{
				ApplySelection(CollectSelectedNodeIds(*Toolkit));
			}
		}

	private:
		TSharedRef<SWidget> MakeWidgetRow(const FText& Label, const TSharedRef<SWidget>& Widget)
		{
			return SNew(SHorizontalBox)
				+ SHorizontalBox::Slot()
				.AutoWidth()
				.VAlign(VAlign_Center)
				.Padding(0, 0, 10, 0)
				[
					SNew(STextBlock)
					.MinDesiredWidth(150)
					.Text(Label)
				]
				+ SHorizontalBox::Slot()
				.FillWidth(1.0f)
				[
					Widget
				];
		}

		TSharedRef<SWidget> MakeTextBoxRow(const FText& Label, const TSharedRef<SEditableTextBox>& TextBox)
		{
			return MakeWidgetRow(Label, TextBox);
		}

		TSharedRef<SWidget> MakeCheckBoxRow(const FText& Label, bool& Value)
		{
			return SNew(SCheckBox)
				.IsChecked_Lambda([&Value]()
				{
					return Value ? ECheckBoxState::Checked : ECheckBoxState::Unchecked;
				})
				.OnCheckStateChanged_Lambda([&Value](ECheckBoxState NewState)
				{
					Value = NewState == ECheckBoxState::Checked;
				})
				[
					SNew(STextBlock).Text(Label)
				];
		}

		void OnModeChanged(TSharedPtr<FString> NewSelection, ESelectInfo::Type)
		{
			if (NewSelection.IsValid())
			{
				SelectedMode = NewSelection;
			}
		}

		FText GetSelectedModeText() const
		{
			return FText::FromString(SelectedMode.IsValid() ? *SelectedMode : FString());
		}

		EVisibility GetPropertyVisibility() const
		{
			return IsMode(EUE2CodeExportMode::MaterialProperty) ? EVisibility::Visible : EVisibility::Collapsed;
		}

		EVisibility GetNodeVisibility() const
		{
			return IsMode(EUE2CodeExportMode::MaterialNode) ? EVisibility::Visible : EVisibility::Collapsed;
		}

		EVisibility GetUnreferencedVisibility() const
		{
			return IsMode(EUE2CodeExportMode::Material) ? EVisibility::Visible : EVisibility::Collapsed;
		}

		bool IsNiagaraMode() const
		{
			return IsMode(EUE2CodeExportMode::NiagaraFunctionScript) || IsMode(EUE2CodeExportMode::NiagaraModuleScript);
		}

		bool IsMode(EUE2CodeExportMode Mode) const
		{
			return SelectedMode == ModeOptions[static_cast<int32>(Mode)];
		}

		void SetMode(EUE2CodeExportMode Mode)
		{
			SelectedMode = ModeOptions[static_cast<int32>(Mode)];
			if (ModeComboBox.IsValid())
			{
				ModeComboBox->SetSelectedItem(SelectedMode);
			}
		}

		FReply UseSelectedAsset()
		{
			// A Content Browser asset replaces any node selection taken from an editor.
			SourceToolkit.Reset();
			ClearSelection();
			if (!GEditor || !GEditor->GetSelectedObjects())
			{
				SetStatus(TEXT("No selected supported graph asset."));
				return FReply::Handled();
			}

			TArray<UObject*> SelectedObjects;
			GEditor->GetSelectedObjects()->GetSelectedObjects(SelectedObjects);
			for (UObject* Object : SelectedObjects)
			{
				if (Object && (Object->IsA<UBlueprint>() || Object->IsA<UNiagaraScript>()
					|| Object->IsA<UMaterialInterface>() || Object->IsA<UMaterialFunctionInterface>()))
				{
					ApplyAsset(Object);
					return FReply::Handled();
				}
			}

			SetStatus(TEXT("Selection does not contain a Blueprint, material, material function, Niagara Function Script, or Niagara Module Script."));
			return FReply::Handled();
		}

		// Fills the asset path, default output file and export mode for a supported asset.
		bool ApplyAsset(UObject* Object)
		{
			if (Cast<UBlueprint>(Object))
			{
				SetMode(EUE2CodeExportMode::Blueprint);
				SetStatus(TEXT("Blueprint applied."));
			}
			else if (UNiagaraScript* NiagaraScript = Cast<UNiagaraScript>(Object))
			{
				if (NiagaraScript->IsFunctionScript())
				{
					SetMode(EUE2CodeExportMode::NiagaraFunctionScript);
					SetStatus(TEXT("Niagara Function Script applied."));
				}
				else if (NiagaraScript->IsModuleScript())
				{
					SetMode(EUE2CodeExportMode::NiagaraModuleScript);
					SetStatus(TEXT("Niagara Module Script applied."));
				}
				else
				{
					SetStatus(TEXT("Niagara script is neither a Function Script nor a Module Script."));
					return false;
				}
			}
			else if (Cast<UMaterialInterface>(Object))
			{
				SetMode(EUE2CodeExportMode::Material);
				SetStatus(TEXT("Material applied."));
			}
			else if (Cast<UMaterialFunctionInterface>(Object))
			{
				SetMode(EUE2CodeExportMode::MaterialFunction);
				SetStatus(TEXT("Material function applied."));
			}
			else
			{
				return false;
			}
			MaterialPathTextBox->SetText(FText::FromString(Object->GetPathName()));
			AssetName = Object->GetName();
			OutputPathTextBox->SetText(FText::FromString(DefaultOutputPath(AssetName, false)));
			return true;
		}

		void ApplySelection(const TArray<FString>& NodeIds)
		{
			SelectedNodeIds = NodeIds;
			SelectionAssetPath = MaterialPathTextBox->GetText().ToString().TrimStartAndEnd();
			// A selection made in the editor is exported by default.
			SetSelectedOnly(SelectedNodeIds.Num() > 0);
			if (SelectedNodeIds.Num() > 0)
			{
				SetStatus(FString::Printf(TEXT("Current asset applied with %d selected node(s)."), SelectedNodeIds.Num()));
			}
		}

		void ClearSelection()
		{
			SetSelectedOnly(false);
			SelectedNodeIds.Reset();
			SelectionAssetPath.Reset();
		}

		// Swaps between the full and selection output names unless the user picked a custom file.
		void SetSelectedOnly(bool bValue)
		{
			if (bSelectedOnly != bValue && !AssetName.IsEmpty() && OutputPathTextBox.IsValid()
				&& FPaths::IsSamePath(OutputPathTextBox->GetText().ToString(), DefaultOutputPath(AssetName, bSelectedOnly)))
			{
				OutputPathTextBox->SetText(FText::FromString(DefaultOutputPath(AssetName, bValue)));
			}
			bSelectedOnly = bValue;
		}

		bool SupportsSelection() const
		{
			return IsMode(EUE2CodeExportMode::Blueprint) || IsMode(EUE2CodeExportMode::Material)
				|| IsMode(EUE2CodeExportMode::MaterialFunction) || IsNiagaraMode();
		}

		// The selection belongs to the asset it was read from; editing the path detaches it.
		bool IsSelectionApplicable() const
		{
			return SelectedNodeIds.Num() > 0 && SupportsSelection()
				&& MaterialPathTextBox->GetText().ToString().TrimStartAndEnd() == SelectionAssetPath;
		}

		EVisibility GetSelectionVisibility() const
		{
			return IsSelectionApplicable() ? EVisibility::Visible : EVisibility::Collapsed;
		}

		EVisibility GetRefreshSelectionVisibility() const
		{
			return SourceToolkit.IsValid() ? EVisibility::Visible : EVisibility::Collapsed;
		}

		ECheckBoxState GetSelectedOnlyState() const
		{
			return bSelectedOnly ? ECheckBoxState::Checked : ECheckBoxState::Unchecked;
		}

		void OnSelectedOnlyChanged(ECheckBoxState NewState)
		{
			SetSelectedOnly(NewState == ECheckBoxState::Checked);
		}

		FText GetSelectedOnlyLabel() const
		{
			return FText::Format(LOCTEXT("SelectedOnlyLabel", "Export Selected Nodes Only ({0} selected)"), FText::AsNumber(SelectedNodeIds.Num()));
		}

		FReply RefreshSelection()
		{
			const TSharedPtr<FAssetEditorToolkit> Toolkit = SourceToolkit.Pin();
			if (!Toolkit.IsValid())
			{
				SetStatus(TEXT("The source asset editor is closed."));
				return FReply::Handled();
			}
			ApplySelection(CollectSelectedNodeIds(*Toolkit));
			if (SelectedNodeIds.Num() == 0)
			{
				SetStatus(TEXT("No nodes are selected in the source asset editor."));
			}
			return FReply::Handled();
		}

		FReply BrowseOutput()
		{
			IDesktopPlatform* DesktopPlatform = FDesktopPlatformModule::Get();
			if (!DesktopPlatform)
			{
				SetStatus(TEXT("Desktop platform file dialog is unavailable."));
				return FReply::Handled();
			}

			TArray<FString> Files;
			const FString CurrentPath = OutputPathTextBox->GetText().ToString();
			const FString DefaultPath = FPaths::GetPath(CurrentPath);
			const FString DefaultFile = FPaths::GetCleanFilename(CurrentPath);
			const bool bPicked = DesktopPlatform->SaveFileDialog(
				FSlateApplication::Get().FindBestParentWindowHandleForDialogs(nullptr),
				TEXT("Export UE Node2Code Text"),
				DefaultPath,
				DefaultFile,
				TEXT("UE Node2Code (*.ue2code.txt)|*.ue2code.txt|Text (*.txt)|*.txt"),
				EFileDialogFlags::None,
				Files
			);

			if (bPicked && Files.Num() > 0)
			{
				OutputPathTextBox->SetText(FText::FromString(Files[0]));
			}
			return FReply::Handled();
		}

		FReply ExportNow()
		{
			const FString MaterialPath = MaterialPathTextBox->GetText().ToString().TrimStartAndEnd();
			const FString OutputPath = OutputPathTextBox->GetText().ToString().TrimStartAndEnd();
			if (MaterialPath.IsEmpty() || OutputPath.IsEmpty())
			{
				SetStatus(TEXT("Asset and output file are required."));
				return FReply::Handled();
			}

			FUE2CodeExportOptions Options;
			Options.NodeHierarchyDepth = FMath::Max(0, static_cast<int32>(HierarchyDepthSpinBox->GetValue()));
			Options.bExportUnreferencedMaterialExpressions = bExportUnreferenced;
			Options.bIncludeDebugMetadata = bIncludeDebugMetadata;
			Options.bIncludeDefaultLikeProperties = bIncludeDefaultLikeProperties;
			const bool bExportSelection = bSelectedOnly && IsSelectionApplicable();
			if (bExportSelection)
			{
				Options.SelectedNodeIds = SelectedNodeIds;
			}

			FString Error;
			bool bSuccess = false;
			if (IsMode(EUE2CodeExportMode::Blueprint))
			{
				bSuccess = FUE2CodeBlueprintExporter::ExportBlueprintAssetPathToText(MaterialPath, OutputPath, Options, Error);
			}
			else if (IsMode(EUE2CodeExportMode::Material))
			{
				bSuccess = FUE2CodeMaterialExporter::ExportMaterialAssetPathToText(MaterialPath, OutputPath, Options, Error);
			}
			else if (IsMode(EUE2CodeExportMode::MaterialFunction))
			{
				bSuccess = FUE2CodeMaterialExporter::ExportMaterialFunctionAssetPathToText(MaterialPath, OutputPath, Options, Error);
			}
			else if (IsMode(EUE2CodeExportMode::NiagaraFunctionScript))
			{
				bSuccess = FUE2CodeNiagaraExporter::ExportNiagaraFunctionScriptAssetPathToText(MaterialPath, OutputPath, Options, Error);
			}
			else if (IsMode(EUE2CodeExportMode::NiagaraModuleScript))
			{
				bSuccess = FUE2CodeNiagaraExporter::ExportNiagaraModuleScriptAssetPathToText(MaterialPath, OutputPath, Options, Error);
			}
			else
			{
				UMaterialInterface* Material = LoadMaterialForExport(MaterialPath, Error);
				if (Material)
				{
					if (IsMode(EUE2CodeExportMode::MaterialProperty))
					{
						EMaterialProperty MaterialProperty = MP_MAX;
						if (FUE2CodeMaterialExporter::ParseMaterialProperty(PropertyTextBox->GetText().ToString(), MaterialProperty))
						{
							bSuccess = FUE2CodeMaterialExporter::ExportMaterialPropertyToText(Material, MaterialProperty, OutputPath, Options, Error);
						}
						else
						{
							Error = FString::Printf(TEXT("Could not parse material property '%s'."), *PropertyTextBox->GetText().ToString());
						}
					}
					else
					{
						bSuccess = FUE2CodeMaterialExporter::ExportMaterialNodeByNameToText(Material, NodeTextBox->GetText().ToString(), OutputPath, Options, Error);
					}
				}
			}

			if (bSuccess)
			{
				SetStatus(bExportSelection
					? FString::Printf(TEXT("Exported %d selected node(s) to %s"), SelectedNodeIds.Num(), *OutputPath)
					: FString::Printf(TEXT("Exported to %s"), *OutputPath));
			}
			else
			{
				SetStatus(Error);
				FMessageDialog::Open(EAppMsgType::Ok, FText::FromString(Error));
			}
			return FReply::Handled();
		}

		void SetStatus(const FString& Status)
		{
			if (StatusTextBlock.IsValid())
			{
				StatusTextBlock->SetText(FText::FromString(Status));
			}
		}

		TArray<TSharedPtr<FString>> ModeOptions;
		TSharedPtr<FString> SelectedMode;
		TSharedPtr<SEditableTextBox> MaterialPathTextBox;
		TSharedPtr<SEditableTextBox> OutputPathTextBox;
		TSharedPtr<SEditableTextBox> PropertyTextBox;
		TSharedPtr<SEditableTextBox> NodeTextBox;
		TSharedPtr<SSpinBox<int32>> HierarchyDepthSpinBox;
		TSharedPtr<SComboBox<TSharedPtr<FString>>> ModeComboBox;
		TSharedPtr<STextBlock> StatusTextBlock;
		bool bExportUnreferenced = false;
		bool bIncludeDebugMetadata = false;
		bool bIncludeDefaultLikeProperties = false;
		FString AssetName;
		TArray<FString> SelectedNodeIds;
		FString SelectionAssetPath;
		bool bSelectedOnly = false;
		TWeakPtr<FAssetEditorToolkit> SourceToolkit;
	};
}

void FUE2CodeModule::StartupModule()
{
	RegisterConsoleCommands();
	RegisterGui();
}

void FUE2CodeModule::ShutdownModule()
{
	UnregisterGui();
	UnregisterConsoleCommands();
}

void FUE2CodeModule::RegisterGui()
{
	if (IsRunningCommandlet())
	{
		return;
	}

	FGlobalTabmanager::Get()->RegisterNomadTabSpawner(
		UE_Node2CodeExportTabName,
		FOnSpawnTab::CreateRaw(this, &FUE2CodeModule::SpawnExportTab)
	)
	.SetDisplayName(LOCTEXT("UE_Node2CodeExportTab", "UE Node2Code"))
	.SetMenuType(ETabSpawnerMenuType::Hidden);

	UToolMenus::RegisterStartupCallback(FSimpleMulticastDelegate::FDelegate::CreateRaw(this, &FUE2CodeModule::RegisterMenus));
}

void FUE2CodeModule::UnregisterGui()
{
	if (IsRunningCommandlet())
	{
		return;
	}

	UToolMenus::UnRegisterStartupCallback(this);
	UToolMenus::UnregisterOwner(this);
	FGlobalTabmanager::Get()->UnregisterNomadTabSpawner(UE_Node2CodeExportTabName);
}

void FUE2CodeModule::RegisterMenus()
{
	FToolMenuOwnerScoped OwnerScoped(this);
	// Every editor's Window menu (level editor and all asset editors) inherits this menu.
	UToolMenu* WindowMenu = UToolMenus::Get()->ExtendMenu(TEXT("MainFrame.MainMenu.Window"));
	FToolMenuSection& Section = WindowMenu->AddSection(
		TEXT("UE_Node2Code"),
		LOCTEXT("UE_Node2CodeSection", "UE Node2Code"),
		FToolMenuInsert(TEXT("WindowLayout"), EToolMenuInsertType::Before));
	Section.AddMenuEntry(
		TEXT("OpenUE_Node2Code"),
		LOCTEXT("OpenUE_Node2Code", "UE Node2Code"),
		LOCTEXT("OpenUE_Node2CodeTooltip", "Open the UE Node2Code graph export window. From an asset editor, the edited asset and its selected nodes are used."),
		FSlateIcon(),
		FToolUIAction(FToolMenuExecuteAction::CreateRaw(this, &FUE2CodeModule::OpenExportWindow))
	);
}

void FUE2CodeModule::OpenExportWindow(const FToolMenuContext& MenuContext)
{
	const UAssetEditorToolkitMenuContext* ToolkitContext = MenuContext.FindContext<UAssetEditorToolkitMenuContext>();
	const TSharedPtr<FAssetEditorToolkit> Toolkit = ToolkitContext ? ToolkitContext->Toolkit.Pin() : nullptr;
	const TSharedPtr<FTabManager> TabManager = Toolkit.IsValid() && Toolkit->GetTabManager().IsValid()
		? Toolkit->GetTabManager()
		: TSharedPtr<FTabManager>(FGlobalTabmanager::Get());
	TabManager->TryInvokeTab(UE_Node2CodeExportTabName);

	const TSharedPtr<SUE2CodeExportWidget> Widget = GExportWidget.Pin();
	if (Toolkit.IsValid() && Widget.IsValid())
	{
		Widget->ApplyEditorContext(Toolkit);
	}
}

TSharedRef<SDockTab> FUE2CodeModule::SpawnExportTab(const FSpawnTabArgs& SpawnTabArgs)
{
	TSharedRef<SUE2CodeExportWidget> Widget = SNew(SUE2CodeExportWidget);
	GExportWidget = Widget;
	return SNew(SDockTab)
		.TabRole(ETabRole::NomadTab)
		[
			Widget
		];
}

void FUE2CodeModule::RegisterConsoleCommands()
{
	ConsoleCommands.Add(IConsoleManager::Get().RegisterConsoleCommand(
		TEXT("UE_Node2Code.ExportBlueprint"),
		TEXT("Exports Blueprint graphs to AI-readable text. Args: <BlueprintAssetPathOrUAssetFile> <OutputFilePath> [NodeHierarchyDepth]"),
		FConsoleCommandWithArgsDelegate::CreateRaw(this, &FUE2CodeModule::ExportBlueprintCommand),
		ECVF_Default
	));
	IConsoleManager& ConsoleManager = IConsoleManager::Get();

	ConsoleCommands.Add(ConsoleManager.RegisterConsoleCommand(
		TEXT("UE_Node2Code.ExportMaterial"),
		TEXT("Exports a material asset to AI-readable text. Args: <MaterialAssetPathOrUAssetFile> <OutputFilePath> [NodeHierarchyDepth]"),
		FConsoleCommandWithArgsDelegate::CreateRaw(this, &FUE2CodeModule::ExportMaterialCommand),
		ECVF_Default
	));

	ConsoleCommands.Add(ConsoleManager.RegisterConsoleCommand(
		TEXT("UE_Node2Code.ExportMaterialFunction"),
		TEXT("Exports a material function asset to AI-readable text. Args: <MaterialFunctionAssetPathOrUAssetFile> <OutputFilePath> [NodeHierarchyDepth]"),
		FConsoleCommandWithArgsDelegate::CreateRaw(this, &FUE2CodeModule::ExportMaterialFunctionCommand),
		ECVF_Default
	));

	ConsoleCommands.Add(ConsoleManager.RegisterConsoleCommand(
		TEXT("UE_Node2Code.ExportNiagaraFunctionScript"),
		TEXT("Exports a Niagara Function Script asset to AI-readable text. Args: <ScriptAssetPathOrUAssetFile> <OutputFilePath> [NodeHierarchyDepth]"),
		FConsoleCommandWithArgsDelegate::CreateRaw(this, &FUE2CodeModule::ExportNiagaraFunctionScriptCommand),
		ECVF_Default
	));

	ConsoleCommands.Add(ConsoleManager.RegisterConsoleCommand(
		TEXT("UE_Node2Code.ExportNiagaraModuleScript"),
		TEXT("Exports a Niagara Module Script asset to AI-readable text. Args: <ScriptAssetPathOrUAssetFile> <OutputFilePath> [NodeHierarchyDepth]"),
		FConsoleCommandWithArgsDelegate::CreateRaw(this, &FUE2CodeModule::ExportNiagaraModuleScriptCommand),
		ECVF_Default
	));

	ConsoleCommands.Add(ConsoleManager.RegisterConsoleCommand(
		TEXT("UE_Node2Code.ExportMaterialProperty"),
		TEXT("Exports one material property chain. Args: <MaterialAssetPathOrUAssetFile> <MaterialProperty> <OutputFilePath> [NodeHierarchyDepth]. Example property: MP_BaseColor"),
		FConsoleCommandWithArgsDelegate::CreateRaw(this, &FUE2CodeModule::ExportMaterialPropertyCommand),
		ECVF_Default
	));

	ConsoleCommands.Add(ConsoleManager.RegisterConsoleCommand(
		TEXT("UE_Node2Code.ExportMaterialNode"),
		TEXT("Exports one material expression by object name. Args: <MaterialAssetPathOrUAssetFile> <ExpressionObjectName> <OutputFilePath> [NodeHierarchyDepth]"),
		FConsoleCommandWithArgsDelegate::CreateRaw(this, &FUE2CodeModule::ExportMaterialNodeCommand),
		ECVF_Default
	));
}

void FUE2CodeModule::UnregisterConsoleCommands()
{
	IConsoleManager& ConsoleManager = IConsoleManager::Get();
	for (IConsoleObject* ConsoleCommand : ConsoleCommands)
	{
		if (ConsoleCommand)
		{
			ConsoleManager.UnregisterConsoleObject(ConsoleCommand);
		}
	}
	ConsoleCommands.Empty();
}

void FUE2CodeModule::ExportMaterialCommand(const TArray<FString>& Args)
{
	if (Args.Num() < 2)
	{
		UE_LOG(LogUE2Code, Error, TEXT("Usage: UE_Node2Code.ExportMaterial <MaterialAssetPathOrUAssetFile> <OutputFilePath> [NodeHierarchyDepth]"));
		return;
	}

	FString Error;
	FUE2CodeExportOptions Options;
	ApplyOptionalHierarchyDepth(Args, 2, Options);
	if (FUE2CodeMaterialExporter::ExportMaterialAssetPathToText(Args[0], Args[1], Options, Error))
	{
		UE_LOG(LogUE2Code, Display, TEXT("Exported material graph to %s"), *Args[1]);
	}
	else
	{
		UE_LOG(LogUE2Code, Error, TEXT("%s"), *Error);
	}
}

void FUE2CodeModule::ExportBlueprintCommand(const TArray<FString>& Args)
{
	if (Args.Num() < 2)
	{
		UE_LOG(LogUE2Code, Error, TEXT("Usage: UE_Node2Code.ExportBlueprint <BlueprintAssetPathOrUAssetFile> <OutputFilePath> [NodeHierarchyDepth]"));
		return;
	}
	FString Error;
	FUE2CodeExportOptions Options;
	ApplyOptionalHierarchyDepth(Args, 2, Options);
	if (FUE2CodeBlueprintExporter::ExportBlueprintAssetPathToText(Args[0], Args[1], Options, Error))
	{
		UE_LOG(LogUE2Code, Display, TEXT("Exported Blueprint graphs to %s"), *Args[1]);
	}
	else
	{
		UE_LOG(LogUE2Code, Error, TEXT("%s"), *Error);
	}
}

void FUE2CodeModule::ExportMaterialFunctionCommand(const TArray<FString>& Args)
{
	if (Args.Num() < 2)
	{
		UE_LOG(LogUE2Code, Error, TEXT("Usage: UE_Node2Code.ExportMaterialFunction <MaterialFunctionAssetPathOrUAssetFile> <OutputFilePath> [NodeHierarchyDepth]"));
		return;
	}

	FString Error;
	FUE2CodeExportOptions Options;
	ApplyOptionalHierarchyDepth(Args, 2, Options);
	if (FUE2CodeMaterialExporter::ExportMaterialFunctionAssetPathToText(Args[0], Args[1], Options, Error))
	{
		UE_LOG(LogUE2Code, Display, TEXT("Exported material function graph to %s"), *Args[1]);
	}
	else
	{
		UE_LOG(LogUE2Code, Error, TEXT("%s"), *Error);
	}
}

void FUE2CodeModule::ExportNiagaraFunctionScriptCommand(const TArray<FString>& Args)
{
	if (Args.Num() < 2)
	{
		UE_LOG(LogUE2Code, Error, TEXT("Usage: UE_Node2Code.ExportNiagaraFunctionScript <ScriptAssetPathOrUAssetFile> <OutputFilePath> [NodeHierarchyDepth]"));
		return;
	}

	FString Error;
	FUE2CodeExportOptions Options;
	ApplyOptionalHierarchyDepth(Args, 2, Options);
	if (FUE2CodeNiagaraExporter::ExportNiagaraFunctionScriptAssetPathToText(Args[0], Args[1], Options, Error))
	{
		UE_LOG(LogUE2Code, Display, TEXT("Exported Niagara Function Script graph to %s"), *Args[1]);
	}
	else
	{
		UE_LOG(LogUE2Code, Error, TEXT("%s"), *Error);
	}
}

void FUE2CodeModule::ExportNiagaraModuleScriptCommand(const TArray<FString>& Args)
{
	if (Args.Num() < 2)
	{
		UE_LOG(LogUE2Code, Error, TEXT("Usage: UE_Node2Code.ExportNiagaraModuleScript <ScriptAssetPathOrUAssetFile> <OutputFilePath> [NodeHierarchyDepth]"));
		return;
	}

	FString Error;
	FUE2CodeExportOptions Options;
	ApplyOptionalHierarchyDepth(Args, 2, Options);
	if (FUE2CodeNiagaraExporter::ExportNiagaraModuleScriptAssetPathToText(Args[0], Args[1], Options, Error))
	{
		UE_LOG(LogUE2Code, Display, TEXT("Exported Niagara Module Script graph to %s"), *Args[1]);
	}
	else
	{
		UE_LOG(LogUE2Code, Error, TEXT("%s"), *Error);
	}
}

void FUE2CodeModule::ExportMaterialPropertyCommand(const TArray<FString>& Args)
{
	if (Args.Num() < 3)
	{
		UE_LOG(LogUE2Code, Error, TEXT("Usage: UE_Node2Code.ExportMaterialProperty <MaterialAssetPathOrUAssetFile> <MaterialProperty> <OutputFilePath> [NodeHierarchyDepth]"));
		return;
	}

	EMaterialProperty MaterialProperty = MP_MAX;
	if (!FUE2CodeMaterialExporter::ParseMaterialProperty(Args[1], MaterialProperty))
	{
		UE_LOG(LogUE2Code, Error, TEXT("Could not parse material property '%s'."), *Args[1]);
		return;
	}

	const FString AssetPath = FUE2CodeMaterialExporter::NormalizeMaterialAssetPath(Args[0]);
	UMaterialInterface* Material = Cast<UMaterialInterface>(StaticLoadObject(UMaterialInterface::StaticClass(), nullptr, *AssetPath));
	if (!Material && !AssetPath.Contains(TEXT(".")))
	{
		const FString ObjectPath = AssetPath + TEXT(".") + FPackageName::GetShortName(AssetPath);
		Material = Cast<UMaterialInterface>(StaticLoadObject(UMaterialInterface::StaticClass(), nullptr, *ObjectPath));
	}
	if (!Material)
	{
		UE_LOG(LogUE2Code, Error, TEXT("Could not load material '%s'."), *AssetPath);
		return;
	}

	FString Error;
	FUE2CodeExportOptions Options;
	ApplyOptionalHierarchyDepth(Args, 3, Options);
	if (FUE2CodeMaterialExporter::ExportMaterialPropertyToText(Material, MaterialProperty, Args[2], Options, Error))
	{
		UE_LOG(LogUE2Code, Display, TEXT("Exported material property graph to %s"), *Args[2]);
	}
	else
	{
		UE_LOG(LogUE2Code, Error, TEXT("%s"), *Error);
	}
}

void FUE2CodeModule::ExportMaterialNodeCommand(const TArray<FString>& Args)
{
	if (Args.Num() < 3)
	{
		UE_LOG(LogUE2Code, Error, TEXT("Usage: UE_Node2Code.ExportMaterialNode <MaterialAssetPathOrUAssetFile> <ExpressionObjectName> <OutputFilePath> [NodeHierarchyDepth]"));
		return;
	}

	const FString AssetPath = FUE2CodeMaterialExporter::NormalizeMaterialAssetPath(Args[0]);
	UMaterialInterface* Material = Cast<UMaterialInterface>(StaticLoadObject(UMaterialInterface::StaticClass(), nullptr, *AssetPath));
	if (!Material && !AssetPath.Contains(TEXT(".")))
	{
		const FString ObjectPath = AssetPath + TEXT(".") + FPackageName::GetShortName(AssetPath);
		Material = Cast<UMaterialInterface>(StaticLoadObject(UMaterialInterface::StaticClass(), nullptr, *ObjectPath));
	}
	if (!Material)
	{
		UE_LOG(LogUE2Code, Error, TEXT("Could not load material '%s'."), *AssetPath);
		return;
	}

	FString Error;
	FUE2CodeExportOptions Options;
	ApplyOptionalHierarchyDepth(Args, 3, Options);
	if (FUE2CodeMaterialExporter::ExportMaterialNodeByNameToText(Material, Args[1], Args[2], Options, Error))
	{
		UE_LOG(LogUE2Code, Display, TEXT("Exported material node graph to %s"), *Args[2]);
	}
	else
	{
		UE_LOG(LogUE2Code, Error, TEXT("%s"), *Error);
	}
}

#undef LOCTEXT_NAMESPACE

IMPLEMENT_MODULE(FUE2CodeModule, UE_Node2Code)
