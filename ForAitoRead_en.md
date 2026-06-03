# For AI to Read: UE Node2Code Export Text Rules

This document is for AI readers. Follow these rules when reading `.ue2code.txt` files exported by `UE_Node2Code`.

## 1. Core Rules

- The export is structured source text for a material graph, not an asset path list.
- `ME_MFC` means `MaterialExpressionMaterialFunctionCall`; it is not a basic computation node.
- When you see `function_ref: FN001`, read the matching block in `function_definitions`.
- Each function definition is written once. Multiple call sites reuse it through `function_ref`.
- `function_ref: unavailable` means the function was not expanded. The reason is on the same line. Do not pretend to know its internals.

## 2. Header

```text
UE_NODE2CODE material_export version=2
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

## 4. Outputs

```text
outputs: index name mask rgba
  0 "RGB" 1 1110
  1 "R" 1 1000
```

A single default output is usually omitted. Named or multiple outputs are listed.

## 5. Material Functions

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

## 6. Properties

```text
properties:
  - Texture = T_IceDecal_normal
  - SamplerType = SAMPLERTYPE_Normal
```

- Asset paths are shortened by default. The short name is a semantic hint, not a readable local path.
- If a node has no non-default properties, `properties` is omitted.
- Default values, empty values, editor UI state, GUIDs, and full object paths are usually omitted.

## 7. Reroute And Layout

- Passthrough Reroute nodes are omitted. `A -> Reroute -> B` is exported as `A -> B`.
- `layout_hint` is only a rough layout hint, not execution order.
- Internal nodes of built-in engine functions usually do not include layout information.

## 8. Recommended Reading Flow

1. Check `node_hierarchy_depth`.
2. Start from `material_outputs`, `root_connection`, or `root_node`.
3. Trace upstream through `inputs` and `call_inputs`.
4. When you see `function_ref`, jump to `function_definitions`.
5. When you see `function_ref: unavailable`, state clearly that the function was not expanded.
