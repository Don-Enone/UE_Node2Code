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
		Value.ReplaceInline(TEXT("\\"), TEXT("\\\\"));
		Value.ReplaceInline(TEXT("\""), TEXT("\\\""));
		Value.ReplaceInline(TEXT("\r\n"), TEXT("\\n"));
		Value.ReplaceInline(TEXT("\n"), TEXT("\\n"));
		Value.ReplaceInline(TEXT("\r"), TEXT("\\n"));
		Value.ReplaceInline(TEXT("\t"), TEXT("\\t"));
		return Value;
	}

	static FString Quote(const FString& Value)
	{
		return FString::Printf(TEXT("\"%s\""), *Escape(Value));
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
		return Name.IsEmpty() ? Pin->PinId.ToString(EGuidFormats::DigitsWithHyphens) : Name;
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
			if (A.NodePosX == B.NodePosX)
			{
				if (A.NodePosY == B.NodePosY)
				{
					return A.GetName() < B.GetName();
				}
				return A.NodePosY < B.NodePosY;
			}
			return A.NodePosX < B.NodePosX;
		});
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

	static void AppendScriptSignature(const TArray<UEdGraphNode*>& Nodes, ENiagaraScriptUsage ScriptUsage, const FString& SignaturePrefix, FString& OutText)
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

		const FString InputsLabel = SignaturePrefix + TEXT("_inputs:");
		AppendLine(OutText, 0, Inputs.Num() > 0 ? InputsLabel : InputsLabel + TEXT(" none"));
		for (const UNiagaraNodeInput* Input : Inputs)
		{
			FString Line = FString::Printf(
				TEXT("- name=%s type=%s required=%s auto_bind=%s hidden=%s sort_priority=%d"),
				*Quote(Input->Input.GetName().ToString()),
				*VariableTypeName(Input->Input),
				Input->ExposureOptions.bRequired ? TEXT("true") : TEXT("false"),
				Input->ExposureOptions.bCanAutoBind ? TEXT("true") : TEXT("false"),
				Input->ExposureOptions.bHidden ? TEXT("true") : TEXT("false"),
				Input->CallSortPriority);
			const FString DefaultValue = VariableDefaultValue(Input->Input);
			if (!DefaultValue.IsEmpty())
			{
				Line += TEXT(" default=") + Quote(DefaultValue);
			}
			AppendLine(OutText, 1, Line);
		}

		int32 OutputCount = 0;
		for (const UNiagaraNodeOutput* Output : Outputs)
		{
			OutputCount += Output->GetOutputs().Num();
		}
		const FString OutputsLabel = SignaturePrefix + TEXT("_outputs:");
		AppendLine(OutText, 0, OutputCount > 0 ? OutputsLabel : OutputsLabel + TEXT(" none"));
		for (const UNiagaraNodeOutput* Output : Outputs)
		{
			for (const FNiagaraVariable& Variable : Output->GetOutputs())
			{
				AppendLine(OutText, 1, FString::Printf(TEXT("- name=%s type=%s"), *Quote(Variable.GetName().ToString()), *VariableTypeName(Variable)));
			}
		}
	}

	static void AppendNode(const UEdGraphNode* Node, const TMap<const UEdGraphNode*, FString>& NodeIds, FString& OutText, const FUE2CodeExportOptions& Options)
	{
		if (!Node)
		{
			return;
		}

		const FString& NodeId = NodeIds.FindChecked(Node);
		AppendLine(OutText, 1, FString::Printf(TEXT("node_begin id=%s type=%s"), *NodeId, *NodeTypeName(Node)));
		AppendLine(OutText, 2, FString::Printf(TEXT("title: %s"), *Quote(Node->GetNodeTitle(ENodeTitleType::ListView).ToString())));
		AppendLine(OutText, 2, FString::Printf(TEXT("position: x=%d y=%d"), Node->NodePosX, Node->NodePosY));
		AppendLine(OutText, 2, FString::Printf(TEXT("enabled_state: %s"), LexToString(Node->GetDesiredEnabledState())));
		if (!Node->NodeComment.IsEmpty())
		{
			AppendLine(OutText, 2, FString::Printf(TEXT("comment: %s"), *Quote(Node->NodeComment)));
		}
		if (Options.bIncludeDebugMetadata)
		{
			AppendLine(OutText, 2, FString::Printf(TEXT("object_name: %s"), *Quote(Node->GetName())));
			AppendLine(OutText, 2, FString::Printf(TEXT("node_guid: %s"), *Node->NodeGuid.ToString(EGuidFormats::DigitsWithHyphens)));
		}

		if (const UNiagaraNodeInput* Input = Cast<UNiagaraNodeInput>(Node))
		{
			FString Line = FString::Printf(
				TEXT("niagara_input: name=%s type=%s usage=%s exposed=%s required=%s"),
				*Quote(Input->Input.GetName().ToString()),
				*VariableTypeName(Input->Input),
				*EnumName(Input->Usage),
				Input->ExposureOptions.bExposed ? TEXT("true") : TEXT("false"),
				Input->ExposureOptions.bRequired ? TEXT("true") : TEXT("false"));
			const FString DefaultValue = VariableDefaultValue(Input->Input);
			if (!DefaultValue.IsEmpty())
			{
				Line += TEXT(" default=") + Quote(DefaultValue);
			}
			AppendLine(OutText, 2, Line);
		}
		else if (const UNiagaraNodeOutput* Output = Cast<UNiagaraNodeOutput>(Node))
		{
			AppendLine(OutText, 2, FString::Printf(TEXT("niagara_output: usage=%s output_count=%d"), *EnumName(Output->GetUsage()), Output->GetOutputs().Num()));
		}

		if (const UNiagaraNodeOp* Operation = Cast<UNiagaraNodeOp>(Node))
		{
			AppendLine(OutText, 2, FString::Printf(TEXT("operation: %s"), *Quote(Operation->OpName.ToString())));
		}
		if (const UNiagaraNodeFunctionCall* FunctionCall = Cast<UNiagaraNodeFunctionCall>(Node))
		{
			const FString FunctionName = FunctionCall->GetFunctionName();
			if (!FunctionName.IsEmpty())
			{
				AppendLine(OutText, 2, FString::Printf(TEXT("function_name: %s"), *Quote(FunctionName)));
			}
			if (FunctionCall->FunctionScript)
			{
				AppendLine(OutText, 2, FString::Printf(TEXT("called_script: %s"), *FunctionCall->FunctionScript->GetName()));
				AppendLine(OutText, 2, FString::Printf(TEXT("called_script_path: %s"), *Quote(FunctionCall->FunctionScript->GetPathName())));
				AppendLine(OutText, 2, FString::Printf(TEXT("called_usage: %s"), *EnumName(FunctionCall->FunctionScript->GetUsage())));
			#if ENGINE_MAJOR_VERSION >= 5
				if (FunctionCall->SelectedScriptVersion.IsValid())
				{
					AppendLine(OutText, 2, FString::Printf(TEXT("called_script_version: %s"), *FunctionCall->SelectedScriptVersion.ToString(EGuidFormats::DigitsWithHyphens)));
				}
			#endif
			}
			else if (!FunctionCall->FunctionScriptAssetObjectPath.IsNone())
			{
				AppendLine(OutText, 2, FString::Printf(TEXT("called_script_path: %s"), *Quote(FunctionCall->FunctionScriptAssetObjectPath.ToString())));
			}
		}

		AppendEditableProperties(Cast<UNiagaraNode>(const_cast<UEdGraphNode*>(Node)), OutText, 2, Options);

		int32 PinCount = 0;
		FString PinText;
		for (const UEdGraphPin* Pin : Node->Pins)
		{
			if (!Pin || IsAddPin(Pin))
			{
				continue;
			}
			FString Line = FString::Printf(
				TEXT("- %s name=%s type=%s"),
				Pin->Direction == EGPD_Input ? TEXT("in") : TEXT("out"),
				*Quote(PinInternalName(Pin)),
				*PinTypeName(Pin));
			const FString DisplayName = PinDisplayName(Pin);
			if (!DisplayName.IsEmpty() && DisplayName != PinInternalName(Pin))
			{
				Line += TEXT(" display_name=") + Quote(DisplayName);
			}
			Line += TEXT(" pin_id=") + Pin->PinId.ToString(EGuidFormats::DigitsWithHyphens);
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
			AppendLine(PinText, 3, Line);
			++PinCount;
		}
		AppendLine(OutText, 2, PinCount > 0 ? TEXT("pins:") : TEXT("pins: none"));
		OutText += PinText;
		AppendLine(OutText, 1, TEXT("node_end"));
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

	static void GatherConnections(const TArray<UEdGraphNode*>& Nodes, const TMap<const UEdGraphNode*, FString>& NodeIds, TArray<FConnection>& OutConnections)
	{
		for (const UEdGraphNode* Node : Nodes)
		{
			for (const UEdGraphPin* Pin : Node->Pins)
			{
				if (!Pin || IsAddPin(Pin) || Pin->Direction != EGPD_Output)
				{
					continue;
				}
				for (const UEdGraphPin* LinkedPin : Pin->LinkedTo)
				{
					const UEdGraphNode* LinkedNode = LinkedPin ? LinkedPin->GetOwningNode() : nullptr;
					if (!LinkedPin || LinkedPin->Direction != EGPD_Input || !NodeIds.Contains(LinkedNode))
					{
						continue;
					}
					FConnection& Connection = OutConnections.AddDefaulted_GetRef();
					Connection.FromNode = NodeIds.FindChecked(Node);
					Connection.FromPin = PinInternalName(Pin);
					Connection.FromPinId = Pin->PinId.ToString(EGuidFormats::DigitsWithHyphens);
					Connection.ToNode = NodeIds.FindChecked(LinkedNode);
					Connection.ToPin = PinInternalName(LinkedPin);
					Connection.ToPinId = LinkedPin->PinId.ToString(EGuidFormats::DigitsWithHyphens);
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

		UNiagaraScriptSource* Source = GetScriptSource(Script);
		UNiagaraGraph* Graph = Source ? Source->NodeGraph : nullptr;
		if (!Graph)
		{
			OutError = FString::Printf(TEXT("%s '%s' has no readable editor graph."), *ScriptKindDisplayName(ExpectedUsage), *Script->GetName());
			return false;
		}

		TArray<UEdGraphNode*> Nodes;
		for (UEdGraphNode* Node : Graph->Nodes)
		{
			if (Node)
			{
				Nodes.Add(Node);
			}
		}
		SortNodes(Nodes);

		TMap<const UEdGraphNode*, FString> NodeIds;
		for (int32 NodeIndex = 0; NodeIndex < Nodes.Num(); ++NodeIndex)
		{
			NodeIds.Add(Nodes[NodeIndex], FString::Printf(TEXT("N%03d"), NodeIndex + 1));
		}

		TArray<FConnection> Connections;
		GatherConnections(Nodes, NodeIds, Connections);

		const FString KindToken = ScriptKindToken(ExpectedUsage);
		OutText.Reset();
		AppendLine(OutText, 0, FString::Printf(TEXT("UE_NODE2CODE niagara_%s_script_export version=1"), *KindToken));
		AppendLine(OutText, 0, FString::Printf(TEXT("engine_version: %s"), *FEngineVersion::Current().ToString()));
		AppendLine(OutText, 0, FString::Printf(TEXT("niagara_%s_script: %s"), *KindToken, *Script->GetName()));
		AppendLine(OutText, 0, FString::Printf(TEXT("usage: %s"), *EnumName(Script->GetUsage())));
		if (Options.bIncludeDebugMetadata)
		{
			AppendLine(OutText, 0, FString::Printf(TEXT("asset_path: %s"), *Quote(Script->GetPathName())));
			AppendLine(OutText, 0, FString::Printf(TEXT("graph_path: %s"), *Quote(Graph->GetPathName())));
		}
		AppendLine(OutText, 0, FString::Printf(TEXT("options: include_debug_metadata=%s include_default_like_properties=%s"), Options.bIncludeDebugMetadata ? TEXT("true") : TEXT("false"), Options.bIncludeDefaultLikeProperties ? TEXT("true") : TEXT("false")));
		AppendLine(OutText, 0, TEXT(""));

		AppendScriptSignature(Nodes, ExpectedUsage, KindToken, OutText);
		AppendLine(OutText, 0, TEXT(""));
		AppendLine(OutText, 0, FString::Printf(TEXT("node_count: %d"), Nodes.Num()));
		AppendLine(OutText, 0, TEXT("nodes:"));
		for (const UEdGraphNode* Node : Nodes)
		{
			AppendNode(Node, NodeIds, OutText, Options);
		}

		AppendLine(OutText, 0, TEXT(""));
		AppendLine(OutText, 0, FString::Printf(TEXT("connection_count: %d"), Connections.Num()));
		AppendLine(OutText, 0, Connections.Num() > 0 ? TEXT("connections: from_node from_pin from_pin_id to_node to_pin to_pin_id") : TEXT("connections: none"));
		for (const FConnection& Connection : Connections)
		{
			AppendLine(OutText, 1, FString::Printf(TEXT("%s %s %s %s %s %s"), *Connection.FromNode, *Quote(Connection.FromPin), *Connection.FromPinId, *Connection.ToNode, *Quote(Connection.ToPin), *Connection.ToPinId));
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
	TestTrue(TEXT("The export has the Niagara Function Script header"), Text.Contains(TEXT("UE_NODE2CODE niagara_function_script_export version=1")));
	TestTrue(TEXT("The export reports Function usage"), Text.Contains(TEXT("usage: Function")));
	TestTrue(TEXT("The export contains the function signature"), Text.Contains(TEXT("function_inputs:")) && Text.Contains(TEXT("function_outputs:")) && Text.Contains(TEXT(" type=")));
	TestTrue(TEXT("The export contains nodes"), Text.Contains(TEXT("node_begin")));
	TestFalse(TEXT("The export contains at least one connection"), Text.Contains(TEXT("connection_count: 0")));
	TestTrue(TEXT("Pins have stable identities"), Text.Contains(TEXT(" pin_id=")));
	TestTrue(TEXT("Connections use stable pin identities"), Text.Contains(TEXT("connections: from_node from_pin from_pin_id to_node to_pin to_pin_id")));
	TestTrue(TEXT("Function-call assets are recorded"), Text.Contains(TEXT("called_script: ")) && Text.Contains(TEXT("called_script_path: ")) && Text.Contains(TEXT("called_usage: ")));
	TestTrue(TEXT("Convert-node internal wiring is recorded"), Text.Contains(TEXT("- Connections = ")));

	UNiagaraScript* ModuleScript = LoadObject<UNiagaraScript>(nullptr, TEXT("/Niagara/Modules/Emitter/SpawnRate.SpawnRate"));
	if (!TestNotNull(TEXT("The engine SpawnRate Niagara Module Script is available"), ModuleScript))
	{
		return false;
	}
	Text.Reset();
	Error.Reset();
	TestTrue(TEXT("A Niagara Module Script exports to text"), FUE2CodeNiagaraExporter::ExportNiagaraModuleScriptToString(ModuleScript, Options, Text, Error));
	TestTrue(TEXT("The export has the Niagara Module Script header"), Text.Contains(TEXT("UE_NODE2CODE niagara_module_script_export version=1")));
	TestTrue(TEXT("The export reports Module usage"), Text.Contains(TEXT("usage: Module")));
	TestTrue(TEXT("The export contains the module signature"), Text.Contains(TEXT("module_inputs:")) && Text.Contains(TEXT("module_outputs:")) && Text.Contains(TEXT(" type=")));
	TestTrue(TEXT("The module export contains nodes"), Text.Contains(TEXT("node_begin")));
	TestFalse(TEXT("The module export contains at least one connection"), Text.Contains(TEXT("connection_count: 0")));
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
	TMap<const UEdGraphNode*, FString> SemanticTestNodeIds;
	SemanticTestNodeIds.Add(SemanticTestNode, TEXT("N001"));
	FString SemanticTestText;
	UE2CodeNiagaraExporterPrivate::AppendNode(SemanticTestNode, SemanticTestNodeIds, SemanticTestText, Options);
	TestTrue(TEXT("Disabled-node semantics are recorded"), SemanticTestText.Contains(TEXT("enabled_state: Disabled")));
	TestTrue(TEXT("Internal and display pin names stay distinct"), SemanticTestText.Contains(TEXT("name=\"InternalName\"")) && SemanticTestText.Contains(TEXT("display_name=\"Display Name\"")));
	TestTrue(TEXT("Ignored defaults are marked"), SemanticTestText.Contains(TEXT("default_ignored=true")));
	TestFalse(TEXT("Ignored defaults are not exported as values"), SemanticTestText.Contains(TEXT("MustNotBeExported")));

	UNiagaraScript* DynamicInputScript = NewObject<UNiagaraScript>();
	DynamicInputScript->SetUsage(ENiagaraScriptUsage::DynamicInput);
	Text.Reset();
	Error.Reset();
	TestFalse(TEXT("A Niagara Dynamic Input is rejected"), FUE2CodeNiagaraExporter::ExportNiagaraModuleScriptToString(DynamicInputScript, Options, Text, Error));
	TestTrue(TEXT("The Dynamic Input rejection identifies the usage mismatch"), Error.Contains(TEXT("not a Niagara Module Script")));
	return true;
}
#endif
