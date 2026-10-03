#include "UE2CodeBlueprintExporter.h"
#include "UE2CodeBlueprintTextFormat.h"

#if WITH_DEV_AUTOMATION_TESTS
#include "EdGraph/EdGraph.h"
#include "EdGraph/EdGraphNode.h"
#include "EdGraph/EdGraphPin.h"
#include "EdGraphSchema_K2.h"
#include "Engine/Blueprint.h"
#include "K2Node_CallFunction.h"
#include "K2Node_Composite.h"
#include "K2Node_FunctionEntry.h"
#include "K2Node_MacroInstance.h"
#include "K2Node_Knot.h"
#include "Kismet/KismetMathLibrary.h"
#include "Misc/AutomationTest.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FUE2CodeBlueprintGraphTest, "UE_Node2Code.Blueprint.GraphSemantics",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FUE2CodeBlueprintGraphTest::RunTest(const FString& Parameters)
{
	UBlueprint* Blueprint = NewObject<UBlueprint>();
	Blueprint->ParentClass = UObject::StaticClass();
	UEdGraph* Graph = NewObject<UEdGraph>(Blueprint, TEXT("EventGraph"));
	Graph->Schema = UEdGraphSchema_K2::StaticClass();
	Blueprint->UbergraphPages.Add(Graph);
	UEdGraphNode* Source = NewObject<UEdGraphNode>(Graph, TEXT("Source"));
	UEdGraphNode* Sink = NewObject<UEdGraphNode>(Graph, TEXT("Sink"));
	Graph->Nodes.Add(Source);
	Graph->Nodes.Add(Sink);
	FEdGraphPinType ExecType;
	ExecType.PinCategory = TEXT("exec");
	UEdGraphPin* ExecOut = Source->CreatePin(EGPD_Output, ExecType, TEXT("then"));
	UEdGraphPin* ExecIn = Sink->CreatePin(EGPD_Input, ExecType, TEXT("execute"));
	ExecOut->MakeLinkTo(ExecIn);
	FEdGraphPinType IntType;
	IntType.PinCategory = TEXT("int");
	UEdGraphPin* DataOut = Source->CreatePin(EGPD_Output, IntType, TEXT("Result"));
	UEdGraphPin* DataIn = Sink->CreatePin(EGPD_Input, IntType, TEXT("Value"));
	DataIn->DefaultValue = TEXT("999");
	DataOut->MakeLinkTo(DataIn);
	UEdGraphPin* Zero = Sink->CreatePin(EGPD_Input, IntType, TEXT("Zero"));
	Zero->DefaultValue = TEXT("0");
	FEdGraphPinType StringType;
	StringType.PinCategory = TEXT("string");
	UEdGraphPin* String = Sink->CreatePin(EGPD_Input, StringType, TEXT("Message"));
	String->DefaultValue = TEXT("first\nsecond\\n\"quoted\"");
	FEdGraphPinType MapType;
	MapType.PinCategory = TEXT("name");
	MapType.ContainerType = EPinContainerType::Map;
	MapType.PinValueType.TerminalCategory = TEXT("int");
	Sink->CreatePin(EGPD_Input, MapType, TEXT("Lookup"));
	UK2Node_CallFunction* NativeCall = NewObject<UK2Node_CallFunction>(Graph, TEXT("NativeCall"));
	NativeCall->FunctionReference.SetExternalMember(TEXT("Add_IntInt"), UKismetMathLibrary::StaticClass());
	NativeCall->AllocateDefaultPins();
	Graph->Nodes.Add(NativeCall);
	FBPVariableDescription Variable;
	Variable.VarName = TEXT("Counter");
	Variable.VarType = IntType;
	Variable.DefaultValue = TEXT("7");
	Blueprint->NewVariables.Add(Variable);

	FUE2CodeExportOptions Options;
	FString Text, Error, Repeat;
	TestTrue(TEXT("Blueprint exports"), FUE2CodeBlueprintExporter::ExportBlueprintToString(Blueprint, Options, Text, Error));
	TestTrue(TEXT("Versioned Blueprint header"), Text.StartsWith(TEXT("UE_NODE2CODE blueprint_export version=2")));
	TestTrue(TEXT("Execution link is preserved"), Text.Contains(TEXT("N2.P0 -> N1.P0")));
	TestTrue(TEXT("Data link is preserved"), Text.Contains(TEXT("N2.P1 -> N1.P1")));
	TestTrue(TEXT("Zero fallback remains explicit"), Text.Contains(TEXT("\"Zero\" type=\"int\" default=\"0\"")));
	TestFalse(TEXT("Connected fallback is inactive"), Text.Contains(TEXT("999")));
	TestTrue(TEXT("String escapes are unambiguous"), Text.Contains(TEXT("first\\nsecond\\\\n\\\"quoted\\\"")));
	TestTrue(TEXT("Map key and value types survive"), Text.Contains(TEXT("map<name,int>")));
	TestTrue(TEXT("Variable defaults are exported"), Text.Contains(TEXT("\"Counter\" type=\"int\" default=\"7\"")));
	TestTrue(TEXT("Native call target remains explicit"), Text.Contains(TEXT("name=\"Add_IntInt\"")) && Text.Contains(TEXT("impl=native")));
	TestFalse(TEXT("Static library self pin is redundant"), Text.Contains(TEXT("hidden=true default_object=")));
	TestFalse(TEXT("Debug positions omitted by default"), Text.Contains(TEXT("position=")));
	FUE2CodeBlueprintExporter::ExportBlueprintToString(Blueprint, Options, Repeat, Error);
	TestEqual(TEXT("Repeated exports are deterministic"), Text, Repeat);
	Options.bIncludeDebugMetadata = true;
	FUE2CodeBlueprintExporter::ExportBlueprintToString(Blueprint, Options, Repeat, Error);
	TestTrue(TEXT("Debug metadata is opt-in"), Repeat.Contains(TEXT("position=")));
	const FString Output = FPaths::ProjectSavedDir() / TEXT("UE_Node2CodeTests/Blueprint.ue2code.txt");
	TestTrue(TEXT("Blueprint writes a UTF-8 file"), FUE2CodeBlueprintExporter::ExportBlueprintToText(Blueprint, Output, Options, Error));
	FString Saved;
	TestTrue(TEXT("Exported file can be read"), FFileHelper::LoadFileToString(Saved, *Output));
	TestEqual(TEXT("File and string exports agree"), Saved, Repeat);
	TestFalse(TEXT("Empty file path fails"), FUE2CodeBlueprintExporter::ExportBlueprintToText(Blueprint, TEXT(""), Options, Error));
	TestFalse(TEXT("Null Blueprint fails"), FUE2CodeBlueprintExporter::ExportBlueprintToString(nullptr, Options, Text, Error));
	TestTrue(TEXT("Failure clears stale text and explains the error"), Text.IsEmpty() && !Error.IsEmpty());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FUE2CodeBlueprintSelectionTest, "UE_Node2Code.Blueprint.SelectedNodes",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FUE2CodeBlueprintSelectionTest::RunTest(const FString& Parameters)
{
	UBlueprint* Blueprint = NewObject<UBlueprint>();
	Blueprint->ParentClass = UObject::StaticClass();
	UEdGraph* Graph = NewObject<UEdGraph>(Blueprint, TEXT("EventGraph"));
	Graph->Schema = UEdGraphSchema_K2::StaticClass();
	Blueprint->UbergraphPages.Add(Graph);
	UEdGraph* Other = NewObject<UEdGraph>(Blueprint, TEXT("OtherGraph"));
	Other->Schema = UEdGraphSchema_K2::StaticClass();
	Blueprint->FunctionGraphs.Add(Other);
	UEdGraphNode* Source = NewObject<UEdGraphNode>(Graph, TEXT("Source"));
	UEdGraphNode* Middle = NewObject<UEdGraphNode>(Graph, TEXT("Middle"));
	UEdGraphNode* Sink = NewObject<UEdGraphNode>(Graph, TEXT("Sink"));
	UEdGraphNode* Elsewhere = NewObject<UEdGraphNode>(Other, TEXT("Elsewhere"));
	for (UEdGraphNode* Node : {Source, Middle, Sink})
	{
		Node->CreateNewGuid();
		Graph->Nodes.Add(Node);
	}
	Elsewhere->CreateNewGuid();
	Other->Nodes.Add(Elsewhere);
	FEdGraphPinType ExecType;
	ExecType.PinCategory = TEXT("exec");
	Source->CreatePin(EGPD_Output, ExecType, TEXT("then"))->MakeLinkTo(Middle->CreatePin(EGPD_Input, ExecType, TEXT("execute")));
	Middle->CreatePin(EGPD_Output, ExecType, TEXT("then"))->MakeLinkTo(Sink->CreatePin(EGPD_Input, ExecType, TEXT("execute")));
	FBPVariableDescription Variable;
	Variable.VarName = TEXT("UnusedBySelection");
	Variable.VarType.PinCategory = TEXT("int");
	Blueprint->NewVariables.Add(Variable);

	FUE2CodeExportOptions Options;
	Options.SelectedNodeIds.Add(Middle->NodeGuid.ToString());
	FString Text, Error;
	TestTrue(TEXT("Selected Blueprint nodes export"), FUE2CodeBlueprintExporter::ExportBlueprintToString(Blueprint, Options, Text, Error));
	TestTrue(TEXT("Selection header is written"), Text.Contains(TEXT("selection: nodes=1")));
	TestTrue(TEXT("Selected graph is marked"), Text.Contains(TEXT("scope=selection")));
	TestFalse(TEXT("Only the selected node is exported"), Text.Contains(TEXT("node N1")));
	TestFalse(TEXT("Graphs without selected nodes are omitted"), Text.Contains(TEXT("OtherGraph")));
	TestTrue(TEXT("Incoming boundary link is marked"), Text.Contains(TEXT("unselected:")) && Text.Contains(TEXT("-> N0.P0")));
	TestTrue(TEXT("Outgoing boundary link is marked"), Text.Contains(TEXT("N0.P1 -> unselected:")));
	TestFalse(TEXT("Unreferenced variables are omitted"), Text.Contains(TEXT("UnusedBySelection")));

	Options.SelectedNodeIds = {TEXT("Middle"), Elsewhere->NodeGuid.ToString(EGuidFormats::DigitsWithHyphens)};
	TestTrue(TEXT("Names and hyphenated GUIDs match"), FUE2CodeBlueprintExporter::ExportBlueprintToString(Blueprint, Options, Text, Error));
	TestTrue(TEXT("Selections can span graphs"), Text.Contains(TEXT("selection: nodes=2")) && Text.Contains(TEXT("OtherGraph")));

	Options.SelectedNodeIds = {TEXT("NoSuchNode")};
	TestFalse(TEXT("An unmatched selection fails"), FUE2CodeBlueprintExporter::ExportBlueprintToString(Blueprint, Options, Text, Error));
	TestTrue(TEXT("Unmatched selection explains the error"), Text.IsEmpty() && Error.Contains(TEXT("None of the selected nodes")));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FUE2CodeBlueprintHierarchyTest, "UE_Node2Code.Blueprint.Hierarchy",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FUE2CodeBlueprintHierarchyTest::RunTest(const FString& Parameters)
{
	UBlueprint* Blueprint = NewObject<UBlueprint>();
	UEdGraph* Root = NewObject<UEdGraph>(Blueprint, TEXT("EventGraph"));
	Root->Schema = UEdGraphSchema_K2::StaticClass();
	Blueprint->UbergraphPages.Add(Root);
	UK2Node_Composite* Composite = NewObject<UK2Node_Composite>(Root, TEXT("Collapsed"));
	Root->Nodes.Add(Composite);
	UEdGraph* Inner = NewObject<UEdGraph>(Composite, TEXT("Inner"));
	Inner->Schema = UEdGraphSchema_K2::StaticClass();
	Composite->BoundGraph = Inner;
	Root->SubGraphs.Add(Inner);
	UK2Node_FunctionEntry* Entry = NewObject<UK2Node_FunctionEntry>(Inner, TEXT("Entry"));
	Inner->Nodes.Add(Entry);
	FBPVariableDescription Local;
	Local.VarName = TEXT("LocalCounter");
	Local.VarType.PinCategory = TEXT("int");
	Local.DefaultValue = TEXT("42");
	Entry->LocalVariables.Add(Local);
	UK2Node_Composite* Recursive = NewObject<UK2Node_Composite>(Inner, TEXT("Recursive"));
	Inner->Nodes.Add(Recursive);
	Recursive->BoundGraph = Root;
	UK2Node_Composite* Repeated = NewObject<UK2Node_Composite>(Root, TEXT("Repeated"));
	Repeated->BoundGraph = Inner;
	Root->Nodes.Add(Repeated);
	FUE2CodeExportOptions Options;
	FString Text, Error;
	Options.NodeHierarchyDepth = 1;
	FUE2CodeBlueprintExporter::ExportBlueprintToString(Blueprint, Options, Text, Error);
	TestTrue(TEXT("Depth one marks omitted calls"), Text.Contains(TEXT("reason=depth_limit")));
	TestFalse(TEXT("Depth one excludes nested definition"), Text.Contains(TEXT("graph G1")));
	Options.NodeHierarchyDepth = 2;
	FUE2CodeBlueprintExporter::ExportBlueprintToString(Blueprint, Options, Text, Error);
	TestTrue(TEXT("Depth two expands nested graph"), Text.Contains(TEXT("graph G1")));
	TestTrue(TEXT("Function local defaults survive"), Text.Contains(TEXT("local_variable \"LocalCounter\" type=\"int\" default=\"42\"")));
	TestFalse(TEXT("Shared and recursive graphs are not duplicated"), Text.Contains(TEXT("graph G2")));
	TestTrue(TEXT("Recursive reference points back to root"), Text.Contains(TEXT("call ref=G0")));
	Options.NodeHierarchyDepth = 0;
	Options.MaxFunctionDepth = 1;
	FUE2CodeBlueprintExporter::ExportBlueprintToString(Blueprint, Options, Text, Error);
	TestFalse(TEXT("Unlimited mode respects safety limit"), Text.Contains(TEXT("graph G1")));
	Options.MaxFunctionDepth = 32;
	TestTrue(TEXT("Unlimited mode terminates on cycles"), FUE2CodeBlueprintExporter::ExportBlueprintToString(Blueprint, Options, Text, Error));
	TestTrue(TEXT("Unlimited mode includes nested definition"), Text.Contains(TEXT("graph G1")));
	UBlueprint* Library = NewObject<UBlueprint>();
	UEdGraph* MacroGraph = NewObject<UEdGraph>(Library, TEXT("ExternalMacro"));
	MacroGraph->Schema = UEdGraphSchema_K2::StaticClass();
	Library->MacroGraphs.Add(MacroGraph);
	UK2Node_MacroInstance* Macro = NewObject<UK2Node_MacroInstance>(Root, TEXT("Macro"));
	Macro->SetMacroGraph(MacroGraph);
	Root->Nodes.Add(Macro);
	Options.NodeHierarchyDepth = 1;
	FUE2CodeBlueprintExporter::ExportBlueprintToString(Blueprint, Options, Text, Error);
	TestFalse(TEXT("External macro excluded at depth one"), Text.Contains(TEXT("name=\"ExternalMacro\"")));
	Options.NodeHierarchyDepth = 2;
	FUE2CodeBlueprintExporter::ExportBlueprintToString(Blueprint, Options, Text, Error);
	TestTrue(TEXT("External macro expanded at depth two"), Text.Contains(TEXT("name=\"ExternalMacro\"")));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FUE2CodeBlueprintCompactTest, "UE_Node2Code.Blueprint.CompactSemantics",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FUE2CodeBlueprintCompactTest::RunTest(const FString& Parameters)
{
	using namespace UE2CodeBlueprintTextFormat;
	TestEqual(TEXT("Integral decimal"), Number(TEXT("150.000000")), FString(TEXT("150")));
	TestEqual(TEXT("Significant digits retained"), Number(TEXT("-0.057249000")), FString(TEXT("-0.057249")));
	TestEqual(TEXT("Exponent retained"), Number(TEXT("1.23000e-12")), FString(TEXT("1.23e-12")));
	TestEqual(TEXT("High precision is not rounded"), Number(TEXT("12345678901234567890.123456789000")), FString(TEXT("12345678901234567890.123456789")));
	FFormatter Format;
	FEdGraphPinType StringType;
	StringType.PinCategory = TEXT("string");
	const FString Literal = TEXT("150.000000 Vector0_15_389CAAD94285A27BAA495C8A5E95CEF3");
	TestEqual(TEXT("String values are not rewritten"), Format.Value(StringType, Literal), Literal);
	StringType.PinCategory = TEXT("name");
	TestEqual(TEXT("Name values are not rewritten"), Format.Value(StringType, Literal), Literal);
	TestEqual(TEXT("Generated field suffix removed"), FieldName(TEXT("Vector0_15_389CAAD94285A27BAA495C8A5E95CEF3")), FString(TEXT("Vector0")));
	TestEqual(TEXT("Actual GUID value is retained"), FieldName(TEXT("389CAAD94285A27BAA495C8A5E95CEF3")), FString(TEXT("389CAAD94285A27BAA495C8A5E95CEF3")));
	TestEqual(TEXT("Paths use asset names"), Format.Path(TEXT("/Game/A/Example.Example")), FString(TEXT("Example")));
	TestEqual(TEXT("Same-name assets are disambiguated"), Format.Path(TEXT("/Game/B/Example.Example")), FString(TEXT("/Game/B/Example.Example")));
	TestEqual(TEXT("Repeated path keeps its identity"), Format.Path(TEXT("/Game/A/Example.Example")), FString(TEXT("Example")));

	// A reflected struct with both numeric and string fields exercises typed
	// formatting rather than a global replacement over exported text.
	UScriptStruct* Struct = NewObject<UScriptStruct>();
	const FName GeneratedName(TEXT("Amount_11_389CAAD94285A27BAA495C8A5E95CEF3"));
	Struct->AddCppProperty(new FFloatProperty(Struct, GeneratedName, RF_Public));
	Struct->AddCppProperty(new FStrProperty(Struct, TEXT("Label"), RF_Public));
	const FString StructText = TEXT("(Amount_11_389CAAD94285A27BAA495C8A5E95CEF3=150.000000,Label=\"150.000000,(keep=literal)\")");
	TestEqual(TEXT("Struct values respect field types and quoted delimiters"), Format.StructValue(Struct, StructText),
		FString(TEXT("(Amount=150,Label=\"150.000000,(keep=literal)\")")));
	FEdGraphPinType StructType;
	StructType.PinCategory = TEXT("struct");
	StructType.PinSubCategoryObject = Struct;
	StructType.ContainerType = EPinContainerType::Array;
	TestEqual(TEXT("Arrays of structs are compacted safely"), Format.Value(StructType, TEXT("(") + StructText + TEXT(")")),
		FString(TEXT("((Amount=150,Label=\"150.000000,(keep=literal)\"))")));
	Format.bDebug = true;
	TestEqual(TEXT("Debug struct values stay raw"), Format.StructValue(Struct, StructText), StructText);

	UBlueprint* Blueprint = NewObject<UBlueprint>();
	UEdGraph* Graph = NewObject<UEdGraph>(Blueprint, TEXT("EventGraph"));
	Graph->Schema = UEdGraphSchema_K2::StaticClass();
	Blueprint->UbergraphPages.Add(Graph);
	UEdGraphNode* Source = NewObject<UEdGraphNode>(Graph, TEXT("A_Source"));
	UEdGraphNode* Sink = NewObject<UEdGraphNode>(Graph, TEXT("Z_Sink"));
	Graph->Nodes.Add(Source);
	Graph->Nodes.Add(Sink);
	FEdGraphPinType FloatType;
	FloatType.PinCategory = TEXT("float");
	UEdGraphPin* Out = Source->CreatePin(EGPD_Output, FloatType, TEXT("Value"));
	Source->CreatePin(EGPD_Output, FloatType, TEXT("Unused"));
	UEdGraphPin* In = Sink->CreatePin(EGPD_Input, FloatType, TEXT("Value"));
	UEdGraphPin* Other = Sink->CreatePin(EGPD_Input, FloatType, TEXT("Other"));
	Sink->CreatePin(EGPD_Input, FloatType, TEXT("Radius"))->DefaultValue = TEXT("150.000000");
	UK2Node_Knot* First = NewObject<UK2Node_Knot>(Graph, TEXT("Knot1"));
	UK2Node_Knot* Second = NewObject<UK2Node_Knot>(Graph, TEXT("Knot2"));
	for (UK2Node_Knot* Knot : {First, Second})
	{
		Graph->Nodes.Add(Knot);
		Knot->AllocateDefaultPins();
		Knot->GetInputPin()->PinType = FloatType;
		Knot->GetOutputPin()->PinType = FloatType;
	}
	Out->MakeLinkTo(First->GetInputPin());
	First->GetOutputPin()->MakeLinkTo(Second->GetInputPin());
	Second->GetOutputPin()->MakeLinkTo(In);
	Second->GetOutputPin()->MakeLinkTo(Other);
	FUE2CodeExportOptions Options;
	FString Text, Error;
	FUE2CodeBlueprintExporter::ExportBlueprintToString(Blueprint, Options, Text, Error);
	TestFalse(TEXT("Simple reroutes are inlined"), Text.Contains(TEXT("type=K2_Knot")));
	TestTrue(TEXT("First branch survives reroute inlining"), Text.Contains(TEXT("N0.P0 -> N1.P0")));
	TestTrue(TEXT("Second branch survives reroute inlining"), Text.Contains(TEXT("N0.P0 -> N1.P1")));
	TestFalse(TEXT("Unused data output omitted"), Text.Contains(TEXT("\"Unused\"")));
	TestTrue(TEXT("Numeric pin defaults compacted"), Text.Contains(TEXT("\"Radius\" type=\"float\" default=\"150\"")));
	Options.bIncludeDebugMetadata = true;
	FUE2CodeBlueprintExporter::ExportBlueprintToString(Blueprint, Options, Text, Error);
	TestTrue(TEXT("Debug preserves reroutes"), Text.Contains(TEXT("type=K2Node_Knot")));
	TestTrue(TEXT("Debug preserves unused outputs"), Text.Contains(TEXT("\"Unused\"")));
	TestTrue(TEXT("Debug preserves raw decimals"), Text.Contains(TEXT("150.000000")));
	Options.bIncludeDebugMetadata = false;
	First->GetInputPin()->BreakAllPinLinks();
	Second->GetOutputPin()->MakeLinkTo(First->GetInputPin());
	TestTrue(TEXT("Cyclic reroutes terminate"), FUE2CodeBlueprintExporter::ExportBlueprintToString(Blueprint, Options, Text, Error));
	TestTrue(TEXT("Cyclic reroutes are retained"), Text.Contains(TEXT("type=K2_Knot")));
	return true;
}
#endif
