#include "UE_Node2Code.h"

#include "DesktopPlatformModule.h"
#include "Editor.h"
#include "Engine/Selection.h"
#include "Framework/Application/SlateApplication.h"
#include "Framework/Docking/TabManager.h"
#include "Framework/MultiBox/MultiBoxBuilder.h"
#include "HAL/IConsoleManager.h"
#include "IDesktopPlatform.h"
#include "LevelEditor.h"
#include "Materials/Material.h"
#include "Materials/MaterialFunctionInterface.h"
#include "Materials/MaterialInterface.h"
#include "Misc/MessageDialog.h"
#include "Misc/PackageName.h"
#include "Misc/Paths.h"
#include "UE2CodeMaterialExporter.h"
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

	class SUE2CodeExportWidget : public SCompoundWidget
	{
	public:
		SLATE_BEGIN_ARGS(SUE2CodeExportWidget) {}
		SLATE_END_ARGS()

		void Construct(const FArguments& InArgs)
		{
			ModeOptions.Add(MakeShared<FString>(TEXT("Material")));
			ModeOptions.Add(MakeShared<FString>(TEXT("Material Function")));
			ModeOptions.Add(MakeShared<FString>(TEXT("Material Property")));
			ModeOptions.Add(MakeShared<FString>(TEXT("Material Node")));
			SelectedMode = ModeOptions[0];

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
							.Text(LOCTEXT("ExportTitle", "UE Node2Code Material Export"))
						]
						+ SVerticalBox::Slot()
						.AutoHeight()
						.Padding(0, 3)
						[
							MakeTextBoxRow(
								LOCTEXT("MaterialAsset", "Material / Function"),
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
								.Text(FText::FromString(FPaths::ConvertRelativePathToFull(FPaths::ProjectDir() / TEXT("Codex/MaterialExport.ue2code.txt"))))
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
								.ToolTipText(LOCTEXT("HierarchyDepthTip", "0 expands functions until basic nodes. 1 exports only the current graph. 2 expands first-level functions. Higher natural numbers expand deeper nested functions."))
							)
						]
						+ SVerticalBox::Slot()
						.AutoHeight()
						.Padding(0, 8, 0, 0)
						[
							MakeCheckBoxRow(LOCTEXT("ExportUnreferenced", "Export Unreferenced Material Nodes"), bExportUnreferenced)
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
			return SelectedMode == ModeOptions[2] ? EVisibility::Visible : EVisibility::Collapsed;
		}

		EVisibility GetNodeVisibility() const
		{
			return SelectedMode == ModeOptions[3] ? EVisibility::Visible : EVisibility::Collapsed;
		}

		FReply UseSelectedAsset()
		{
			if (!GEditor || !GEditor->GetSelectedObjects())
			{
				SetStatus(TEXT("No selected material or material function."));
				return FReply::Handled();
			}

			TArray<UObject*> SelectedObjects;
			GEditor->GetSelectedObjects()->GetSelectedObjects(SelectedObjects);
			for (UObject* Object : SelectedObjects)
			{
				if (UMaterialInterface* Material = Cast<UMaterialInterface>(Object))
				{
					MaterialPathTextBox->SetText(FText::FromString(Material->GetPathName()));
					const FString ShortName = Material->GetName();
					OutputPathTextBox->SetText(FText::FromString(FPaths::ConvertRelativePathToFull(FPaths::ProjectDir() / FString::Printf(TEXT("Codex/%s.ue2code.txt"), *ShortName))));
					SetStatus(TEXT("Selected material applied."));
					return FReply::Handled();
				}

				if (UMaterialFunctionInterface* MaterialFunction = Cast<UMaterialFunctionInterface>(Object))
				{
					MaterialPathTextBox->SetText(FText::FromString(MaterialFunction->GetPathName()));
					const FString ShortName = MaterialFunction->GetName();
					OutputPathTextBox->SetText(FText::FromString(FPaths::ConvertRelativePathToFull(FPaths::ProjectDir() / FString::Printf(TEXT("Codex/%s.ue2code.txt"), *ShortName))));
					SelectedMode = ModeOptions[1];
					if (ModeComboBox.IsValid())
					{
						ModeComboBox->SetSelectedItem(SelectedMode);
					}
					SetStatus(TEXT("Selected material function applied."));
					return FReply::Handled();
				}
			}

			SetStatus(TEXT("Selection does not contain a material or material function."));
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

			FString Error;
			bool bSuccess = false;
			if (SelectedMode == ModeOptions[0])
			{
				bSuccess = FUE2CodeMaterialExporter::ExportMaterialAssetPathToText(MaterialPath, OutputPath, Options, Error);
			}
			else if (SelectedMode == ModeOptions[1])
			{
				bSuccess = FUE2CodeMaterialExporter::ExportMaterialFunctionAssetPathToText(MaterialPath, OutputPath, Options, Error);
			}
			else
			{
				UMaterialInterface* Material = LoadMaterialForExport(MaterialPath, Error);
				if (Material)
				{
					if (SelectedMode == ModeOptions[2])
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
				SetStatus(FString::Printf(TEXT("Exported to %s"), *OutputPath));
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
		bool bExportUnreferenced = true;
		bool bIncludeDebugMetadata = false;
		bool bIncludeDefaultLikeProperties = false;
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

	FLevelEditorModule& LevelEditorModule = FModuleManager::LoadModuleChecked<FLevelEditorModule>(TEXT("LevelEditor"));
	MenuExtender = MakeShareable(new FExtender());
	MenuExtender->AddMenuExtension(
		TEXT("WindowLayout"),
		EExtensionHook::After,
		nullptr,
		FMenuExtensionDelegate::CreateRaw(this, &FUE2CodeModule::AddWindowMenuEntry)
	);
	LevelEditorModule.GetMenuExtensibilityManager()->AddExtender(MenuExtender);
}

void FUE2CodeModule::UnregisterGui()
{
	if (IsRunningCommandlet())
	{
		return;
	}

	if (MenuExtender.IsValid() && FModuleManager::Get().IsModuleLoaded(TEXT("LevelEditor")))
	{
		FLevelEditorModule& LevelEditorModule = FModuleManager::GetModuleChecked<FLevelEditorModule>(TEXT("LevelEditor"));
		LevelEditorModule.GetMenuExtensibilityManager()->RemoveExtender(MenuExtender);
		MenuExtender.Reset();
	}

	FGlobalTabmanager::Get()->UnregisterNomadTabSpawner(UE_Node2CodeExportTabName);
}

void FUE2CodeModule::AddWindowMenuEntry(FMenuBuilder& MenuBuilder)
{
	MenuBuilder.AddMenuEntry(
		LOCTEXT("OpenUE_Node2Code", "UE Node2Code"),
		LOCTEXT("OpenUE_Node2CodeTooltip", "Open the UE Node2Code material and material function export window."),
		FSlateIcon(),
		FUIAction(FExecuteAction::CreateRaw(this, &FUE2CodeModule::OpenExportWindow))
	);
}

void FUE2CodeModule::OpenExportWindow()
{
	FGlobalTabmanager::Get()->TryInvokeTab(UE_Node2CodeExportTabName);
}

TSharedRef<SDockTab> FUE2CodeModule::SpawnExportTab(const FSpawnTabArgs& SpawnTabArgs)
{
	return SNew(SDockTab)
		.TabRole(ETabRole::NomadTab)
		[
			SNew(SUE2CodeExportWidget)
		];
}

void FUE2CodeModule::RegisterConsoleCommands()
{
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
