#include "UE2CodeBlueprintExporter.h"

#include "UE2CodeEngineCompat.h"
#include "UE2CodeTextFormat.h"
#include "UE2CodeBlueprintTextFormat.h"
#include "UE2CodeSelection.h"
#include "EdGraph/EdGraph.h"
#include "EdGraph/EdGraphNode.h"
#include "EdGraph/EdGraphPin.h"
#include "EdGraph/EdGraphSchema.h"
#include "Engine/Blueprint.h"
#include "HAL/FileManager.h"
#include "K2Node_CallFunction.h"
#include "K2Node_Composite.h"
#include "K2Node_Event.h"
#include "K2Node_FunctionEntry.h"
#include "K2Node_FunctionTerminator.h"
#include "K2Node_Knot.h"
#include "K2Node_Tunnel.h"
#include "K2Node_MacroInstance.h"
#include "K2Node_Variable.h"
#include "Misc/FileHelper.h"
#include "Misc/PackageName.h"
#include "Misc/Paths.h"

namespace UE2CodeBlueprintExporterPrivate
{
	using UE2CodeTextFormat::Quote;
	using UE2CodeBlueprintTextFormat::FFormatter;

	static void Line(FString& Text, int32 Depth, const FString& Value)
	{
		for (int32 Index = 0; Index < Depth; ++Index) { Text += TEXT("  "); }
		Text += Value + LINE_TERMINATOR;
	}

	static FString PinType(const FEdGraphPinType& Type, FFormatter& Format)
	{
		FString Result = Type.PinCategory.ToString();
		if (!Type.PinSubCategory.IsNone()) { Result += TEXT(":") + Type.PinSubCategory.ToString(); }
		if (UObject* Object = Type.PinSubCategoryObject.Get()) { Result += TEXT(":") + Format.Path(Object->GetPathName()); }
		if (Type.IsArray()) { Result = TEXT("array<") + Result + TEXT(">"); }
		if (Type.IsSet()) { Result = TEXT("set<") + Result + TEXT(">"); }
		if (Type.IsMap())
		{
			FEdGraphPinType ValueType;
			ValueType.PinCategory = Type.PinValueType.TerminalCategory;
			ValueType.PinSubCategory = Type.PinValueType.TerminalSubCategory;
			ValueType.PinSubCategoryObject = Type.PinValueType.TerminalSubCategoryObject;
			Result = TEXT("map<") + Result + TEXT(",") + PinType(ValueType, Format) + TEXT(">");
		}
		if (Type.bIsReference) { Result += TEXT("&"); }
		if (Type.bIsConst) { Result = TEXT("const ") + Result; }
		return Result;
	}

	static TArray<UEdGraphNode*> SortedNodes(UEdGraph* Graph)
	{
		TArray<UEdGraphNode*> Result;
		for (UEdGraphNode* Node : Graph->Nodes) { if (Node) { Result.Add(Node); } }
		Result.Sort([](const UEdGraphNode& A, const UEdGraphNode& B) { return A.GetName() < B.GetName(); });
		return Result;
	}

	static UBlueprint* OwnerBlueprint(const UObject* Object)
	{
		return Object ? Object->GetTypedOuter<UBlueprint>() : nullptr;
	}

	static UEdGraph* CalledGraph(UEdGraphNode* Node)
	{
		if (UK2Node_MacroInstance* Macro = Cast<UK2Node_MacroInstance>(Node)) { return Macro->GetMacroGraph(); }
		if (UK2Node_Composite* Composite = Cast<UK2Node_Composite>(Node)) { return Composite->BoundGraph; }
		if (UK2Node_CallFunction* Call = Cast<UK2Node_CallFunction>(Node))
		{
			UBlueprint* Owner = OwnerBlueprint(Node);
			UFunction* Function = Call->GetTargetFunction();
			UClass* Class = Function ? Function->GetOuterUClass() : Call->FunctionReference.GetMemberParentClass(Owner ? Owner->GeneratedClass : nullptr);
			UBlueprint* Target = Class ? Cast<UBlueprint>(Class->ClassGeneratedBy) : nullptr;
			if (!Target && Call->FunctionReference.IsSelfContext()) { Target = Owner; }
			if (Target)
			{
				for (UEdGraph* Graph : Target->FunctionGraphs)
				{
					if (Graph && Graph->GetFName() == Call->FunctionReference.GetMemberName()) { return Graph; }
				}
			}
		}
		return nullptr;
	}

	static FString Member(const FMemberReference& Reference, FFormatter& Format)
	{
		FString Result = TEXT("name=") + Quote(Reference.GetMemberName().ToString());
		Result += Reference.IsSelfContext() ? TEXT(" context=self") : TEXT(" context=external");
		if (UClass* Class = Reference.GetMemberParentClass()) { Result += TEXT(" owner=") + Quote(Format.Path(Class->GetPathName())); }
		if (!Reference.GetMemberScopeName().IsEmpty()) { Result += TEXT(" scope=") + Quote(Reference.GetMemberScopeName()); }
		return Result;
	}

	struct FGraphEntry
	{
		UEdGraph* Graph = nullptr;
		int32 Layer = 1;
		bool bRoot = false;
		// Only selected nodes of this graph are exported.
		bool bSelection = false;
	};

	struct FContext
	{
		FFormatter Format;
		TArray<FGraphEntry> Graphs;
		TMap<UEdGraph*, int32> GraphIds;
		int32 MaxLayer = 32;
		const UE2CodeSelection::FNodeSelection* Selection = nullptr;
		void Add(UEdGraph* Graph, int32 Layer, bool bRoot, bool bSelection = false)
		{
			if (Graph && !GraphIds.Contains(Graph))
			{
				GraphIds.Add(Graph, Graphs.Num());
				FGraphEntry Entry;
				Entry.Graph = Graph;
				Entry.Layer = Layer;
				Entry.bRoot = bRoot;
				Entry.bSelection = bSelection;
				Graphs.Add(Entry);
			}
		}
		FString Ref(UEdGraph* Graph) const
		{
			const int32* Id = GraphIds.Find(Graph);
			return Id ? FString::Printf(TEXT("G%d"), *Id) : TEXT("external");
		}
	};

	// Keep editable node settings (casts, switches, timelines, RPC flags, etc.),
	// but never serialize the graph object hierarchy or transient editor caches.
	static void NodeProperties(UEdGraphNode* Node, FString& Text, const FUE2CodeExportOptions& Options, FFormatter& Format)
	{
		TArray<FProperty*> Properties;
		for (TFieldIterator<FProperty> It(Node->GetClass()); It; ++It)
		{
			FProperty* Property = *It;
			if (Property->GetOwnerClass() == UEdGraphNode::StaticClass()
				|| Property->HasAnyPropertyFlags(CPF_Transient | CPF_Deprecated)) { continue; }
			const FString Name = Property->GetName();
			if (!Format.bDebug && Name == TEXT("bIsPureFunc") && Cast<UK2Node_CallFunction>(Node)) { continue; }
			const bool bSemantic = Name == TEXT("TargetType") || Name == TEXT("FunctionFlags")
				|| Name == TEXT("TimelineName") || Name == TEXT("bIsPureFunc")
				|| Name == TEXT("bIsPureCast") || Name == TEXT("Enum")
				|| Name == TEXT("SignatureName") || Name == TEXT("SignatureClass");
			if (!bSemantic && !Property->HasAnyPropertyFlags(CPF_Edit)) { continue; }
			if (CastField<FArrayProperty>(Property) || CastField<FMapProperty>(Property)
				|| CastField<FSetProperty>(Property) || CastField<FStructProperty>(Property)) { continue; }
			if (!Options.bIncludeDefaultLikeProperties && Property->Identical_InContainer(Node, Node->GetClass()->GetDefaultObject())) { continue; }
			Properties.Add(Property);
		}
		Properties.Sort([](const FProperty& A, const FProperty& B) { return A.GetName() < B.GetName(); });
		for (const FProperty* Property : Properties)
		{
			FString Value;
			const void* Ptr = Property->ContainerPtrToValuePtr<const void>(Node);
			if (const FObjectPropertyBase* ObjectProperty = CastField<FObjectPropertyBase>(Property))
			{
				UObject* Object = ObjectProperty->GetObjectPropertyValue(Ptr);
				if (Object && (Object->IsA<UEdGraph>() || Object->IsA<UEdGraphNode>())) { continue; }
				Value = Object ? Format.Path(Object->GetPathName()) : TEXT("None");
			}
			else
			{
				UE2CodeEngineCompat::ExportPropertyText(Property, Value, Ptr, nullptr);
				Value = Format.Property(Property, Value);
			}
			Line(Text, 3, TEXT("property ") + Property->GetName() + TEXT("=") + Quote(Value));
		}
	}

	// Inline only well-formed reroute chains with one upstream source. Preserve
	// disconnected, cyclic, annotated and orphaned reroutes rather than guessing.
	static UEdGraphPin* SourceThroughReroutes(UEdGraphPin* Source)
	{
		UEdGraphPin* Original = Source;
		TSet<UEdGraphNode*> Seen;
		while (Source)
		{
			UK2Node_Knot* Knot = Cast<UK2Node_Knot>(Source->GetOwningNode());
			if (!Knot) { return Source; }
			if (Seen.Contains(Knot) || Knot->Pins.Num() != 2 || !Knot->NodeComment.IsEmpty()
				|| !Knot->Pins[0] || !Knot->Pins[1] || Knot->GetOutputPin() != Source
				|| Knot->GetInputPin()->LinkedTo.Num() != 1 || Source->bOrphanedPin || Knot->GetInputPin()->bOrphanedPin)
			{
				return Original;
			}
			Seen.Add(Knot);
			UEdGraphPin* Upstream = Knot->GetInputPin()->LinkedTo[0];
			if (!Upstream || Upstream->Direction != EGPD_Output || Upstream->GetOwningNode()->GetGraph() != Knot->GetGraph()) { return Original; }
			Source = Upstream;
		}
		return Original;
	}

	static bool KeepPin(UEdGraphPin* Pin, bool bDebug)
	{
		if (!Pin) { return false; }
		if (bDebug || Pin->bOrphanedPin || Pin->LinkedTo.Num() || Pin->PinType.PinCategory == TEXT("exec")) { return true; }
		UEdGraphNode* Node = Pin->GetOwningNode();
		if (Cast<UK2Node_FunctionTerminator>(Node) || Cast<UK2Node_Tunnel>(Node) || Cast<UK2Node_Event>(Node)) { return true; }
		if (Pin->Direction == EGPD_Input)
		{
			UK2Node_CallFunction* Call = Cast<UK2Node_CallFunction>(Node);
			UFunction* Function = Call ? Call->GetTargetFunction() : nullptr;
			if (Pin->PinName == TEXT("self") && Pin->bHidden && Function && Function->HasAnyFunctionFlags(FUNC_Static)) { return false; }
			return true;
		}
		// Keep split parents so every retained child has a declared parent ID.
		return Pin->SubPins.Num() > 0;
	}

	static FString PinName(UEdGraphPin* Pin, bool bDebug)
	{
		FString Name = Pin->PinName.ToString();
		if (bDebug) { return Name; }
		if (Pin->ParentPin)
		{
			const FString Prefix = Pin->ParentPin->PinName.ToString() + TEXT("_");
			if (Name.StartsWith(Prefix)) { Name = Name.Mid(Prefix.Len()); }
		}
		return UE2CodeBlueprintTextFormat::FieldName(Name);
	}

	static TArray<UEdGraphNode*> ExportedNodes(const FGraphEntry& Entry, const FContext& Context)
	{
		TArray<UEdGraphNode*> Nodes = SortedNodes(Entry.Graph);
		if (Entry.bSelection && Context.Selection)
		{
			Nodes.RemoveAll([&Context](UEdGraphNode* Node) { return !Context.Selection->Contains(Node, Node->NodeGuid); });
		}
		if (!Context.Format.bDebug)
		{
			Nodes.RemoveAll([](UEdGraphNode* Node)
			{
				UK2Node_Knot* Knot = Cast<UK2Node_Knot>(Node);
				return Knot && Knot->Pins.Num() == 2 && Knot->Pins[1] && SourceThroughReroutes(Knot->GetOutputPin()) != Knot->GetOutputPin();
			});
		}
		return Nodes;
	}

	// Endpoint of a link that leaves the exported selection.
	static FString Unselected(UEdGraphPin* Pin, bool bDebug)
	{
		return TEXT("unselected:") + Quote(Pin->GetOwningNode()->GetNodeTitle(ENodeTitleType::ListView).ToString() + TEXT(".") + PinName(Pin, bDebug));
	}

	// Follow inlined or unselected reroutes forward to the real input pins.
	static void DownstreamPins(UEdGraphPin* Output, const TMap<UEdGraphPin*, FString>& PinIds, bool bDebug, TSet<UEdGraphPin*>& Seen, TArray<UEdGraphPin*>& OutPins)
	{
		for (UEdGraphPin* Linked : Output->LinkedTo)
		{
			if (!Linked || Seen.Contains(Linked)) { continue; }
			Seen.Add(Linked);
			UK2Node_Knot* Knot = Cast<UK2Node_Knot>(Linked->GetOwningNode());
			if (!bDebug && Knot && !PinIds.Contains(Linked) && Knot->Pins.Num() == 2 && Knot->GetOutputPin())
			{
				DownstreamPins(Knot->GetOutputPin(), PinIds, bDebug, Seen, OutPins);
			}
			else
			{
				OutPins.Add(Linked);
			}
		}
	}

	static void ExportGraph(const FGraphEntry& Entry, FContext& Context, const FUE2CodeExportOptions& Options, FString& Text)
	{
		FFormatter& Format = Context.Format;
		UEdGraph* Graph = Entry.Graph;
		const UEdGraphSchema* Schema = Graph->GetSchema();
		Line(Text, 1, TEXT("graph ") + Context.Ref(Graph) + TEXT(" name=") + Quote(Graph->GetName())
			+ TEXT(" owner=") + Quote(Format.Path(OwnerBlueprint(Graph) ? OwnerBlueprint(Graph)->GetPathName() : Graph->GetPathName()))
			+ (Format.bDebug || !Schema || Schema->GetClass()->GetFName() != TEXT("EdGraphSchema_K2") ? TEXT(" schema=") + Quote(Schema ? Schema->GetClass()->GetName() : TEXT("None")) : TEXT(""))
			+ FString::Printf(TEXT(" layer=%d root=%s"), Entry.Layer, Entry.bRoot ? TEXT("true") : TEXT("false"))
			+ (Entry.bSelection ? TEXT(" scope=selection") : TEXT("")));
		const TArray<UEdGraphNode*> Nodes = ExportedNodes(Entry, Context);
		TMap<UEdGraphNode*, FString> NodeIds;
		TMap<UEdGraphPin*, FString> PinIds;
		for (int32 Index = 0; Index < Nodes.Num(); ++Index)
		{
			const FString Id = FString::Printf(TEXT("N%d"), Index);
			NodeIds.Add(Nodes[Index], Id);
			for (int32 PinIndex = 0; PinIndex < Nodes[Index]->Pins.Num(); ++PinIndex)
			{
				if (UEdGraphPin* Pin = Nodes[Index]->Pins[PinIndex])
				{
					if (KeepPin(Pin, Format.bDebug)) { PinIds.Add(Pin, Id + FString::Printf(TEXT(".P%d"), PinIndex)); }
				}
			}
		}
		for (UEdGraphNode* Node : Nodes)
		{
			FString NodeType = Node->GetClass()->GetName();
			if (!Format.bDebug && NodeType.StartsWith(TEXT("K2Node_"))) { NodeType = TEXT("K2_") + NodeType.Mid(7); }
			FString NodeLine = TEXT("node ") + NodeIds.FindChecked(Node) + TEXT(" type=") + NodeType;
			if (Format.bDebug || (!Cast<UK2Node_CallFunction>(Node) && !Cast<UK2Node_Variable>(Node)))
			{
				NodeLine += TEXT(" title=") + Quote(Node->GetNodeTitle(ENodeTitleType::ListView).ToString());
			}
			if (!Format.bDebug)
			{
				if (UK2Node_CallFunction* Call = Cast<UK2Node_CallFunction>(Node))
				{
					if (Call->IsNodePure()) { NodeLine += TEXT(" pure=true"); }
				}
			}
			Line(Text, 2, NodeLine);
			if (!Node->NodeComment.IsEmpty()) { Line(Text, 3, TEXT("comment=") + Quote(Node->NodeComment)); }
			if (Node->GetDesiredEnabledState() != ENodeEnabledState::Enabled)
			{
				Line(Text, 3, TEXT("enabled=") + FString(Node->GetDesiredEnabledState() == ENodeEnabledState::Disabled ? TEXT("false") : TEXT("development_only")));
			}
			if (Options.bIncludeDebugMetadata)
			{
				Line(Text, 3, TEXT("object=") + Quote(Node->GetPathName()) + TEXT(" guid=") + Node->NodeGuid.ToString()
					+ FString::Printf(TEXT(" position=(%d,%d)"), Node->NodePosX, Node->NodePosY));
			}
			UK2Node_CallFunction* Call = Cast<UK2Node_CallFunction>(Node);
			UFunction* Function = Call ? Call->GetTargetFunction() : nullptr;
			const bool bNative = Function && Function->HasAnyFunctionFlags(FUNC_Native);
			if (Call) { Line(Text, 3, TEXT("function ") + Member(Call->FunctionReference, Format) + (!Format.bDebug && bNative ? TEXT(" impl=native") : TEXT(""))); }
			if (UK2Node_Variable* Variable = Cast<UK2Node_Variable>(Node)) { Line(Text, 3, TEXT("variable ") + Member(Variable->VariableReference, Format)); }
			if (UK2Node_Event* Event = Cast<UK2Node_Event>(Node))
			{
				Line(Text, 3, TEXT("event ") + Member(Event->EventReference, Format) + (Format.bDebug || !Event->CustomFunctionName.IsNone() ? TEXT(" custom_name=") + Quote(Event->CustomFunctionName.ToString()) : TEXT("")));
			}
			if (UK2Node_FunctionEntry* FunctionEntry = Cast<UK2Node_FunctionEntry>(Node))
			{
				Line(Text, 3, FString::Printf(TEXT("function_flags=%d"), FunctionEntry->GetExtraFlags()));
				for (const FBPVariableDescription& Variable : FunctionEntry->LocalVariables)
				{
					Line(Text, 3, TEXT("local_variable ") + Quote(Variable.VarName.ToString())
						+ TEXT(" type=") + Quote(PinType(Variable.VarType, Format)) + TEXT(" default=") + Quote(Format.Value(Variable.VarType, Variable.DefaultValue)));
				}
			}
			if (UEdGraph* Target = CalledGraph(Node))
			{
				// Root definitions already present in this asset can always be referenced.
				const bool bAvailable = Context.GraphIds.Contains(Target);
				Line(Text, 3, TEXT("call ref=") + Context.Ref(Target) + (Format.bDebug || !bAvailable ? TEXT(" target=") + Quote(Format.Path(Target->GetPathName())) : TEXT(""))
					+ (bAvailable ? TEXT("") : TEXT(" reason=depth_limit")));
			}
			else if ((Call && (!bNative || Format.bDebug)) || Cast<UK2Node_MacroInstance>(Node) || Cast<UK2Node_Composite>(Node))
			{
				Line(Text, 3, TEXT("call ref=external reason=native_or_unavailable"));
			}
			NodeProperties(Node, Text, Options, Format);
			for (UEdGraphPin* Pin : Node->Pins)
			{
				if (!Pin || !PinIds.Contains(Pin)) { continue; }
				FString Value = TEXT("pin ") + PinIds.FindChecked(Pin)
					+ (Pin->Direction == EGPD_Input ? TEXT(" in ") : TEXT(" out "))
					+ Quote(PinName(Pin, Format.bDebug)) + TEXT(" type=") + Quote(PinType(Pin->PinType, Format));
				if (Pin->ParentPin) { Value += TEXT(" parent=") + PinIds.FindRef(Pin->ParentPin); }
				if (Pin->bHidden) { Value += TEXT(" hidden=true"); }
				if (Pin->bOrphanedPin) { Value += TEXT(" orphaned=true"); }
				if (Pin->Direction == EGPD_Input && Pin->LinkedTo.Num() == 0 && Pin->SubPins.Num() == 0
					&& !Pin->bDefaultValueIsIgnored && Pin->PinType.PinCategory != TEXT("exec"))
				{
					if (Pin->DefaultObject) { Value += TEXT(" default_object=") + Quote(Format.Path(Pin->DefaultObject->GetPathName())); }
					else if (!Pin->DefaultTextValue.IsEmpty()) { Value += TEXT(" default_text=") + Quote(Pin->DefaultTextValue.ToString()); }
					else { Value += TEXT(" default=") + Quote(Format.Value(Pin->PinType, Pin->DefaultValue)); }
				}
				Line(Text, 3, Value);
			}
		}
		TArray<FString> Links;
		for (UEdGraphNode* Node : Nodes)
		{
			for (UEdGraphPin* Pin : Node->Pins)
			{
				if (!Pin || Pin->Direction != EGPD_Input || !PinIds.Contains(Pin)) { continue; }
				for (UEdGraphPin* Linked : Pin->LinkedTo)
				{
					if (!Linked) { continue; }
					UEdGraphPin* Source = Format.bDebug ? Linked : SourceThroughReroutes(Linked);
					const FString* SourceId = PinIds.Find(Source);
					FString SourceText;
					if (SourceId) { SourceText = *SourceId; }
					else if (Entry.bSelection && Source->GetOwningNode()->GetGraph() == Graph) { SourceText = Unselected(Source, Format.bDebug); }
					else { SourceText = TEXT("external:") + Quote(Source->GetOwningNode()->GetPathName() + TEXT(".") + Source->PinName.ToString()); }
					Links.Add(SourceText + TEXT(" -> ") + PinIds.FindChecked(Pin));
				}
			}
		}
		if (Entry.bSelection)
		{
			for (UEdGraphNode* Node : Nodes)
			{
				for (UEdGraphPin* Pin : Node->Pins)
				{
					if (!Pin || Pin->Direction != EGPD_Output || !PinIds.Contains(Pin)) { continue; }
					TSet<UEdGraphPin*> Seen;
					TArray<UEdGraphPin*> Targets;
					DownstreamPins(Pin, PinIds, Format.bDebug, Seen, Targets);
					for (UEdGraphPin* Target : Targets)
					{
						if (!PinIds.Contains(Target)) { Links.Add(PinIds.FindChecked(Pin) + TEXT(" -> ") + Unselected(Target, Format.bDebug)); }
					}
				}
			}
		}
		Links.Sort();
		if (Links.Num()) { Line(Text, 2, TEXT("links:")); }
		for (const FString& Link : Links) { Line(Text, 3, Link); }
	}
}

bool FUE2CodeBlueprintExporter::ExportBlueprintToString(UBlueprint* Blueprint, const FUE2CodeExportOptions& Options, FString& OutText, FString& OutError)
{
	using namespace UE2CodeBlueprintExporterPrivate;
	OutText.Reset();
	OutError.Reset();
	if (!Blueprint) { OutError = TEXT("Blueprint is null."); return false; }
	FContext Context;
	Context.Format.bDebug = Options.bIncludeDebugMetadata;
	FFormatter& Format = Context.Format;
	Context.MaxLayer = Options.NodeHierarchyDepth > 0 ? Options.NodeHierarchyDepth : FMath::Max(1, Options.MaxFunctionDepth);
	const UE2CodeSelection::FNodeSelection Selection(Options);
	Context.Selection = &Selection;
	TArray<UEdGraph*> Graphs;
	Blueprint->GetAllGraphs(Graphs);
	Graphs.RemoveAll([](UEdGraph* Graph) { return !Graph; });
	Graphs.Sort([](const UEdGraph& A, const UEdGraph& B) { return A.GetPathName() < B.GetPathName(); });
	for (UEdGraph* Graph : Graphs)
	{
		if (!Selection.IsActive())
		{
			if (Graph->GetOuter() == Blueprint) { Context.Add(Graph, 1, true); }
		}
		// Selected nodes may live in any graph of the asset, including collapsed graphs.
		else if (Graph->Nodes.ContainsByPredicate([&Selection](UEdGraphNode* Node) { return Node && Selection.Contains(Node, Node->NodeGuid); }))
		{
			Context.Add(Graph, 1, true, true);
		}
	}
	if (Selection.IsActive() && Context.Graphs.Num() == 0)
	{
		OutError = UE2CodeSelection::NoMatchError(Blueprint->GetName());
		return false;
	}
	// Breadth-first collection assigns shared definitions their shortest call depth.
	for (int32 Index = 0; Index < Context.Graphs.Num(); ++Index)
	{
		const FGraphEntry Entry = Context.Graphs[Index];
		if (Entry.Layer >= Context.MaxLayer) { continue; }
		for (UEdGraphNode* Node : Entry.bSelection ? ExportedNodes(Entry, Context) : SortedNodes(Entry.Graph)) { Context.Add(CalledGraph(Node), Entry.Layer + 1, false); }
		if (Entry.bSelection) { continue; }
		for (UEdGraph* SubGraph : Entry.Graph->SubGraphs) { Context.Add(SubGraph, Entry.Layer + 1, false); }
	}
	// A selection export only declares the variables its exported nodes use.
	TSet<FName> UsedVariables;
	int32 SelectedNodeCount = 0;
	if (Selection.IsActive())
	{
		for (const FGraphEntry& Entry : Context.Graphs)
		{
			for (UEdGraphNode* Node : ExportedNodes(Entry, Context))
			{
				if (Entry.bSelection) { ++SelectedNodeCount; }
				if (UK2Node_Variable* Variable = Cast<UK2Node_Variable>(Node)) { UsedVariables.Add(Variable->VariableReference.GetMemberName()); }
			}
		}
	}
	Line(OutText, 0, TEXT("UE_NODE2CODE blueprint_export version=2"));
	Line(OutText, 0, TEXT("type_aliases: K2_=K2Node_"));
	Line(OutText, 0, TEXT("asset: ") + Quote(Format.Path(Blueprint->GetPathName())));
	Line(OutText, 0, TEXT("parent_class: ") + Quote(Blueprint->ParentClass ? Format.Path(Blueprint->ParentClass->GetPathName()) : TEXT("None")));
	Line(OutText, 0, FString::Printf(TEXT("hierarchy_depth: %d"), FMath::Max(0, Options.NodeHierarchyDepth)));
	if (Selection.IsActive()) { Line(OutText, 0, UE2CodeSelection::HeaderLine(SelectedNodeCount)); }
	Line(OutText, 0, TEXT("variables:"));
	UObject* Defaults = Blueprint->GeneratedClass ? Blueprint->GeneratedClass->GetDefaultObject(false) : nullptr;
	for (const FBPVariableDescription& Variable : Blueprint->NewVariables)
	{
		if (Selection.IsActive() && !UsedVariables.Contains(Variable.VarName)) { continue; }
		FString Default = Variable.DefaultValue;
		const FProperty* DefaultProperty = nullptr;
		if (Defaults)
		{
			if (FProperty* Property = FindFProperty<FProperty>(Defaults->GetClass(), Variable.VarName))
			{
				Default.Reset();
				DefaultProperty = Property;
				UE2CodeEngineCompat::ExportPropertyText(Property, Default, Property->ContainerPtrToValuePtr<const void>(Defaults), nullptr);
			}
		}
		Default = DefaultProperty ? Format.Property(DefaultProperty, Default) : Format.Value(Variable.VarType, Default);
		Line(OutText, 1, TEXT("- ") + Quote(Variable.VarName.ToString()) + TEXT(" type=") + Quote(PinType(Variable.VarType, Format)) + TEXT(" default=") + Quote(Default));
	}
	Line(OutText, 0, TEXT("graphs:"));
	for (const FGraphEntry& Entry : Context.Graphs) { ExportGraph(Entry, Context, Options, OutText); }
	return true;
}

bool FUE2CodeBlueprintExporter::ExportBlueprintToText(UBlueprint* Blueprint, const FString& OutputFilePath, const FUE2CodeExportOptions& Options, FString& OutError)
{
	FString Text;
	if (!ExportBlueprintToString(Blueprint, Options, Text, OutError)) { return false; }
	if (OutputFilePath.TrimStartAndEnd().IsEmpty()) { OutError = TEXT("Output file path is empty."); return false; }
	const FString Path = FPaths::ConvertRelativePathToFull(OutputFilePath);
	IFileManager::Get().MakeDirectory(*FPaths::GetPath(Path), true);
	if (!FFileHelper::SaveStringToFile(Text, *Path, FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM))
	{
		OutError = FString::Printf(TEXT("Failed to write Blueprint export: %s"), *Path);
		return false;
	}
	return true;
}

bool FUE2CodeBlueprintExporter::ExportBlueprintAssetPathToText(const FString& BlueprintAssetPath, const FString& OutputFilePath, const FUE2CodeExportOptions& Options, FString& OutError)
{
	OutError.Reset();
	FString Path = FUE2CodeMaterialExporter::NormalizeMaterialAssetPath(BlueprintAssetPath);
	if (Path.IsEmpty()) { OutError = TEXT("Blueprint asset path is empty."); return false; }
	if (!Path.Contains(TEXT("."))) { Path += TEXT(".") + FPackageName::GetShortName(Path); }
	UBlueprint* Blueprint = LoadObject<UBlueprint>(nullptr, *Path);
	if (!Blueprint) { OutError = FString::Printf(TEXT("Could not load Blueprint asset: %s"), *Path); return false; }
	return ExportBlueprintToText(Blueprint, OutputFilePath, Options, OutError);
}
