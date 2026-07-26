# For AI to Read: UE Node2Code Export Rules

Use this guide when reading `.ue2code.txt`. The format preserves useful graph semantics while removing editor noise and redundant data.

## 1. Format Versions

```text
UE_NODE2CODE material_export version=3
UE_NODE2CODE material_function_export version=3
UE_NODE2CODE material_property_export version=3
UE_NODE2CODE material_node_export version=3
UE_NODE2CODE niagara_function_script_export version=2
UE_NODE2CODE niagara_module_script_export version=2
```

Material exports may contain `type_aliases`, such as `ME_Mul=MaterialExpressionMultiply`. Interpret later short types and IDs through that table.

## 2. Shared Hierarchy Rule

Every supported graph uses the same `hierarchy_depth`:

- `0`: recursively expand called graphs to basic nodes, subject to a safety limit.
- `1`: export only the current root graph.
- `2`: also expand graphs called directly by the root.
- `N`: expand through hierarchy layer N.

The root is always layer 1. Material calls use `function_ref=FN...` into `function_definitions`; Niagara calls use `ref=NS...` into `called_graphs`. Each called graph is defined once and reused by every call site.

`ref=external reason=depth_limit` or `function_ref: unavailable` means the implementation is not present. Do not infer it. `reason=recursive_call` points back to an existing definition and must not cause infinite traversal.

## 3. Material Graphs

### Entry Points

```text
material_outputs:
  - MP_BaseColor <- ME_Mul_18[0]
  - MP_OpacityMask <- ME_SS_21[0] channels=R

function_outputs:
  - "Result" <- ME_Add_7[0]

root_connection: <- ME_Mul_18[0]
root_node: ME_Mul_18
```

The right side of `<-` is the upstream node. Brackets identify its output index. Optional `channels=RGB/A/...` selects components.

### Nodes

```text
material_nodes: count=2
  node ME_Mul_18 type=ME_Mul role=basic
    caption: "Multiply"
    inputs:
      - [0] "A" <- ME_TS_2[0]
      - [1] "B" <- ME_SP_5[0]
    outputs:
      - [0] "RGB" channels=RGB
    properties:
      - ConstA = 1
```

- `node <ID>` starts a node; indentation ends it, so no redundant end marker exists.
- `role` is `basic`, `function_call`, `function_input`, or `function_output`.
- Unconnected inputs are omitted from `inputs`. An effective fallback literal appears in `properties`.
- A `Const*` fallback is omitted when its input is connected and the value is ineffective.
- Parameter defaults and literal constants remain explicit, including `0` and `false`.
- One unnamed default output is normally omitted; named or multiple outputs are listed.

### Material Functions

```text
node ME_MFC_9 type=ME_MFC role=function_call
  material_function_call:
    function_asset: MF_Noise
    function_ref: FN001
    call_inputs:
      - [0] "UV" <- ME_TC_1[0]

function_definitions: count=1
  function FN001 asset="MF_Noise"
    internal_nodes:
      node ME_FI_1 type=ME_FI role=function_input
      node ME_FO_8 type=ME_FO role=function_output
```

Read `call_inputs`, then jump to the matching `function`. Node IDs inside a function are scoped to that function block.

### Settings and Instance Overrides

```text
material_settings: domain=MD_Surface blend=BLEND_Masked shading=MSM_DefaultLit two_sided=false opacity_mask_clip=0.333
effective_parameter_overrides:
  - scalar name="Roughness" value=0.35
  - texture name="NormalTexture" value=T_Normal
  - static_switch name="UseDetail" value=true
```

Overrides are already merged through the inheritance chain, with child instances winning. Do not replace them with base-asset defaults.

## 4. Niagara Graphs

### Root and Signature

```text
graph:
  script: "RandomBool"
  usage: Function
  hierarchy_depth: 2

root_graph:
  signature:
    inputs:
      - "Probability" : Float default="0.5"
    outputs:
      - "Result" : Bool
```

Top-level `usage` must match the Function or Module header. A top-level Dynamic Input is not an export entry point, but a called Dynamic Input may be expanded inside `called_graphs`.

### Nodes, Pins, and Edges

```text
  nodes: count=2
    node N001 type=Input title="Probability"
      pins:
        - P001 out "Probability" : Float
    node N002 type=Op title="Less Than"
      pins:
        - P002 in "A" : Float
  connections: count=1
    - N001.P001 "Probability" -> N002.P002 "A"
```

- `N...` and `P...` are stable short IDs scoped to the current graph; numbering restarts in another `script NS...`.
- Arrows always point from an output pin to an input pin.
- `in` / `out` are directions; the value after `:` is the Niagara type.
- `container=array|set|map`, `ref=true`, and `const=true` are meaningful type qualifiers.
- An unconnected input may have `default`; `default_ignored=true` means the compiler ignores it.
- Normal enabled state is omitted. Only non-default state appears as `state: Disabled/...`.

### Called Scripts

```text
node N005 type=FunctionCall title="Safe Divide"
  call: name="Safe Divide" usage=DynamicInput ref=NS001

called_graphs: count=1
  script NS001 name="SafeDivide" usage=DynamicInput
    signature:
      ...
    nodes: count=...
    connections: count=...
```

Follow every `ref=NS001` to its definition. Definitions are deduplicated. For `ref=external reason=depth_limit|safety_limit|unreadable_graph|missing_script`, report an external or unreadable implementation without inventing it.

`properties` retains non-default editable data and required structural information such as Convert `Connections`, Static Switch settings, and propagated FunctionCall parameters. Custom HLSL line breaks use `\n`.

## 5. Default Compaction

- Material exports keep only nodes reachable from outputs by default.
- Normal and Named Reroute passthrough nodes are inlined.
- Timestamps, GUIDs, full object paths, positions, ordinary class defaults, and editor UI state are omitted by default.
- Original Niagara node/pin GUIDs, object paths, positions, and version GUIDs appear only with `debug=true`.
- Quotes, backslashes, newlines, and tabs are escaped as `\"`, `\\`, `\n`, and `\t`.

## 6. Recommended Reading Flow

1. Read the header, `hierarchy_depth`, and root graph type.
2. Start at `material_outputs`, `function_outputs`, `root_connection`, `root_node`, or Niagara `root_graph`.
3. Trace Material inputs upstream through `<-`; connect Niagara edges forward through `->`.
4. Follow every `FN...` or `NS...` reference to its deduplicated definition.
5. Distinguish effective properties and input defaults from omitted editor-only data.
6. Preserve the unknown boundary of every `unavailable` or `ref=external`; never fabricate internal logic.
