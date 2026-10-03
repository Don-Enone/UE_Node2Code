#include "UE2CodeNiagaraExporter.h"

#include "EdGraph/EdGraph.h"
#include "EdGraph/EdGraphNode.h"
#include "EdGraph/EdGraphPin.h"
#include "HAL/FileManager.h"
#include "Misc/EngineVersion.h"
#include "Misc/FileHelper.h"
#include "Misc/PackageName.h"
#include "Misc/Paths.h"
#include "NiagaraGraph.h"
#include "NiagaraNode.h"
#include "NiagaraNodeFunctionCall.h"
#include "NiagaraNodeInput.h"
#include "NiagaraNodeOp.h"
#include "NiagaraNodeOutput.h"
#include "NiagaraScript.h"
#include "NiagaraScriptSource.h"
#include "Runtime/Launch/Resources/Version.h"
#include "UE2CodeEngineCompat.h"
#include "UE2CodeSelection.h"
#include "UE2CodeTextFormat.h"
#include "UObject/UnrealType.h"

#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#endif

namespace UE2CodeNiagaraExporterPrivate
{
	static FString Indent(int32 Depth)
	{
		FString Result;
		for (int32 Index = 0; Index < Depth; ++Index)
		{
			Result += TEXT("  ");
		}
		return Result;
	}

	static void AppendLine(FString& OutText, int32 Depth, const FString& Line)
	{
		OutText += Indent(Depth);
		OutText += Line;
		OutText += LINE_TERMINATOR;
	}

	static FString Escape(FString Value)
	{
		return UE2CodeTextFormat::Escape(MoveTemp(Value));
	}

	static FString Quote(const FString& Value)
	{
		return UE2CodeTextFormat::Quote(Value);
	}

	template <typename EnumType>
	static FString EnumName(EnumType Value)
	{
		if (const UEnum* Enum = StaticEnum<EnumType>())
		{
			return Enum->GetNameStringByValue(static_cast<int64>(Value));
		}
		return FString::FromInt(static_cast<int32>(Value));
	}

	static FString NodeTypeName(const UEdGraphNode* Node)
	{
		FString Name = Node ? Node->GetClass()->GetName() : TEXT("None");
		if (Name.StartsWith(TEXT("NiagaraNode")))
		{
			Name.RightChopInline(11);
		}
		return Name;
	}

	static FString PinInternalName(const UEdGraphPin* Pin)
	{
		if (!Pin)
		{
			return TEXT("None");
		}
		const FString Name = Pin->PinName.ToString();
		return Name.IsEmpty() ? TEXT("<unnamed>") : Name;
	}

	static FString PinDisplayName(const UEdGraphPin* Pin)
	{
		if (!Pin)
		{
			return FString();
		}
		const FString FriendlyName = Pin->PinFriendlyName.ToString();
		return FriendlyName.IsEmpty() ? Pin->PinName.ToString() : FriendlyName;
	}

	static FString PinTypeName(const UEdGraphPin* Pin)
	{
		if (!Pin)
		{
			return TEXT("unknown");
		}

		FString Name;
		if (const UObject* TypeObject = Pin->PinType.PinSubCategoryObject.Get())
		{
			Name = TypeObject->GetName();
			if (Name.StartsWith(TEXT("Niagara")) && Name.Len() > 7)
			{
				Name.RightChopInline(7);
			}
		}
		else if (!Pin->PinType.PinSubCategory.IsNone())
		{
			Name = Pin->PinType.PinSubCategory.ToString();
		}
		else
		{
			Name = Pin->PinType.PinCategory.ToString();
		}
		return Name.IsEmpty() ? TEXT("unknown") : Name;
	}

	static bool IsAddPin(const UEdGraphPin* Pin)
	{
		return Pin
			&& Pin->PinType.PinCategory.ToString().Equals(TEXT("misc"), ESearchCase::IgnoreCase)
			&& Pin->PinType.PinSubCategory.ToString().Contains(TEXT("add"), ESearchCase::IgnoreCase);
	}

	static FString PinDefaultValue(const UEdGraphPin* Pin)
	{
		if (!Pin)
		{
			return FString();
		}
		if (Pin->DefaultObject)
		{
			return Pin->DefaultObject->GetName();
		}
		if (!Pin->DefaultValue.IsEmpty())
		{
			return Pin->DefaultValue;
		}
		if (!Pin->DefaultTextValue.IsEmpty())
		{
			return Pin->DefaultTextValue.ToString();
		}
		return FString();
	}

	static FString VariableTypeName(const FNiagaraVariable& Variable)
	{
		FString Name = Variable.GetType().GetName();
		if (Name.StartsWith(TEXT("Niagara")) && Name.Len() > 7)
		{
			Name.RightChopInline(7);
		}
		return Name;
	}

	static FString VariableDefaultValue(const FNiagaraVariable& Variable)
	{
		if (!Variable.IsDataAllocated())
		{
			return FString();
		}

		FString Value = Variable.ToString();
		const FString Prefix = Variable.GetName().ToString() + TEXT("(");
		if (Value.StartsWith(Prefix) && Value.EndsWith(TEXT(")")))
		{
			Value = Value.Mid(Prefix.Len(), Value.Len() - Prefix.Len() - 1);
		}
		Value.TrimStartAndEndInline();
		return Value;
	}

	static UNiagaraScriptSource* GetScriptSource(UNiagaraScript* Script)
	{
#if WITH_EDITORONLY_DATA
		if (!Script)
		{
			return nullptr;
		}
#if ENGINE_MAJOR_VERSION >= 5
		return Cast<UNiagaraScriptSource>(Script->GetLatestSource());
#else
		return Cast<UNiagaraScriptSource>(Script->GetSource());
#endif
#else
		return nullptr;
#endif
	}

	static UNiagaraScript* LoadScript(const FString& InputPath, FString& OutNormalizedPath)
	{
		FString CandidatePath = InputPath.TrimStartAndEnd();
		FPaths::NormalizeFilename(CandidatePath);
		if (CandidatePath.EndsWith(TEXT(".uasset"), ESearchCase::IgnoreCase))
		{
			const FString FullPath = FPaths::ConvertRelativePathToFull(CandidatePath);
			if (!FPackageName::TryConvertFilenameToLongPackageName(FullPath, OutNormalizedPath))
			{
				OutNormalizedPath = FUE2CodeMaterialExporter::NormalizeMaterialAssetPath(CandidatePath);
			}
		}
		else
		{
			OutNormalizedPath = FUE2CodeMaterialExporter::NormalizeMaterialAssetPath(CandidatePath);
		}
		UNiagaraScript* Script = Cast<UNiagaraScript>(StaticLoadObject(UNiagaraScript::StaticClass(), nullptr, *OutNormalizedPath));
		if (!Script && !OutNormalizedPath.Contains(TEXT(".")))
		{
			const FString ObjectPath = OutNormalizedPath + TEXT(".") + FPackageName::GetShortName(OutNormalizedPath);
			Script = Cast<UNiagaraScript>(StaticLoadObject(UNiagaraScript::StaticClass(), nullptr, *ObjectPath));
		}
		return Script;
	}

	static bool SaveTextToFile(const FString& OutputFilePath, const FString& Text, FString& OutError)
	{
		if (OutputFilePath.IsEmpty())
		{
			OutError = TEXT("OutputFilePath is empty.");
			return false;
		}

		const FString OutputDirectory = FPaths::GetPath(OutputFilePath);
		if (!OutputDirectory.IsEmpty())
		{
			IFileManager::Get().MakeDirectory(*OutputDirectory, true);
		}
		if (!FFileHelper::SaveStringToFile(Text, *OutputFilePath, FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM))
		{
			OutError = FString::Printf(TEXT("Failed to save text to '%s'."), *OutputFilePath);
			return false;
		}
		return true;
	}

	static void SortNodes(TArray<UEdGraphNode*>& Nodes)
	{
		Nodes.Sort([](const UEdGraphNode& A, const UEdGraphNode& B)
		{
			if (A.NodeGuid.IsValid() && B.NodeGuid.IsValid() && A.NodeGuid != B.NodeGuid)
			{
				return A.NodeGuid < B.NodeGuid;
			}
			return A.GetName() < B.GetName();
		});
	}

	static UNiagaraGraph* GetScriptGraph(UNiagaraScript* Script)
	{
		UNiagaraScriptSource* Source = GetScriptSource(Script);
		return Source ? Source->NodeGraph : nullptr;
	}

	// A selection, when given, keeps only the selected nodes of this graph.
	static void GetSortedNodes(UNiagaraGraph* Graph, TArray<UEdGraphNode*>& OutNodes, const UE2CodeSelection::FNodeSelection* Selection = nullptr)
	{
		OutNodes.Reset();
		if (!Graph)
		{
			return;
		}
		for (UEdGraphNode* Node : Graph->Nodes)
		{
			if (Node && (!Selection || Selection->Contains(Node, Node->NodeGuid)))
			{
				OutNodes.Add(Node);
			}
		}
		SortNodes(OutNodes);
	}

	struct FCallResolution
	{
		FString Reference;
		FString Reason;
	};

	struct FNiagaraExportContext
	{
		explicit FNiagaraExportContext(const FUE2CodeExportOptions& InOptions, UNiagaraScript* InRootScript)
			: Options(InOptions)
			, RootScript(InRootScript)
		{
			if (RootScript)
			{
				ScriptIds.Add(RootScript, TEXT("ROOT"));
				ScriptStack.Add(RootScript);
			}
		}

		const FUE2CodeExportOptions& Options;
		UNiagaraScript* RootScript = nullptr;
		TMap<const UNiagaraScript*, FString> ScriptIds;
		TArray<UNiagaraScript*> ScriptDefinitions;
		TArray<const UNiagaraScript*> ScriptStack;
		TMap<const UNiagaraNodeFunctionCall*, FCallResolution> CallResolutions;
		int32 NextScriptId = 1;
	};

	static void RegisterCalledScripts(UNiagaraScript* Script, FNiagaraExportContext& Context, const UE2CodeSelection::FNodeSelection* Selection = nullptr)
	{
		UNiagaraGraph* Graph = GetScriptGraph(Script);
		TArray<UEdGraphNode*> Nodes;
		GetSortedNodes(Graph, Nodes, Selection);

		for (UEdGraphNode* Node : Nodes)
		{
			const UNiagaraNodeFunctionCall* FunctionCall = Cast<UNiagaraNodeFunctionCall>(Node);
			if (!FunctionCall)
			{
				continue;
			}

			FCallResolution Resolution;
			UNiagaraScript* CalledScript = FunctionCall->FunctionScript;
			if (!CalledScript)
			{
				Resolution.Reason = TEXT("missing_script");
				Context.CallResolutions.Add(FunctionCall, MoveTemp(Resolution));
				continue;
			}

			if (const FString* ExistingId = Context.ScriptIds.Find(CalledScript))
			{
				Resolution.Reference = *ExistingId;
				if (Context.ScriptStack.Contains(CalledScript))
				{
					Resolution.Reason = TEXT("recursive_call");
				}
				Context.CallResolutions.Add(FunctionCall, MoveTemp(Resolution));
				continue;
			}

			const int32 TargetLayer = Context.ScriptStack.Num() + 1;
			if (Context.Options.NodeHierarchyDepth > 0 && TargetLayer > Context.Options.NodeHierarchyDepth)
			{
				Resolution.Reason = TEXT("depth_limit");
				Context.CallResolutions.Add(FunctionCall, MoveTemp(Resolution));
				continue;
			}
			if (Context.Options.NodeHierarchyDepth == 0 && TargetLayer > FMath::Max(1, Context.Options.MaxFunctionDepth))
			{
				Resolution.Reason = TEXT("safety_limit");
				Context.CallResolutions.Add(FunctionCall, MoveTemp(Resolution));
				continue;
			}
			if (!GetScriptGraph(CalledScript))
			{
				Resolution.Reason = TEXT("unreadable_graph");
				Context.CallResolutions.Add(FunctionCall, MoveTemp(Resolution));
				continue;
			}

			Resolution.Reference = FString::Printf(TEXT("NS%03d"), Context.NextScriptId++);
			Context.ScriptIds.Add(CalledScript, Resolution.Reference);
			Context.ScriptDefinitions.Add(CalledScript);
			Context.CallResolutions.Add(FunctionCall, Resolution);

			Context.ScriptStack.Add(CalledScript);
			RegisterCalledScripts(CalledScript, Context);
			Context.ScriptStack.Pop();
		}
	}

	static bool IsSemanticNiagaraProperty(const FProperty* Property)
	{
		if (!Property)
		{
			return false;
		}

		static const FName SemanticNames[] =
		{
			TEXT("Connections"), TEXT("InputParameterName"), TEXT("SwitchTypeData"), TEXT("OutputVars"),
			TEXT("Signature"), TEXT("FunctionSpecifiers"), TEXT("PropagatedStaticSwitchParameters")
		};
		for (const FName SemanticName : SemanticNames)
		{
			if (Property->GetFName() == SemanticName)
			{
				return true;
			}
		}
		return false;
	}

	static bool IsSupportedScalarProperty(const FProperty* Property)
	{
		return CastField<FBoolProperty>(Property)
			|| CastField<FNumericProperty>(Property)
			|| CastField<FNameProperty>(Property)
			|| CastField<FStrProperty>(Property)
			|| CastField<FTextProperty>(Property)
			|| CastField<FEnumProperty>(Property)
			|| CastField<FByteProperty>(Property)
			|| CastField<FObjectPropertyBase>(Property);
	}

	static bool ShouldSkipNodeProperty(const FProperty* Property)
	{
		if (!Property || Property->HasAnyPropertyFlags(CPF_Transient | CPF_Deprecated))
		{
			return true;
		}

		static const FName SkippedNames[] =
		{
			TEXT("FunctionScript"), TEXT("FunctionDisplayName"), TEXT("Input"), TEXT("Usage"),
			TEXT("CallSortPriority"), TEXT("ExposureOptions"), TEXT("Outputs"), TEXT("ScriptType"),
			TEXT("OpName")
		};
		for (const FName SkippedName : SkippedNames)
		{
			if (Property->GetFName() == SkippedName)
			{
				return true;
			}
		}

		return !IsSemanticNiagaraProperty(Property)
			&& (!Property->HasAnyPropertyFlags(CPF_Edit) || !IsSupportedScalarProperty(Property));
	}

	static void RemoveSerializedField(FString& Value, const FString& FieldName)
	{
		const FString Prefix = FieldName + TEXT("=");
		int32 SearchFrom = 0;
		while (SearchFrom < Value.Len())
		{
			const int32 FieldStart = Value.Find(Prefix, ESearchCase::CaseSensitive, ESearchDir::FromStart, SearchFrom);
			if (FieldStart == INDEX_NONE)
			{
				break;
			}

			const int32 CommaIndex = Value.Find(TEXT(","), ESearchCase::CaseSensitive, ESearchDir::FromStart, FieldStart + Prefix.Len());
			if (CommaIndex == INDEX_NONE)
			{
				break;
			}
			Value.RemoveAt(FieldStart, CommaIndex - FieldStart + 1);
			SearchFrom = FieldStart;
		}
	}

	static FString CompactSemanticPropertyValue(const FProperty* Property, FString Value)
	{
		if (Property && Property->GetFName() == FName(TEXT("Connections")))
		{
			RemoveSerializedField(Value, TEXT("SourcePinId"));
			RemoveSerializedField(Value, TEXT("DestinationPinId"));
			Value.ReplaceInline(TEXT("SourcePath="), TEXT("from="));
			Value.ReplaceInline(TEXT("DestinationPath="), TEXT("to="));
		}
		return Value;
	}

	static void AppendEditableProperties(UNiagaraNode* Node, FString& OutText, int32 Depth, const FUE2CodeExportOptions& Options)
	{
		if (!Node)
		{
			return;
		}

		FString PropertyText;
		int32 PropertyCount = 0;
		const UObject* Defaults = Node->GetClass()->GetDefaultObject();
		for (const UClass* Class = Node->GetClass(); Class && Class != UNiagaraNode::StaticClass(); Class = Class->GetSuperClass())
		{
			for (TFieldIterator<FProperty> It(Class, EFieldIteratorFlags::ExcludeSuper); It; ++It)
			{
				const FProperty* Property = *It;
				if (ShouldSkipNodeProperty(Property)
					|| (!Options.bIncludeDefaultLikeProperties && Defaults && Property->Identical_InContainer(Node, Defaults)))
				{
					continue;
				}

				const void* ValuePtr = Property->ContainerPtrToValuePtr<const void>(Node);
				FString Value;
				if (const FObjectPropertyBase* ObjectProperty = CastField<FObjectPropertyBase>(Property))
				{
					const UObject* Object = ObjectProperty->GetObjectPropertyValue(ValuePtr);
					Value = Object ? Object->GetName() : TEXT("None");
				}
				else
				{
					UE2CodeEngineCompat::ExportPropertyText(Property, Value, ValuePtr, Node);
				}
				Value = CompactSemanticPropertyValue(Property, MoveTemp(Value));
				if (!Options.bIncludeDefaultLikeProperties && (Value.IsEmpty() || Value == TEXT("None") || Value == TEXT("()")))
				{
					continue;
				}

				AppendLine(PropertyText, Depth + 1, FString::Printf(TEXT("- %s = %s"), *Property->GetName(), *Quote(Value)));
				++PropertyCount;
			}
		}

		if (PropertyCount > 0)
		{
			AppendLine(OutText, Depth, TEXT("properties:"));
			OutText += PropertyText;
		}
	}

	static void AppendScriptSignature(const TArray<UEdGraphNode*>& Nodes, ENiagaraScriptUsage ScriptUsage, FString& OutText, int32 Depth)
	{
		TArray<UNiagaraNodeInput*> Inputs;
		TArray<UNiagaraNodeOutput*> Outputs;
		for (UEdGraphNode* Node : Nodes)
		{
			if (UNiagaraNodeInput* Input = Cast<UNiagaraNodeInput>(Node))
			{
				if (Input->Usage == ENiagaraInputNodeUsage::Parameter && Input->ExposureOptions.bExposed)
				{
					Inputs.Add(Input);
				}
			}
			else if (UNiagaraNodeOutput* Output = Cast<UNiagaraNodeOutput>(Node))
			{
				if (Output->GetUsage() == ScriptUsage)
				{
					Outputs.Add(Output);
				}
			}
		}

		Inputs.Sort([](const UNiagaraNodeInput& A, const UNiagaraNodeInput& B)
		{
			return A.CallSortPriority == B.CallSortPriority
				? A.Input.GetName().LexicalLess(B.Input.GetName())
				: A.CallSortPriority < B.CallSortPriority;
		});

		AppendLine(OutText, Depth, TEXT("signature:"));
		AppendLine(OutText, Depth + 1, Inputs.Num() > 0 ? TEXT("inputs:") : TEXT("inputs: none"));
		for (const UNiagaraNodeInput* Input : Inputs)
		{
			FString Line = FString::Printf(
				TEXT("- %s : %s"),
				*Quote(Input->Input.GetName().ToString()),
				*VariableTypeName(Input->Input));
			const FString DefaultValue = VariableDefaultValue(Input->Input);
			if (!DefaultValue.IsEmpty())
			{
				Line += TEXT(" default=") + Quote(DefaultValue);
			}
			if (Input->ExposureOptions.bRequired)
			{
				Line += TEXT(" required");
			}
			if (Input->ExposureOptions.bCanAutoBind)
			{
				Line += TEXT(" auto_bind");
			}
			if (Input->ExposureOptions.bHidden)
			{
				Line += TEXT(" hidden");
			}
			if (Input->CallSortPriority != 0)
			{
				Line += FString::Printf(TEXT(" sort=%d"), Input->CallSortPriority);
			}
			AppendLine(OutText, Depth + 2, Line);
		}

		int32 OutputCount = 0;
		for (const UNiagaraNodeOutput* Output : Outputs)
		{
			OutputCount += Output->GetOutputs().Num();
		}
		AppendLine(OutText, Depth + 1, OutputCount > 0 ? TEXT("outputs:") : TEXT("outputs: none"));
		for (const UNiagaraNodeOutput* Output : Outputs)
		{
			for (const FNiagaraVariable& Variable : Output->GetOutputs())
			{
				AppendLine(OutText, Depth + 2, FString::Printf(TEXT("- %s : %s"), *Quote(Variable.GetName().ToString()), *VariableTypeName(Variable)));
			}
		}
	}

	struct FConnection
	{
		FString FromNode;
		FString FromPin;
		FString FromPinId;
		FString ToNode;
		FString ToPin;
		FString ToPinId;
	};

	struct FGraphExportData
	{
		TArray<UEdGraphNode*> Nodes;
		TMap<const UEdGraphNode*, FString> NodeIds;
		TMap<const UEdGraphPin*, FString> PinIds;
		TArray<FConnection> Connections;
		// Selection exports also list links that cross the selection boundary.
		bool bSelection = false;
	};

	// An endpoint outside the exported selection has a node title and no pin ID.
	static FString UnselectedNode(const UEdGraphNode* Node)
	{
		return TEXT("unselected:") + Quote(Node->GetNodeTitle(ENodeTitleType::ListView).ToString());
	}

	static void GatherConnections(const FGraphExportData& Data, TArray<FConnection>& OutConnections)
	{
		for (const UEdGraphNode* Node : Data.Nodes)
		{
			for (const UEdGraphPin* Pin : Node->Pins)
			{
				if (!Pin || IsAddPin(Pin))
				{
					continue;
				}
				if (Pin->Direction == EGPD_Input)
				{
					if (!Data.bSelection || !Data.PinIds.Contains(Pin))
					{
						continue;
					}
					for (const UEdGraphPin* LinkedPin : Pin->LinkedTo)
					{
						const UEdGraphNode* LinkedNode = LinkedPin ? LinkedPin->GetOwningNode() : nullptr;
						if (!LinkedNode || Data.NodeIds.Contains(LinkedNode))
						{
							continue;
						}
						FConnection& Connection = OutConnections.AddDefaulted_GetRef();
						Connection.FromNode = UnselectedNode(LinkedNode);
						Connection.FromPin = PinInternalName(LinkedPin);
						Connection.ToNode = Data.NodeIds.FindChecked(Node);
						Connection.ToPin = PinInternalName(Pin);
						Connection.ToPinId = Data.PinIds.FindChecked(Pin);
					}
					continue;
				}
				for (const UEdGraphPin* LinkedPin : Pin->LinkedTo)
				{
					const UEdGraphNode* LinkedNode = LinkedPin ? LinkedPin->GetOwningNode() : nullptr;
					if (!LinkedPin || LinkedPin->Direction != EGPD_Input)
					{
						continue;
					}
					if (!Data.NodeIds.Contains(LinkedNode))
					{
						if (Data.bSelection && LinkedNode && Data.PinIds.Contains(Pin))
						{
							FConnection& Connection = OutConnections.AddDefaulted_GetRef();
							Connection.FromNode = Data.NodeIds.FindChecked(Node);
							Connection.FromPin = PinInternalName(Pin);
							Connection.FromPinId = Data.PinIds.FindChecked(Pin);
							Connection.ToNode = UnselectedNode(LinkedNode);
							Connection.ToPin = PinInternalName(LinkedPin);
						}
						continue;
					}
					const FString* FromPinId = Data.PinIds.Find(Pin);
					const FString* ToPinId = Data.PinIds.Find(LinkedPin);
					if (!FromPinId || !ToPinId)
					{
						continue;
					}

					FConnection& Connection = OutConnections.AddDefaulted_GetRef();
					Connection.FromNode = Data.NodeIds.FindChecked(Node);
					Connection.FromPin = PinInternalName(Pin);
					Connection.FromPinId = *FromPinId;
					Connection.ToNode = Data.NodeIds.FindChecked(LinkedNode);
					Connection.ToPin = PinInternalName(LinkedPin);
					Connection.ToPinId = *ToPinId;
				}
			}
		}

		OutConnections.Sort([](const FConnection& A, const FConnection& B)
		{
			const FString AKey = A.FromNode + TEXT("|") + A.FromPinId + TEXT("|") + A.ToNode + TEXT("|") + A.ToPinId;
			const FString BKey = B.FromNode + TEXT("|") + B.FromPinId + TEXT("|") + B.ToNode + TEXT("|") + B.ToPinId;
			return AKey < BKey;
		});
	}

	static void BuildGraphExportData(UNiagaraGraph* Graph, FGraphExportData& OutData, const UE2CodeSelection::FNodeSelection* Selection = nullptr)
	{
		GetSortedNodes(Graph, OutData.Nodes, Selection);
		OutData.bSelection = Selection != nullptr;
		for (int32 NodeIndex = 0; NodeIndex < OutData.Nodes.Num(); ++NodeIndex)
		{
			const UEdGraphNode* Node = OutData.Nodes[NodeIndex];
			OutData.NodeIds.Add(Node, FString::Printf(TEXT("N%03d"), NodeIndex + 1));
		}

		int32 PinIndex = 1;
		for (const UEdGraphNode* Node : OutData.Nodes)
		{
			for (const UEdGraphPin* Pin : Node->Pins)
			{
				if (Pin && !IsAddPin(Pin))
				{
					OutData.PinIds.Add(Pin, FString::Printf(TEXT("P%03d"), PinIndex++));
				}
			}
		}
		GatherConnections(OutData, OutData.Connections);
	}

	static void AppendNode(
		const UEdGraphNode* Node,
		const FGraphExportData& Data,
		FString& OutText,
		int32 Depth,
		FNiagaraExportContext& Context)
	{
		if (!Node)
		{
			return;
		}

		const FString& NodeId = Data.NodeIds.FindChecked(Node);
		AppendLine(
			OutText,
			Depth,
			FString::Printf(
				TEXT("node %s type=%s title=%s"),
				*NodeId,
				*NodeTypeName(Node),
				*Quote(Node->GetNodeTitle(ENodeTitleType::ListView).ToString())));
		if (Node->GetDesiredEnabledState() != ENodeEnabledState::Enabled)
		{
			AppendLine(OutText, Depth + 1, FString::Printf(TEXT("state: %s"), LexToString(Node->GetDesiredEnabledState())));
		}
		if (!Node->NodeComment.IsEmpty())
		{
			AppendLine(OutText, Depth + 1, FString::Printf(TEXT("comment: %s"), *Quote(Node->NodeComment)));
		}
		if (Context.Options.bIncludeDebugMetadata)
		{
			AppendLine(OutText, Depth + 1, FString::Printf(TEXT("debug: position=(%d,%d) object=%s guid=%s"), Node->NodePosX, Node->NodePosY, *Quote(Node->GetName()), *Node->NodeGuid.ToString(EGuidFormats::DigitsWithHyphens)));
		}

		if (const UNiagaraNodeInput* Input = Cast<UNiagaraNodeInput>(Node))
		{
			FString Line = FString::Printf(
				TEXT("input: %s : %s usage=%s"),
				*Quote(Input->Input.GetName().ToString()),
				*VariableTypeName(Input->Input),
				*EnumName(Input->Usage));
			const FString DefaultValue = VariableDefaultValue(Input->Input);
			if (!DefaultValue.IsEmpty())
			{
				Line += TEXT(" default=") + Quote(DefaultValue);
			}
			if (Input->ExposureOptions.bExposed)
			{
				Line += TEXT(" exposed");
			}
			if (Input->ExposureOptions.bRequired)
			{
				Line += TEXT(" required");
			}
			AppendLine(OutText, Depth + 1, Line);
		}
		else if (const UNiagaraNodeOutput* Output = Cast<UNiagaraNodeOutput>(Node))
		{
			AppendLine(OutText, Depth + 1, FString::Printf(TEXT("output: usage=%s values=%d"), *EnumName(Output->GetUsage()), Output->GetOutputs().Num()));
		}

		if (const UNiagaraNodeOp* Operation = Cast<UNiagaraNodeOp>(Node))
		{
			AppendLine(OutText, Depth + 1, FString::Printf(TEXT("operation: %s"), *Quote(Operation->OpName.ToString())));
		}
		if (const UNiagaraNodeFunctionCall* FunctionCall = Cast<UNiagaraNodeFunctionCall>(Node))
		{
			const FString FunctionName = FunctionCall->GetFunctionName();
			FString Line = TEXT("call:");
			Line += TEXT(" name=") + Quote(FunctionName.IsEmpty() && FunctionCall->FunctionScript ? FunctionCall->FunctionScript->GetName() : FunctionName);
			if (FunctionCall->FunctionScript)
			{
				Line += TEXT(" usage=") + EnumName(FunctionCall->FunctionScript->GetUsage());
			}
			const FCallResolution* Resolution = Context.CallResolutions.Find(FunctionCall);
			if (Resolution && !Resolution->Reference.IsEmpty())
			{
				Line += TEXT(" ref=") + Resolution->Reference;
			}
			else
			{
				Line += TEXT(" ref=external");
			}
			if (Resolution && !Resolution->Reason.IsEmpty())
			{
				Line += TEXT(" reason=") + Resolution->Reason;
			}
			AppendLine(OutText, Depth + 1, Line);
			if (FunctionCall->FunctionScript && Context.Options.bIncludeDebugMetadata)
			{
				AppendLine(OutText, Depth + 1, FString::Printf(TEXT("called_script_path: %s"), *Quote(FunctionCall->FunctionScript->GetPathName())));
			#if ENGINE_MAJOR_VERSION >= 5
				if (FunctionCall->SelectedScriptVersion.IsValid())
				{
					AppendLine(OutText, Depth + 1, FString::Printf(TEXT("called_script_version: %s"), *FunctionCall->SelectedScriptVersion.ToString(EGuidFormats::DigitsWithHyphens)));
				}
			#endif
			}
			else if (!FunctionCall->FunctionScriptAssetObjectPath.IsNone() && Context.Options.bIncludeDebugMetadata)
			{
				AppendLine(OutText, Depth + 1, FString::Printf(TEXT("called_script_path: %s"), *Quote(FunctionCall->FunctionScriptAssetObjectPath.ToString())));
			}
		}

		AppendEditableProperties(Cast<UNiagaraNode>(const_cast<UEdGraphNode*>(Node)), OutText, Depth + 1, Context.Options);

		int32 PinCount = 0;
		FString PinText;
		for (const UEdGraphPin* Pin : Node->Pins)
		{
			if (!Pin || IsAddPin(Pin))
			{
				continue;
			}
			const FString* PinId = Data.PinIds.Find(Pin);
			if (!PinId)
			{
				continue;
			}
			FString Line = FString::Printf(
				TEXT("- %s %s %s : %s"),
				**PinId,
				Pin->Direction == EGPD_Input ? TEXT("in") : TEXT("out"),
				*Quote(PinInternalName(Pin)),
				*PinTypeName(Pin));
			if (Pin->PinType.IsArray())
			{
				Line += TEXT(" container=array");
			}
			else if (Pin->PinType.IsSet())
			{
				Line += TEXT(" container=set");
			}
			else if (Pin->PinType.IsMap())
			{
				Line += TEXT(" container=map");
			}
			if (Pin->PinType.bIsReference)
			{
				Line += TEXT(" ref=true");
			}
			if (Pin->PinType.bIsConst)
			{
				Line += TEXT(" const=true");
			}
			const FString DisplayName = PinDisplayName(Pin);
			if (!DisplayName.IsEmpty() && DisplayName != PinInternalName(Pin))
			{
				Line += TEXT(" display_name=") + Quote(DisplayName);
			}
			if (Context.Options.bIncludeDebugMetadata)
			{
				Line += TEXT(" guid=") + Pin->PinId.ToString(EGuidFormats::DigitsWithHyphens);
			}
			if (Pin->bHidden)
			{
				Line += TEXT(" hidden=true");
			}
			if (Pin->bOrphanedPin)
			{
				Line += TEXT(" orphaned=true");
			}
			if (Pin->Direction == EGPD_Input && Pin->LinkedTo.Num() == 0)
			{
				if (Pin->bDefaultValueIsIgnored)
				{
					Line += TEXT(" default_ignored=true");
				}
				else
				{
					const FString DefaultValue = PinDefaultValue(Pin);
					if (!DefaultValue.IsEmpty())
					{
						Line += TEXT(" default=") + Quote(DefaultValue);
					}
				}
			}
			AppendLine(PinText, Depth + 2, Line);
			++PinCount;
		}
		AppendLine(OutText, Depth + 1, PinCount > 0 ? TEXT("pins:") : TEXT("pins: none"));
		OutText += PinText;
	}

	static void AppendGraphContent(
		UNiagaraScript* Script,
		UNiagaraGraph* Graph,
		FString& OutText,
		int32 Depth,
		FNiagaraExportContext& Context,
		const UE2CodeSelection::FNodeSelection* Selection = nullptr)
	{
		FGraphExportData Data;
		BuildGraphExportData(Graph, Data, Selection);
		// The signature always describes the whole script, even for a selection.
		TArray<UEdGraphNode*> AllNodes;
		GetSortedNodes(Graph, AllNodes);
		AppendScriptSignature(AllNodes, Script->GetUsage(), OutText, Depth);
		AppendLine(OutText, Depth, FString::Printf(TEXT("nodes: count=%d"), Data.Nodes.Num()));
		for (const UEdGraphNode* Node : Data.Nodes)
		{
			AppendNode(Node, Data, OutText, Depth + 1, Context);
		}

		AppendLine(OutText, Depth, Data.Connections.Num() > 0 ? FString::Printf(TEXT("connections: count=%d"), Data.Connections.Num()) : TEXT("connections: none"));
		for (const FConnection& Connection : Data.Connections)
		{
			const auto Endpoint = [](const FString& Node, const FString& PinId, const FString& Pin)
			{
				return (PinId.IsEmpty() ? Node : Node + TEXT(".") + PinId) + TEXT(" ") + Quote(Pin);
			};
			AppendLine(
				OutText,
				Depth + 1,
				FString::Printf(
					TEXT("- %s -> %s"),
					*Endpoint(Connection.FromNode, Connection.FromPinId, Connection.FromPin),
					*Endpoint(Connection.ToNode, Connection.ToPinId, Connection.ToPin)));
		}
	}

	static FString ScriptKindToken(ENiagaraScriptUsage Usage)
	{
		return Usage == ENiagaraScriptUsage::Module ? TEXT("module") : TEXT("function");
	}

	static FString ScriptKindDisplayName(ENiagaraScriptUsage Usage)
	{
		return Usage == ENiagaraScriptUsage::Module ? TEXT("Niagara Module Script") : TEXT("Niagara Function Script");
	}

	static bool ExportScriptToString(UNiagaraScript* Script, ENiagaraScriptUsage ExpectedUsage, const FUE2CodeExportOptions& Options, FString& OutText, FString& OutError)
	{
		if (!Script)
		{
			OutError = TEXT("Niagara script is null.");
			return false;
		}
		if (Script->GetUsage() != ExpectedUsage)
		{
			OutError = FString::Printf(TEXT("Niagara script '%s' is usage '%s', not a %s."), *Script->GetName(), *EnumName(Script->GetUsage()), *ScriptKindDisplayName(ExpectedUsage));
			return false;
		}

		UNiagaraGraph* Graph = GetScriptGraph(Script);
		if (!Graph)
		{
			OutError = FString::Printf(TEXT("%s '%s' has no readable editor graph."), *ScriptKindDisplayName(ExpectedUsage), *Script->GetName());
			return false;
		}

		const UE2CodeSelection::FNodeSelection Selection(Options);
		const UE2CodeSelection::FNodeSelection* RootSelection = Selection.IsActive() ? &Selection : nullptr;
		int32 SelectedNodeCount = 0;
		if (RootSelection)
		{
			TArray<UEdGraphNode*> SelectedNodes;
			GetSortedNodes(Graph, SelectedNodes, RootSelection);
			SelectedNodeCount = SelectedNodes.Num();
			if (SelectedNodeCount == 0)
			{
				OutText.Reset();
				OutError = UE2CodeSelection::NoMatchError(Script->GetName());
				return false;
			}
		}

		FNiagaraExportContext Context(Options, Script);
		RegisterCalledScripts(Script, Context, RootSelection);

		const FString KindToken = ScriptKindToken(ExpectedUsage);
		OutText.Reset();
		AppendLine(OutText, 0, FString::Printf(TEXT("UE_NODE2CODE niagara_%s_script_export version=2"), *KindToken));
		AppendLine(OutText, 0, TEXT("graph:"));
		AppendLine(OutText, 1, FString::Printf(TEXT("script: %s"), *Quote(Script->GetName())));
		AppendLine(OutText, 1, FString::Printf(TEXT("usage: %s"), *EnumName(Script->GetUsage())));
		AppendLine(OutText, 1, FString::Printf(TEXT("hierarchy_depth: %d"), Options.NodeHierarchyDepth));
		if (Options.bIncludeDebugMetadata)
		{
			AppendLine(OutText, 1, FString::Printf(TEXT("engine_version: %s"), *FEngineVersion::Current().ToString()));
			AppendLine(OutText, 1, FString::Printf(TEXT("asset_path: %s"), *Quote(Script->GetPathName())));
			AppendLine(OutText, 1, FString::Printf(TEXT("graph_path: %s"), *Quote(Graph->GetPathName())));
		}
		AppendLine(
			OutText,
			1,
			FString::Printf(
				TEXT("options: debug=%s default_properties=%s"),
				Options.bIncludeDebugMetadata ? TEXT("true") : TEXT("false"),
				Options.bIncludeDefaultLikeProperties ? TEXT("true") : TEXT("false")));
		if (RootSelection)
		{
			AppendLine(OutText, 1, UE2CodeSelection::HeaderLine(SelectedNodeCount));
		}

		AppendLine(OutText, 0, TEXT(""));
		AppendLine(OutText, 0, TEXT("root_graph:"));
		AppendGraphContent(Script, Graph, OutText, 1, Context, RootSelection);

		AppendLine(OutText, 0, TEXT(""));
		AppendLine(
			OutText,
			0,
			Context.ScriptDefinitions.Num() > 0
				? FString::Printf(TEXT("called_graphs: count=%d"), Context.ScriptDefinitions.Num())
				: TEXT("called_graphs: none"));
		for (UNiagaraScript* DefinitionScript : Context.ScriptDefinitions)
		{
			UNiagaraGraph* DefinitionGraph = GetScriptGraph(DefinitionScript);
			if (!DefinitionGraph)
			{
				continue;
			}
			const FString DefinitionId = Context.ScriptIds.FindRef(DefinitionScript);
			AppendLine(
				OutText,
				1,
				FString::Printf(
					TEXT("script %s name=%s usage=%s"),
					*DefinitionId,
					*Quote(DefinitionScript->GetName()),
					*EnumName(DefinitionScript->GetUsage())));
			if (Options.bIncludeDebugMetadata)
			{
				AppendLine(OutText, 2, FString::Printf(TEXT("asset_path: %s"), *Quote(DefinitionScript->GetPathName())));
			}
			AppendGraphContent(DefinitionScript, DefinitionGraph, OutText, 2, Context);
		}

		OutError.Reset();
		return true;
	}

	static bool ExportScriptToText(UNiagaraScript* Script, ENiagaraScriptUsage ExpectedUsage, const FString& OutputFilePath, const FUE2CodeExportOptions& Options, FString& OutError)
	{
		FString Text;
		if (!ExportScriptToString(Script, ExpectedUsage, Options, Text, OutError))
		{
			return false;
		}
		return SaveTextToFile(OutputFilePath, Text, OutError);
	}

	static bool ExportScriptAssetPathToText(const FString& ScriptAssetPath, ENiagaraScriptUsage ExpectedUsage, const FString& OutputFilePath, const FUE2CodeExportOptions& Options, FString& OutError)
	{
		FString NormalizedPath;
		UNiagaraScript* Script = LoadScript(ScriptAssetPath, NormalizedPath);
		if (!Script)
		{
			OutError = FString::Printf(TEXT("Could not load Niagara script asset from '%s' (normalized from '%s')."), *NormalizedPath, *ScriptAssetPath);
			return false;
		}
		return ExportScriptToText(Script, ExpectedUsage, OutputFilePath, Options, OutError);
	}
}

bool FUE2CodeNiagaraExporter::ExportNiagaraFunctionScriptAssetPathToText(const FString& ScriptAssetPath, const FString& OutputFilePath, const FUE2CodeExportOptions& Options, FString& OutError)
{
	return UE2CodeNiagaraExporterPrivate::ExportScriptAssetPathToText(ScriptAssetPath, ENiagaraScriptUsage::Function, OutputFilePath, Options, OutError);
}

bool FUE2CodeNiagaraExporter::ExportNiagaraFunctionScriptToText(UNiagaraScript* Script, const FString& OutputFilePath, const FUE2CodeExportOptions& Options, FString& OutError)
{
	return UE2CodeNiagaraExporterPrivate::ExportScriptToText(Script, ENiagaraScriptUsage::Function, OutputFilePath, Options, OutError);
}

bool FUE2CodeNiagaraExporter::ExportNiagaraFunctionScriptToString(UNiagaraScript* Script, const FUE2CodeExportOptions& Options, FString& OutText, FString& OutError)
{
	return UE2CodeNiagaraExporterPrivate::ExportScriptToString(Script, ENiagaraScriptUsage::Function, Options, OutText, OutError);
}

bool FUE2CodeNiagaraExporter::ExportNiagaraModuleScriptAssetPathToText(const FString& ScriptAssetPath, const FString& OutputFilePath, const FUE2CodeExportOptions& Options, FString& OutError)
{
	return UE2CodeNiagaraExporterPrivate::ExportScriptAssetPathToText(ScriptAssetPath, ENiagaraScriptUsage::Module, OutputFilePath, Options, OutError);
}

bool FUE2CodeNiagaraExporter::ExportNiagaraModuleScriptToText(UNiagaraScript* Script, const FString& OutputFilePath, const FUE2CodeExportOptions& Options, FString& OutError)
{
	return UE2CodeNiagaraExporterPrivate::ExportScriptToText(Script, ENiagaraScriptUsage::Module, OutputFilePath, Options, OutError);
}

bool FUE2CodeNiagaraExporter::ExportNiagaraModuleScriptToString(UNiagaraScript* Script, const FUE2CodeExportOptions& Options, FString& OutText, FString& OutError)
{
	return UE2CodeNiagaraExporterPrivate::ExportScriptToString(Script, ENiagaraScriptUsage::Module, Options, OutText, OutError);
}

#if WITH_DEV_AUTOMATION_TESTS
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FUE2CodeNiagaraScriptExportTest,
	"UE_Node2Code.Niagara.ScriptExport",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FUE2CodeNiagaraScriptExportTest::RunTest(const FString& Parameters)
{
	UNiagaraScript* FunctionScript = LoadObject<UNiagaraScript>(nullptr, TEXT("/Niagara/Functions/RandomBool.RandomBool"));
	if (!TestNotNull(TEXT("The engine RandomBool Niagara Function Script is available"), FunctionScript))
	{
		return false;
	}

	FString Text;
	FString Error;
	FUE2CodeExportOptions Options;
	TestTrue(TEXT("A Niagara Function Script exports to text"), FUE2CodeNiagaraExporter::ExportNiagaraFunctionScriptToString(FunctionScript, Options, Text, Error));
	TestTrue(TEXT("The export has the Niagara Function Script header"), Text.Contains(TEXT("UE_NODE2CODE niagara_function_script_export version=2")));
	TestTrue(TEXT("The export reports Function usage"), Text.Contains(TEXT("usage: Function")));
	TestTrue(TEXT("The export contains a readable signature"), Text.Contains(TEXT("signature:")) && Text.Contains(TEXT("inputs:")) && Text.Contains(TEXT("outputs:")) && Text.Contains(TEXT(" : ")));
	TestTrue(TEXT("The export contains nodes"), Text.Contains(TEXT("node N")));
	TestTrue(TEXT("The export contains at least one connection"), Text.Contains(TEXT("connections: count=")));
	TestTrue(TEXT("Pins use graph-local short identities"), Text.Contains(TEXT("- P")));
	TestTrue(TEXT("Connections use readable arrows and short identities"), Text.Contains(TEXT(" -> ")) && Text.Contains(TEXT(".P")));
	TestTrue(TEXT("Function-call graphs are referenced and expanded"), Text.Contains(TEXT("call: name=")) && Text.Contains(TEXT(" ref=NS")) && Text.Contains(TEXT("called_graphs: count=")));
	TestFalse(TEXT("Default output omits called-script object paths"), Text.Contains(TEXT("called_script_path:")));
	TestTrue(TEXT("Convert-node internal wiring is recorded"), Text.Contains(TEXT("- Connections = ")));
	TestFalse(TEXT("Convert-node wiring omits redundant internal GUIDs"), Text.Contains(TEXT("SourcePinId=")) || Text.Contains(TEXT("DestinationPinId=")));
	TestTrue(TEXT("Convert-node wiring keeps readable source/destination paths"), Text.Contains(TEXT("from=")) && Text.Contains(TEXT("to=")));

	FUE2CodeExportOptions RootOnlyOptions;
	RootOnlyOptions.NodeHierarchyDepth = 1;
	FString RootOnlyText;
	Error.Reset();
	TestTrue(TEXT("Hierarchy depth 1 exports the root Niagara graph"), FUE2CodeNiagaraExporter::ExportNiagaraFunctionScriptToString(FunctionScript, RootOnlyOptions, RootOnlyText, Error));
	TestTrue(TEXT("Hierarchy depth 1 leaves calls external"), RootOnlyText.Contains(TEXT("ref=external reason=depth_limit")));
	TestTrue(TEXT("Hierarchy depth 1 emits no called graph definitions"), RootOnlyText.Contains(TEXT("called_graphs: none")));

	FUE2CodeExportOptions DirectCallOptions;
	DirectCallOptions.NodeHierarchyDepth = 2;
	FString DirectCallText;
	Error.Reset();
	TestTrue(TEXT("Hierarchy depth 2 exports direct Niagara calls"), FUE2CodeNiagaraExporter::ExportNiagaraFunctionScriptToString(FunctionScript, DirectCallOptions, DirectCallText, Error));
	TestTrue(TEXT("Hierarchy depth 2 emits direct called graph definitions"), DirectCallText.Contains(TEXT("ref=NS")) && DirectCallText.Contains(TEXT("called_graphs: count=")));

	TArray<UEdGraphNode*> RootNodes;
	UE2CodeNiagaraExporterPrivate::GetSortedNodes(UE2CodeNiagaraExporterPrivate::GetScriptGraph(FunctionScript), RootNodes);
	const UEdGraphNode* LinkedNode = nullptr;
	for (const UEdGraphNode* Node : RootNodes)
	{
		if (Node->Pins.ContainsByPredicate([](const UEdGraphPin* Pin) { return Pin && Pin->LinkedTo.Num() > 0; }))
		{
			LinkedNode = Node;
			break;
		}
	}
	if (TestNotNull(TEXT("RandomBool has a linked node to select"), LinkedNode))
	{
		FUE2CodeExportOptions SelectionOptions;
		SelectionOptions.SelectedNodeIds.Add(LinkedNode->NodeGuid.ToString());
		FString SelectionText;
		Error.Reset();
		TestTrue(TEXT("Selected Niagara nodes export"), FUE2CodeNiagaraExporter::ExportNiagaraFunctionScriptToString(FunctionScript, SelectionOptions, SelectionText, Error));
		TestTrue(TEXT("Niagara selection header is written"), SelectionText.Contains(TEXT("selection: nodes=1")));
		TestTrue(TEXT("Only the selected Niagara node is exported"), SelectionText.Contains(TEXT("nodes: count=1")) && !SelectionText.Contains(TEXT("node N002")));
		TestTrue(TEXT("Niagara boundary links are marked"), SelectionText.Contains(TEXT("unselected:")));
		TestTrue(TEXT("The full signature is retained"), SelectionText.Contains(TEXT("signature:")));
		SelectionOptions.SelectedNodeIds = {TEXT("NoSuchNode")};
		TestFalse(TEXT("An unmatched Niagara selection fails"), FUE2CodeNiagaraExporter::ExportNiagaraFunctionScriptToString(FunctionScript, SelectionOptions, SelectionText, Error));
	}

	UNiagaraScript* ModuleScript = LoadObject<UNiagaraScript>(nullptr, TEXT("/Niagara/Modules/Emitter/SpawnRate.SpawnRate"));
	if (!TestNotNull(TEXT("The engine SpawnRate Niagara Module Script is available"), ModuleScript))
	{
		return false;
	}
	Text.Reset();
	Error.Reset();
	TestTrue(TEXT("A Niagara Module Script exports to text"), FUE2CodeNiagaraExporter::ExportNiagaraModuleScriptToString(ModuleScript, Options, Text, Error));
	TestTrue(TEXT("The export has the Niagara Module Script header"), Text.Contains(TEXT("UE_NODE2CODE niagara_module_script_export version=2")));
	TestTrue(TEXT("The export reports Module usage"), Text.Contains(TEXT("usage: Module")));
	TestTrue(TEXT("The export contains the module signature"), Text.Contains(TEXT("signature:")) && Text.Contains(TEXT("inputs:")) && Text.Contains(TEXT("outputs:")));
	TestTrue(TEXT("The module export contains nodes"), Text.Contains(TEXT("node N")));
	TestTrue(TEXT("The module export contains at least one connection"), Text.Contains(TEXT("connections: count=")));
	TestFalse(TEXT("Function export rejects a Module Script"), FUE2CodeNiagaraExporter::ExportNiagaraFunctionScriptToString(ModuleScript, Options, Text, Error));
	TestTrue(TEXT("Function rejection identifies the usage mismatch"), Error.Contains(TEXT("not a Niagara Function Script")));
	TestFalse(TEXT("Module export rejects a Function Script"), FUE2CodeNiagaraExporter::ExportNiagaraModuleScriptToString(FunctionScript, Options, Text, Error));
	TestTrue(TEXT("Module rejection identifies the usage mismatch"), Error.Contains(TEXT("not a Niagara Module Script")));

	const FString ScriptFilename = FPackageName::LongPackageNameToFilename(TEXT("/Niagara/Functions/RandomBool"), FPackageName::GetAssetPackageExtension());
	FString NormalizedPath;
	UNiagaraScript* FilePathScript = UE2CodeNiagaraExporterPrivate::LoadScript(ScriptFilename, NormalizedPath);
	TestNotNull(TEXT("A mounted-plugin .uasset filename loads"), FilePathScript);
	TestEqual(TEXT("A mounted-plugin .uasset filename normalizes to its package"), NormalizedPath, FString(TEXT("/Niagara/Functions/RandomBool")));

	UNiagaraNodeInput* SemanticTestNode = NewObject<UNiagaraNodeInput>();
	SemanticTestNode->SetEnabledState(ENodeEnabledState::Disabled);
	UEdGraphPin* IgnoredDefaultPin = SemanticTestNode->CreatePin(EGPD_Input, TEXT("test"), TEXT("InternalName"));
	IgnoredDefaultPin->PinFriendlyName = FText::FromString(TEXT("Display Name"));
	IgnoredDefaultPin->DefaultValue = TEXT("MustNotBeExported");
	IgnoredDefaultPin->bDefaultValueIsIgnored = true;
	IgnoredDefaultPin->PinType.ContainerType = EPinContainerType::Array;
	IgnoredDefaultPin->PinType.bIsReference = true;
	IgnoredDefaultPin->PinType.bIsConst = true;
	UE2CodeNiagaraExporterPrivate::FGraphExportData SemanticTestData;
	SemanticTestData.Nodes.Add(SemanticTestNode);
	SemanticTestData.NodeIds.Add(SemanticTestNode, TEXT("N001"));
	SemanticTestData.PinIds.Add(IgnoredDefaultPin, TEXT("P001"));
	FString SemanticTestText;
	UE2CodeNiagaraExporterPrivate::FNiagaraExportContext SemanticTestContext(Options, nullptr);
	UE2CodeNiagaraExporterPrivate::AppendNode(SemanticTestNode, SemanticTestData, SemanticTestText, 1, SemanticTestContext);
	TestTrue(TEXT("Disabled-node semantics are recorded"), SemanticTestText.Contains(TEXT("state: Disabled")));
	TestTrue(TEXT("Internal and display pin names stay distinct"), SemanticTestText.Contains(TEXT("\"InternalName\"")) && SemanticTestText.Contains(TEXT("display_name=\"Display Name\"")));
	TestTrue(TEXT("Ignored defaults are marked"), SemanticTestText.Contains(TEXT("default_ignored=true")));
	TestFalse(TEXT("Ignored defaults are not exported as values"), SemanticTestText.Contains(TEXT("MustNotBeExported")));
	TestTrue(TEXT("Pin container/reference/const semantics are retained"), SemanticTestText.Contains(TEXT("container=array ref=true const=true")));
	TestFalse(TEXT("Default output omits Niagara node positions"), SemanticTestText.Contains(TEXT("debug: position=")));

	Options.bIncludeDebugMetadata = true;
	SemanticTestText.Reset();
	UE2CodeNiagaraExporterPrivate::FNiagaraExportContext DebugSemanticTestContext(Options, nullptr);
	UE2CodeNiagaraExporterPrivate::AppendNode(SemanticTestNode, SemanticTestData, SemanticTestText, 1, DebugSemanticTestContext);
	TestTrue(TEXT("Debug output retains Niagara node positions"), SemanticTestText.Contains(TEXT("debug: position=")));
	TestTrue(TEXT("Debug output retains original Niagara pin GUIDs"), SemanticTestText.Contains(TEXT(" guid=")));
	Options.bIncludeDebugMetadata = false;

	UNiagaraScript* DynamicInputScript = NewObject<UNiagaraScript>();
	DynamicInputScript->SetUsage(ENiagaraScriptUsage::DynamicInput);
	Text.Reset();
	Error.Reset();
	TestFalse(TEXT("A Niagara Dynamic Input is rejected"), FUE2CodeNiagaraExporter::ExportNiagaraModuleScriptToString(DynamicInputScript, Options, Text, Error));
	TestTrue(TEXT("The Dynamic Input rejection identifies the usage mismatch"), Error.Contains(TEXT("not a Niagara Module Script")));
	return true;
}
#endif
