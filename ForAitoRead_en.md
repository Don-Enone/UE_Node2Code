# For AI to Read: UE Node2Code Export Text Rules

This document is for AI readers. Follow these rules when reading `.ue2code.txt` files exported by `UE_Node2Code`.

## 1. Core Rules

- The export is structured source text for a Material or Material Function graph, not an asset path list.
- `ME_MFC` means `MaterialExpressionMaterialFunctionCall`; it is not a basic computation node.
- When you see `function_ref: FN001`, read the matching block in `function_definitions`.
- Each function definition is written once. Multiple call sites reuse it through `function_ref`.
- `function_ref: unavailable` means the function was not expanded. The reason is on the same line. Do not pretend to know its internals.
- In Niagara exports, `called_script` is an external script reference; the current format does not include that script's internal graph.

## 2. Header

```text
UE_NODE2CODE material_export version=2
UE_NODE2CODE material_function_export version=2
UE_NODE2CODE niagara_function_script_export version=1
UE_NODE2CODE niagara_module_script_export version=1
aliases:
  ME=MaterialExpression; MF=MaterialFunction; ME_CM=MaterialExpressionComponentMask
```

- `aliases` maps short names to full Unreal type names.
- `ME_CM_23` means `MaterialExpressionComponentMask_23`.
- `id` is the short node ID.
- `type` is the short type name.
- `role` describes the node role: `basic`, `function_call`, `function_input`, or `function_output`.

## 3. Connections

Normal node inputs:

```text
inputs: index name from out mask rgba
  0 "A" ME_TS_1 0 - -
  1 "B" ME_C_2 0 1 1110
```

Function call inputs:

```text
call_inputs: index name from out mask rgba
  0 "Number" ME_MFC_17 1 - -
```

Meaning:

- `index`: input pin index on the current node.
- `name`: input pin name on the current node.
- `from`: upstream node ID.
- `out`: upstream output index.
- `mask rgba`: channel selection. `- -` means no channel restriction.

Unconnected inputs are omitted.

## 4. Root Outputs

Material exports use:

```text
material_outputs: property from out mask rgba
```

Direct Material Function exports use:

```text
function_outputs: name from out mask rgba
```

Both are reading entry points. Start from these outputs and follow `from` upstream.

## 5. Outputs

```text
outputs: index name mask rgba
  0 "RGB" 1 1110
  1 "R" 1 1000
```

A single default output is usually omitted. Named or multiple outputs are listed.

## 6. Material Functions

Function call:

```text
material_function_call:
  function_asset: DebugScalarValues
  function_ref: FN002
```

Function definition:

```text
function_definitions:
  function_definition_begin id=FN002 asset="DebugScalarValues"
    internal_nodes:
      node_begin ...
      node_end
  function_definition_end
```

Reading order:

1. Read the call site's `call_inputs`.
2. Jump to the matching `function_definition`.
3. Inside the function, use `InputName` on `ME_FI` nodes and `OutputName` on `ME_FO` nodes to understand inputs and outputs.
4. If the function contains another `ME_MFC`, follow its `function_ref` recursively.

`call_output_bindings` is omitted. Infer output names from the call node `outputs` and `ME_FO` nodes in the function definition.

## 7. Properties

```text
properties:
  - Texture = T_IceDecal_normal
  - SamplerType = SAMPLERTYPE_Normal
```

- Asset paths are shortened by default. The short name is a semantic hint, not a readable local path.
- If a node has no non-default properties, `properties` is omitted.
- Default values, empty values, editor UI state, GUIDs, and full object paths are usually omitted.

## 8. Reroute And Layout

- Passthrough Reroute nodes are omitted. `A -> Reroute -> B` is exported as `A -> B`.
- `layout_hint` is only a rough layout hint, not execution order.
- Internal nodes of built-in engine functions usually do not include layout information.

## 9. Niagara Scripts

Niagara Function and Module Script exports use generic nodes and pins. Function signatures use `function_inputs` / `function_outputs`; Module signatures use `module_inputs` / `module_outputs`:

```text
function_inputs:
  - name="Probability" type=Float required=false default="0.5"
function_outputs:
  - name="Result" type=Bool
nodes:
  node_begin id=N001 type=Input
    pins:
      - out name="Probability" type=Float pin_id=11111111-1111-1111-1111-111111111111
  node_end
connections: from_node from_pin from_pin_id to_node to_pin to_pin_id
  N001 "Probability" 11111111-1111-1111-1111-111111111111 N002 "A" 22222222-2222-2222-2222-222222222222
```

- `usage` must match the format header: `Function` for `niagara_function_script_export`, or `Module` for `niagara_module_script_export`. Dynamic Inputs are rejected.
- `in` / `out` are pin directions, `name` is the internal pin name, `display_name` is optional UI text, and `type` is the Niagara type.
- Every pin has a stable `pin_id`. `connections` is the directed edge table from output pins to input pins and repeats both endpoint IDs, so display-name collisions are harmless.
- An unconnected input may have a `default`; `default_ignored=true` means the compiler does not use the serialized default.
- `enabled_state` is semantically significant: a `Disabled` node must not be interpreted as executing normally.
- `called_script`, `called_script_path`, and `called_usage` identify a FunctionCall target; UE5 exports also include `called_script_version` when a version is selected. Its implementation is not recursively expanded.
- `properties` contains non-default editable scalar properties plus required structural data such as Convert `Connections`, Static Switch settings, and propagated FunctionCall parameters. Custom HLSL text uses `\n` for line breaks.

## 10. Recommended Reading Flow

1. For a Material export, check `node_hierarchy_depth`; Niagara Function/Module v1 has no hierarchy-depth field.
2. Start from `material_outputs`, `function_outputs`, `root_connection`, or `root_node`.
3. Trace upstream through `inputs` and `call_inputs`.
4. When you see `function_ref`, jump to `function_definitions`.
5. For Niagara graphs, connect nodes through `connections`; treat `called_script` as an external implementation.
6. When you see `function_ref: unavailable`, state clearly that the function was not expanded.
