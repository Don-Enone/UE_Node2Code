# For AI to Read: UE Node2Code 导出文本规范

这份文档给 AI 使用。阅读 `UE_Node2Code` 导出的 `.ue2code.txt` 时，请遵守以下规则。

## 1. 基本原则

- 导出文本是材质图或材质函数图的结构化源码，不是资源路径清单。
- `ME_MFC` 是 `MaterialExpressionMaterialFunctionCall`，不是基础计算节点。
- 遇到 `function_ref: FN001`，必须到文末 `function_definitions` 查找实际函数内部图。
- 同一函数定义只写一次，多次调用只通过 `function_ref` 引用。
- `function_ref: unavailable` 表示函数没有展开，原因写在同一行；不要假装知道其内部实现。
- Niagara 导出中的 `called_script` 是外部脚本引用；当前格式不会附带该脚本的内部图。

## 2. 常见结构

```text
UE_NODE2CODE material_export version=2
UE_NODE2CODE material_function_export version=2
UE_NODE2CODE niagara_function_script_export version=1
UE_NODE2CODE niagara_module_script_export version=1
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

## 9. Niagara Script

Niagara Function 与 Module Script 导出使用通用节点与引脚结构。Function 签名使用 `function_inputs` / `function_outputs`，Module 签名使用 `module_inputs` / `module_outputs`：

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

- `usage` 必须与格式头匹配：`niagara_function_script_export` 对应 `Function`，`niagara_module_script_export` 对应 `Module`。Dynamic Input 会被拒绝。
- `in` / `out` 是引脚方向，`name` 是内部引脚名，`display_name` 是可选界面文本，`type` 是 Niagara 类型。
- 每个引脚都有稳定的 `pin_id`；`connections` 是从输出到输入的有向边表，并重复两端 ID，因此显示名重名不会造成歧义。
- 未连接输入可带 `default`；`default_ignored=true` 表示编译器不会使用序列化默认值。
- `enabled_state` 具有语义：不能把 `Disabled` 节点解释为正常执行。
- `called_script`、`called_script_path` 和 `called_usage` 表示 FunctionCall 的目标；UE5 在选择了脚本版本时还会输出 `called_script_version`。其内部实现未递归展开。
- `properties` 保存非默认的可编辑标量属性，以及 Convert `Connections`、Static Switch 设置和 FunctionCall 传播参数等必要结构数据。Custom HLSL 正文中的换行写成 `\n`。

## 10. 阅读流程

1. 材质导出先确认 `node_hierarchy_depth`；Niagara Function/Module v1 没有该字段。
2. 从 `material_outputs`、`function_outputs`、`root_connection` 或 `root_node` 开始。
3. 沿 `inputs` / `call_inputs` 追踪上游。
4. 遇到 `function_ref` 就跳到 `function_definitions`。
5. 对 Niagara 图按 `connections` 连接节点；遇到 `called_script` 时把它视为外部实现。
6. 遇到 `function_ref: unavailable` 时，明确说明该函数未展开。
