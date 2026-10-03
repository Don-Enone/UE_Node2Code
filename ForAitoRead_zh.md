# For AI to Read：UE Node2Code 导出文本规范

这份文档给读取 `.ue2code.txt` 的 AI 使用。目标是从最少文本中还原节点图的有效计算语义。

## 1. 格式版本

```text
UE_NODE2CODE material_export version=3
UE_NODE2CODE material_function_export version=3
UE_NODE2CODE material_property_export version=3
UE_NODE2CODE material_node_export version=3
UE_NODE2CODE niagara_function_script_export version=2
UE_NODE2CODE niagara_module_script_export version=2
UE_NODE2CODE blueprint_export version=2
```

材质类型可能使用 `type_aliases`，例如 `ME_Mul=MaterialExpressionMultiply`。后续短类型与节点 ID 应按该表理解。

### 蓝图图结构

`graphs` 中每个图定义使用 `G0`、`G1` 等 ID。目标蓝图资产的全部顶层图均为第 1 层根图，嵌套图和外部调用图按 `hierarchy_depth` 展开。`call ref=G...` 指向复用的定义，也可指向递归调用的已有图。`ref=external reason=depth_limit` 表示深度限制省略了实现；`native_or_unavailable` 表示没有可解析的蓝图实现，不要猜测其内部逻辑。

节点/引脚 ID 在图内有效，如 `N0`、`N0.P0`。引脚包含方向、类型、未连接输入的有效默认值、隐藏/孤立状态和拆分引脚的父引用。`links` 中 `输出引脚 -> 输入引脚` 同时表示执行连线和数据连线。已连接引脚的存储默认值不起作用，因此不导出。字符串中的 `\n` 为换行转义，`\\n` 为字面反斜杠加 n；材质 Custom 的 `Code = |` 则使用保留缩进的实际多行代码。

`function`、`variable`、`event` 标明成员及所有者/作用域；`property` 包含部分节点配置。`variables` 为本资产声明的变量，优先读取已有编译类的默认值，没有编译类则读取编辑器描述默认值；导出器不主动请求编译。未连接的计算节点仍保留，不能假定每个节点都会执行。禁用/仅开发节点会显式标记。原生 C++ 实现、组件模板、Timeline 曲线、Widget 布局、专用动画/第三方节点属性不属于本格式的完整覆盖范围。

蓝图 v2 默认压缩：`K2_` 还原为 `K2Node_`；已确认的原生函数用 `impl=native` 标识外部实现，不能把它当作丢失的连线。纯函数标为 `pure=true`。已有图引用时不重复输出目标路径。资源路径使用短名，重名时保留完整路径区分。数值按类型删除小数末尾零而不舍入，字符串、Name、Text 与真实 GUID 值不做此改写。仅结构体字段和引脚标签移除生成的 `_序号_GUID` 后缀；连线始终以引脚 ID 为准，拆分引脚另有父引用。

单一上游来源的正常转接链会被内联，全部有效连线及分支保留；循环、断开、带注释或孤立引脚的链保留原节点。未使用的数据输出和静态函数冗余的隐藏 self 引脚会省略，执行引脚及函数/宏签名引脚保留，因此引脚编号可能不连续。调试元数据模式保留原始命名、路径、数值写法、转接节点及未使用输出。两种模式均保留注释。

## 2. 通用层级规则

所有支持的图都使用同一个 `hierarchy_depth`：

- `0`：递归展开被调用图，直到基础节点；仍受安全上限保护。
- `1`：只输出当前根图。
- `2`：额外展开根图直接调用的图。
- `N`：展开到第 N 层。

根图始终是第 1 层。材质调用通过 `function_ref=FN...` 指向 `function_definitions`；Niagara 调用通过 `ref=NS...` 指向 `called_graphs`。同一被调用图只定义一次，多处调用复用同一个引用。

`ref=external reason=depth_limit` 或 `function_ref: unavailable` 表示实现未展开。不要猜测外部实现。`reason=recursive_call` 表示检测到循环引用，应跳到已有定义，不要无限递归。

### 仅导出选中节点

出现 `selection: nodes=N` 时，文本只是图的一部分：只包含用户在编辑器中选中的 N 个节点（蓝图图行另标 `scope=selection`）。选中节点调用的图仍按 `hierarchy_depth` 完整展开。连到选区之外的连线会保留端点信息但不展开对方节点：

- 蓝图：`unselected:"节点标题.引脚" -> N0.P1` 或 `N0.P2 -> unselected:"节点标题.引脚"`。
- Niagara：`- unselected:"节点标题" "引脚" -> N001.P002 "A"`；`signature` 仍描述整个脚本。
- 材质：`- [0] "A" <- ME_Mul_3[0] unselected`；`material_outputs` / `function_outputs` 只列出由选中节点直接驱动的输出。

`unselected` 表示该值或执行流来自/去往选区外的未知逻辑，不要猜测其实现；未出现在文本中的节点不代表不存在。

## 3. 材质图

### 阅读入口

```text
material_outputs:
  - MP_BaseColor <- ME_Mul_18[0]
  - MP_OpacityMask <- ME_SS_21[0] channels=R

function_outputs:
  - "Result" <- ME_Add_7[0]

root_connection: <- ME_Mul_18[0]
root_node: ME_Mul_18
```

`<-` 右侧是上游节点，方括号是上游输出索引。`channels=RGB/A/...` 是通道选择；缺省表示不限制通道。

### 节点

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

- `node <ID>` 开始一个节点；缩进结束即节点结束，不使用冗余结束标记。
- `role` 为 `basic`、`function_call`、`function_input` 或 `function_output`。
- 未连接输入不进入 `inputs`。其真正生效的回退常量会进入 `properties`。
- 输入已经连接时，无效的 `Const*` 回退值会被剔除。
- 参数默认值与常量字面值始终保留，包括 `0` 和 `false`。
- 单个无名默认输出通常省略；多输出或具名输出才输出 `outputs`。

### 材质函数

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

先读调用点 `call_inputs`，再跳到对应 `function`。函数内部的节点 ID 只在该函数块内有效。

### 材质设置与实例覆盖

```text
material_settings: domain=MD_Surface blend=BLEND_Masked shading=MSM_DefaultLit two_sided=false opacity_mask_clip=0.333
effective_parameter_overrides:
  - scalar name="Roughness" value=0.35
  - texture name="NormalTexture" value=T_Normal
  - static_switch name="UseDetail" value=true
```

覆盖值已沿实例继承链合并，子实例优先。不能用基础材质或基础函数的默认值替代。

## 4. Niagara 图

### 根图与签名

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

`usage` 必须与头部一致：Function 或 Module。顶层 Dynamic Input 不是当前导出入口，但被调用的 Dynamic Input 可以作为 `called_graphs` 定义展开。

### 节点、引脚和连线

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

- `N...` 和 `P...` 是当前图内的稳定短 ID；进入另一个 `script NS...` 后重新计数。
- 箭头始终从输出引脚指向输入引脚。
- `in` / `out` 是方向，冒号后是 Niagara 类型。
- `container=array|set|map`、`ref=true`、`const=true` 是有效类型限定。
- 未连接输入可以带 `default`；`default_ignored=true` 表示该默认值不会被编译器使用。
- 正常启用状态被省略；只有非默认状态才输出 `state: Disabled/...`。

### 被调用脚本

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

遇到 `ref=NS001` 必须读取对应定义。定义已去重。若是 `ref=external reason=depth_limit|safety_limit|unreadable_graph|missing_script`，只报告该调用为外部或不可读实现。

`properties` 保存非默认可编辑属性以及 Convert `Connections`、Static Switch、FunctionCall 传播参数等必要结构信息。Custom HLSL 换行使用 `\n`。

## 5. 默认压缩规则

- 默认仅保留从材质输出可达的节点；未引用材质节点默认剔除。
- 普通 Reroute 与 Named Reroute 透传节点被内联。
- 默认剔除时间戳、GUID、完整对象路径、节点坐标、普通类默认值和编辑器 UI 状态。
- Niagara 原始节点/引脚 GUID、对象路径、位置和版本 GUID 只在 `debug=true` 时输出。
- 字符串中的引号、反斜杠、换行、制表符分别写为 `\"`、`\\`、`\n`、`\t`。

## 6. 推荐阅读顺序

1. 检查格式头、`hierarchy_depth` 和根图类型。
2. 从 `material_outputs`、`function_outputs`、`root_connection`、`root_node` 或 Niagara `root_graph` 开始。
3. 材质沿 `<-` 逆向追踪；Niagara 沿 `->` 正向连接。
4. 遇到 `FN...` 或 `NS...` 引用时跳到对应去重定义。
5. 区分生效属性、未连接输入默认值与被剔除的编辑器信息。
6. 对任何 `unavailable` / `ref=external` 明确保留未知边界，不补造内部逻辑。
