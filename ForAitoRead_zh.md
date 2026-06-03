# For AI to Read: UE Node2Code 导出文本规范

这份文档给 AI 使用。阅读 `UE_Node2Code` 导出的 `.ue2code.txt` 时，请遵守以下规则。

## 1. 基本原则

- 导出文本是材质图或材质函数图的结构化源码，不是资源路径清单。
- `ME_MFC` 是 `MaterialExpressionMaterialFunctionCall`，不是基础计算节点。
- 遇到 `function_ref: FN001`，必须到文末 `function_definitions` 查找实际函数内部图。
- 同一函数定义只写一次，多次调用只通过 `function_ref` 引用。
- `function_ref: unavailable` 表示函数没有展开，原因写在同一行；不要假装知道其内部实现。

## 2. 常见结构

```text
UE_NODE2CODE material_export version=2
UE_NODE2CODE material_function_export version=2
aliases:
  ME=MaterialExpression; MF=MaterialFunction; ME_CM=MaterialExpressionComponentMask
```

- `aliases` 是缩写表。后文的 `ME_CM_23` 可理解为 `MaterialExpressionComponentMask_23`。
- `id` 是短节点 ID。
- `type` 是短类型名。
- `role` 说明节点角色：`basic`、`function_call`、`function_input`、`function_output`。

## 3. 连接表

普通输入格式：

```text
inputs: index name from out mask rgba
  0 "A" ME_TS_1 0 - -
  1 "B" ME_C_2 0 1 1110
```

函数调用输入格式：

```text
call_inputs: index name from out mask rgba
  0 "Number" ME_MFC_17 1 - -
```

含义：

- `index`：当前节点输入引脚索引。
- `name`：当前节点输入引脚名。
- `from`：上游节点 ID。
- `out`：上游输出索引。
- `mask rgba`：通道选择；`- -` 表示无通道限制。

未连接输入不会输出。

## 4. 根输出

材质导出使用：

```text
material_outputs: property from out mask rgba
```

材质函数直接导出使用：

```text
function_outputs: name from out mask rgba
```

两者都是阅读入口。先从这些输出开始，沿 `from` 追踪上游节点。

## 5. 输出表

```text
outputs: index name mask rgba
  0 "RGB" 1 1110
  1 "R" 1 1000
```

单个默认输出通常省略 `outputs`。多输出或具名输出才会列出。

## 6. Material Function

函数调用：

```text
material_function_call:
  function_asset: DebugScalarValues
  function_ref: FN002
```

函数定义：

```text
function_definitions:
  function_definition_begin id=FN002 asset="DebugScalarValues"
    internal_nodes:
      node_begin ...
      node_end
  function_definition_end
```

阅读步骤：

1. 先读调用点的 `call_inputs`。
2. 再跳到对应 `function_definition`。
3. 在函数内部根据 `ME_FI` 的 `InputName` 和 `ME_FO` 的 `OutputName` 理解输入输出。
4. 如果函数内部还有 `ME_MFC`，继续按 `function_ref` 递归阅读。

`call_output_bindings` 已省略。输出名从调用节点 `outputs` 和函数定义中的 `ME_FO` 推断。

## 7. 属性

```text
properties:
  - Texture = T_IceDecal_normal
  - SamplerType = SAMPLERTYPE_Normal
```

- 资源路径默认压缩为短名。短名是语义提示，不是可读取路径。
- 没有非默认属性时，不输出 `properties`。
- 默认值、空值、编辑器 UI 状态、GUID、完整对象路径通常被省略。

## 8. Reroute 与位置

- Reroute 透传节点默认不输出。`A -> Reroute -> B` 会写成 `A -> B`。
- `layout_hint` 只表示粗略布局，不是执行顺序。
- 引擎内置函数内部节点通常不输出位置信息。

## 9. 阅读流程

1. 先确认 `node_hierarchy_depth`。
2. 从 `material_outputs`、`function_outputs`、`root_connection` 或 `root_node` 开始。
3. 沿 `inputs` / `call_inputs` 追踪上游。
4. 遇到 `function_ref` 就跳到 `function_definitions`。
5. 遇到 `function_ref: unavailable` 时，明确说明该函数未展开。
