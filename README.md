# UE Node2Code

[中文](#中文) | [English](#english)

UE Node2Code is an Unreal Engine 4.26+ editor plugin that exports Material, Material Function, Niagara Function Script, and Niagara Module Script graphs into compact, AI-readable text. It recursively expands called Material and Niagara graphs through one shared hierarchy-depth rule and produces text that is practical to paste into web-based AI chat tools.

Default exports are optimized for AI context windows: unreachable nodes, volatile metadata, ineffective fallbacks, and layout data are omitted unless explicitly requested.

## 中文

### 简介

`UE_Node2Code` 用于把 UE 材质、材质函数、Niagara Function Script 和 Niagara Module Script 节点图导出为结构化文本，让 AI 能够阅读图的真实计算逻辑。

它不会只停留在表层调用节点。默认情况下，插件会打开 Material Function 或 Niagara FunctionCall 指向的脚本，继续导出内部图；嵌套调用也会按统一层级规则递归展开，直到基础节点或用户设置的深度限制。

当前版本支持材质节点、Material Function、Niagara Function Script 和 Niagara Module Script 直接导出。蓝图节点暂未支持。

源码目标是 UE 4.26 及后续版本通用。当前材质与 Niagara 导出已在 UE 4.26 和 UE 5.8 完成插件打包编译与自动化测试。

默认导出针对 AI 上下文窗口做了裁剪：不可达节点、易变调试元数据、无效回退值和布局信息不会输出，除非用户显式启用对应选项。

### 主要功能

- 导出整个材质图。
- 直接导出 Material Function 内部图。
- 直接导出 Niagara Function Script，包含函数输入/输出、节点、引脚类型、默认值和连线。
- 直接导出 Niagara Module Script，包含模块输入/输出以及完整图语义。
- 按层级递归展开 Niagara FunctionCall 指向的 Function、Module 或 Dynamic Input 图，并对定义去重。
- 导出单个材质属性链，例如 `MP_BaseColor`、`MP_Normal`。
- 导出单个材质节点的上游链。
- 提供 UE 编辑器 GUI：`Window > UE Node2Code`。
- 支持控制台命令和 C++/蓝图调用。
- 支持 `NodeHierarchyDepth` 统一控制材质与 Niagara 被调用图的展开层级。
- 同一个 Material Function 定义只导出一次，多次调用使用同一个 `function_ref`。
- 普通和 Named Reroute 透传节点会被内联，不作为独立计算节点输出。
- 输出有效材质设置，以及 Material Instance / Material Function Instance 在继承链中最终生效的参数覆盖。
- 材质 v3 默认省略完整资源路径、GUID、坐标、未连接引脚和无效默认值，但保留未连接输入当前实际使用的回退常量。Niagara v2 使用图内短节点/引脚 ID、箭头连线和去重的被调用图定义；原始 GUID 与坐标只在调试元数据模式输出。
- 默认输出不含时间戳，并按稳定标识排序，便于缓存、比较和重复提交给 AI。
- 整材质导出默认只包含能追溯到材质输出的节点；未引用节点需显式勾选后才输出。
- 使用缩进式精简格式：短节点 ID、可读箭头连线、资源短名和去重定义。

### 仓库结构

```text
UE_Node2Code/
  UE_Node2Code_Plugin/
    Source/
    UE_Node2Code.uplugin
  ForAitoRead_zh.md
  ForAitoRead_en.md
  README.md
```

`UE_Node2Code` 是 GitHub 仓库根目录。`UE_Node2Code_Plugin` 是实际插件目录，可直接复制进 UE 项目的 `Plugins` 目录，也作为 GitHub Releases 的发布包来源。

不要提交 `Binaries/`、`Intermediate/` 或本地打包输出目录。

### 安装

把 `UE_Node2Code_Plugin` 目录复制到 UE 项目的 `Plugins` 目录：

```text
<YourProject>/Plugins/UE_Node2Code_Plugin
```

然后重新打开项目，在 `Edit > Plugins` 中确认 `UE Node2Code` 已启用。

如果插件需要重新编译，请使用 UE 4.26 或更高版本打开项目，或从源码打包。

### GUI 使用

在 UE 编辑器菜单中打开：

```text
Window > UE Node2Code
```

窗口选项：

| 选项 | 说明 |
| --- | --- |
| `Graph Asset` | 材质、材质函数、Niagara Function Script 或 Niagara Module Script 的资源路径、对象路径或 `.uasset` 文件路径 |
| `Use Selected Asset` | 使用当前选中的受支持图资源，并自动切换导出模式 |
| `Output File` | 输出 `.ue2code.txt` 文件；默认保存到 `<Project>/UE_Node2Code/` |
| `Export Mode` | 选择材质、材质函数、Niagara Function/Module Script、属性链或节点链 |
| `Material Property` | 属性模式下使用，例如 `MP_BaseColor` |
| `Node Name` | 节点模式下使用，例如 `MaterialExpressionMultiply_3` |
| `Node Hierarchy Depth` | 通用于全部图类型；控制 Material Function 和 Niagara 调用图的递归展开层级 |
| `Export Unreferenced Material Nodes` | 是否包含未被材质输出引用的节点；默认关闭以避免无效文本 |
| `Include Debug Metadata` | 输出对象路径、节点 GUID、时间戳和节点位置等额外调试信息 |
| `Include Default-Like Properties` | 输出通常被过滤的普通默认/空属性；已连接输入对应的无效回退值仍会剔除，不影响 Niagara 引脚默认值 |

### 导出模式

| 模式 | 用途 |
| --- | --- |
| `Material` | 导出整个材质图 |
| `Material Function` | 直接导出 Material Function 内部图 |
| `Niagara Function Script` | 导出 Function Usage 的 Niagara Script 图 |
| `Niagara Module Script` | 导出 Module Usage 的 Niagara Script 图 |
| `Material Property` | 只导出某个材质属性的上游链 |
| `Material Node` | 只导出某个节点的上游链 |

### NodeHierarchyDepth

| 值 | 含义 |
| --- | --- |
| `0` | 默认值，递归展开全部被调用图，直到基础节点或达到安全上限 |
| `1` | 只记录当前根图，不展开任何调用 |
| `2` | 展开根图直接调用的 Material Function / Niagara Script |
| `3` | 再展开下一层嵌套调用 |
| `4...N` | 继续按自然数增加可展开的图层级 |

当层级限制阻止展开时，材质写入 `function_ref: unavailable ...`，Niagara 写入：

```text
call: name="..." usage=DynamicInput ref=external reason=depth_limit
```

### 控制台命令

```text
UE_Node2Code.ExportMaterial <MaterialAssetPathOrUAssetFile> <OutputFilePath> [NodeHierarchyDepth]
UE_Node2Code.ExportMaterialFunction <MaterialFunctionAssetPathOrUAssetFile> <OutputFilePath> [NodeHierarchyDepth]
UE_Node2Code.ExportNiagaraFunctionScript <ScriptAssetPathOrUAssetFile> <OutputFilePath> [NodeHierarchyDepth]
UE_Node2Code.ExportNiagaraModuleScript <ScriptAssetPathOrUAssetFile> <OutputFilePath> [NodeHierarchyDepth]
UE_Node2Code.ExportMaterialProperty <MaterialAssetPathOrUAssetFile> <MaterialProperty> <OutputFilePath> [NodeHierarchyDepth]
UE_Node2Code.ExportMaterialNode <MaterialAssetPathOrUAssetFile> <ExpressionObjectName> <OutputFilePath> [NodeHierarchyDepth]
```

示例：

```text
UE_Node2Code.ExportMaterial /Game/Test/MaterialTest C:/Temp/MaterialExport.ue2code.txt 0
UE_Node2Code.ExportMaterialFunction /Engine/Functions/Engine_MaterialFunctions02/Utility/DebugFloat3Values C:/Temp/DebugFloat3Values.ue2code.txt 0
UE_Node2Code.ExportNiagaraFunctionScript /Niagara/Functions/RandomBool.RandomBool C:/Temp/RandomBool.ue2code.txt 0
UE_Node2Code.ExportNiagaraModuleScript /Niagara/Modules/Emitter/SpawnRate.SpawnRate C:/Temp/SpawnRate.ue2code.txt 2
UE_Node2Code.ExportMaterial /Game/Test/MaterialTest C:/Temp/MaterialExport.depth1.ue2code.txt 1
UE_Node2Code.ExportMaterialProperty /Game/Test/MaterialTest MP_BaseColor C:/Temp/BaseColor.ue2code.txt 2
```

常见 `MaterialProperty`：

```text
MP_BaseColor
MP_EmissiveColor
MP_Metallic
MP_Specular
MP_Roughness
MP_Normal
MP_Opacity
MP_OpacityMask
MP_AmbientOcclusion
MP_WorldPositionOffset
MP_PixelDepthOffset
```

### 输出格式示例

```text
UE_NODE2CODE material_export version=3
type_aliases:
  ME=MaterialExpression; MF=MaterialFunction; ME_TS=MaterialExpressionTextureSample

material_settings: domain=MD_Surface blend=BLEND_Opaque shading=MSM_DefaultLit two_sided=false
effective_parameter_overrides:
  - scalar name="Roughness" value=0.35
material_outputs:
  - MP_EmissiveColor <- ME_MFC_2[0]

material_nodes: count=1
  node ME_TS_1 type=ME_TS role=basic
    caption: "Texture Sample"
    outputs:
      - [0] "RGB" channels=RGB
      - [1] "R" channels=R
    properties:
      - Texture = T_IceDecal_normal
      - SamplerType = SAMPLERTYPE_Normal
```

Niagara Function Script 与 Module Script 使用 v2 格式，并共享签名、短 ID、箭头连线和被调用图定义：

```text
UE_NODE2CODE niagara_function_script_export version=2
graph:
  script: "ExampleFunction"
  usage: Function
  hierarchy_depth: 2
root_graph:
  signature:
    inputs:
      - "Probability" : Float default="0.5"
    outputs:
      - "Result" : Bool
  nodes: count=2
    node N001 type=Input title="Probability"
      pins:
        - P001 out "Probability" : Float
  connections: count=1
    - N001.P001 "Probability" -> N002.P002 "A"
called_graphs: count=1
  script NS001 name="NestedFunction" usage=DynamicInput
```

Module Script 使用对应的头；内部结构相同：

```text
UE_NODE2CODE niagara_module_script_export version=2
graph:
  script: "SpawnRate"
  usage: Module
```

AI 阅读规则见：

```text
ForAitoRead_zh.md
ForAitoRead_en.md
```

### 从源码打包

使用对应 UE 版本的 `RunUAT BuildPlugin`：

```text
<UE>/Engine/Build/BatchFiles/RunUAT.bat BuildPlugin ^
  -Plugin="<Repo>/UE_Node2Code_Plugin/UE_Node2Code.uplugin" ^
  -Package="<PackageOutputDir>" ^
  -TargetPlatforms=Win64 ^
  -Rocket
```

`<PackageOutputDir>` 可以是任意临时输出目录。当前仓库不再保留独立 build 文件夹；需要免编译版本时，把 `UE_Node2Code_Plugin` 或 BuildPlugin 产物打包上传到 GitHub Releases。

### 常见问题

**输出文本仍然很长怎么办？**

先尝试 `NodeHierarchyDepth=1`，或只导出单个材质属性链。如果 AI 需要完整内部原理，再使用 `NodeHierarchyDepth=0`。

**命令行导出成功，但 UE4Editor-Cmd 返回失败怎么办？**

先检查日志中是否有与目标材质无关的资源版本错误。项目扫描阶段的资源错误可能导致命令行返回失败，但不一定表示插件导出失败。

**为什么只保留纹理短名，不保留完整路径？**

默认目标是网页版 AI。它无法读取本地资源文件，完整路径通常没有帮助。短名更利于理解语义，例如 `T_IceDecal_normal`。

### 路线图

- 支持更多材质节点语义压缩。
- 支持蓝图节点导出。
- 增加更多导出格式选项。
- 继续增加更多 UE 版本兼容性测试。

### 许可

当前仓库尚未指定开源许可证。公开发布前建议添加 `LICENSE` 文件。

## English

### Overview

`UE_Node2Code` exports Unreal Engine Material, Material Function, Niagara Function Script, and Niagara Module Script graphs into structured text so AI tools can read their actual logic.

It does not stop at surface-level call nodes. By default, the plugin opens Material Functions and Niagara FunctionCall targets, exports their internal graphs, and recursively follows nested calls until basic nodes or the configured hierarchy limit.

The current version supports Material nodes, direct Material Function export, Niagara Function Script export, and Niagara Module Script export. Blueprint nodes are not supported yet.

The source target is Unreal Engine 4.26 and later. The current Material and Niagara exporters have passed plugin packaging builds and automation tests on UE 4.26 and UE 5.8.

Default exports are pruned for AI context windows: unreachable nodes, volatile debug metadata, ineffective fallbacks, and layout data are omitted unless explicitly requested.

### Features

- Export a full material graph.
- Export a Material Function graph directly.
- Export a Niagara Function Script with its signature, nodes, pin types, defaults, and connections.
- Export a Niagara Module Script with its module inputs/outputs and complete graph semantics.
- Recursively expand Niagara FunctionCall targets, including Function, Module, and Dynamic Input graphs, with deduplicated definitions.
- Export one material property chain, such as `MP_BaseColor` or `MP_Normal`.
- Export the upstream chain of one material expression.
- Editor GUI: `Window > UE Node2Code`.
- Console commands and C++/Blueprint callable APIs.
- Shared Material/Niagara called-graph expansion control through `NodeHierarchyDepth`.
- Deduplicate repeated Material Function definitions with `function_ref`.
- Inline normal and Named Reroute passthrough nodes.
- Export effective material settings and parameter overrides inherited by Material Instances and Material Function Instances.
- Material v3 omits full asset paths, GUIDs, coordinates, unconnected pins, and ineffective defaults while retaining effective fallback constants. Niagara v2 uses graph-local short node/pin IDs, arrow edges, and deduplicated called-graph definitions; original GUIDs and coordinates are debug-only.
- Omit timestamps and use stable ordering by default so exports are cacheable and diff-friendly.
- Full-material exports contain only nodes reachable from material outputs by default; exporting unreferenced nodes is opt-in.
- Compact indented output: short node IDs, readable arrow edges, short asset names, and deduplicated definitions.

### Repository Layout

```text
UE_Node2Code/
  UE_Node2Code_Plugin/
    Source/
    UE_Node2Code.uplugin
  ForAitoRead_zh.md
  ForAitoRead_en.md
  README.md
```

`UE_Node2Code` is the GitHub repository root. `UE_Node2Code_Plugin` is the actual plugin directory; copy it into a project's `Plugins` directory and use it as the source for GitHub Releases.

Do not commit `Binaries/`, `Intermediate/`, or local packaged output directories.

### Installation

Copy `UE_Node2Code_Plugin` into your Unreal project:

```text
<YourProject>/Plugins/UE_Node2Code_Plugin
```

Reopen the project and check `Edit > Plugins` to make sure `UE Node2Code` is enabled.

If recompilation is required, open the project with UE 4.26 or later, or build the plugin from source.

### GUI Usage

Open the editor window from:

```text
Window > UE Node2Code
```

Window options:

| Option | Description |
| --- | --- |
| `Graph Asset` | Material, Material Function, Niagara Function Script, or Niagara Module Script asset/object/`.uasset` path |
| `Use Selected Asset` | Use the selected supported graph asset and switch mode automatically |
| `Output File` | Target `.ue2code.txt` file; defaults to `<Project>/UE_Node2Code/` |
| `Export Mode` | Select Material, Material Function, Niagara Function/Module Script, property-chain, or node-chain export |
| `Material Property` | Used in property mode, for example `MP_BaseColor` |
| `Node Name` | Used in node mode, for example `MaterialExpressionMultiply_3` |
| `Node Hierarchy Depth` | Applies to every graph type; controls Material Function and Niagara called-graph expansion |
| `Export Unreferenced Material Nodes` | Include nodes not referenced by material outputs; off by default to avoid irrelevant text |
| `Include Debug Metadata` | Include object paths, node GUIDs, timestamps, positions, and other debug metadata |
| `Include Default-Like Properties` | Include ordinary default or empty properties that are usually filtered; ineffective fallbacks for connected inputs remain omitted, and Niagara pin defaults are unaffected |

### Export Modes

| Mode | Purpose |
| --- | --- |
| `Material` | Export the whole material graph |
| `Material Function` | Export the internal graph of a Material Function directly |
| `Niagara Function Script` | Export a Niagara Script whose Usage is Function |
| `Niagara Module Script` | Export a Niagara Script whose Usage is Module |
| `Material Property` | Export only the upstream chain of one material property |
| `Material Node` | Export only the upstream chain of one expression |

### NodeHierarchyDepth

| Value | Meaning |
| --- | --- |
| `0` | Default. Recursively expand all called graphs until basic nodes or the safety limit |
| `1` | Current root graph only; do not expand calls |
| `2` | Expand Material Functions / Niagara Scripts called directly by the root |
| `3` | Also expand the next nested call layer |
| `4...N` | Continue increasing the allowed graph hierarchy depth |

When a call is not expanded because of the depth limit, Material uses `function_ref: unavailable ...`; Niagara uses:

```text
call: name="..." usage=DynamicInput ref=external reason=depth_limit
```

### Console Commands

```text
UE_Node2Code.ExportMaterial <MaterialAssetPathOrUAssetFile> <OutputFilePath> [NodeHierarchyDepth]
UE_Node2Code.ExportMaterialFunction <MaterialFunctionAssetPathOrUAssetFile> <OutputFilePath> [NodeHierarchyDepth]
UE_Node2Code.ExportNiagaraFunctionScript <ScriptAssetPathOrUAssetFile> <OutputFilePath> [NodeHierarchyDepth]
UE_Node2Code.ExportNiagaraModuleScript <ScriptAssetPathOrUAssetFile> <OutputFilePath> [NodeHierarchyDepth]
UE_Node2Code.ExportMaterialProperty <MaterialAssetPathOrUAssetFile> <MaterialProperty> <OutputFilePath> [NodeHierarchyDepth]
UE_Node2Code.ExportMaterialNode <MaterialAssetPathOrUAssetFile> <ExpressionObjectName> <OutputFilePath> [NodeHierarchyDepth]
```

Examples:

```text
UE_Node2Code.ExportMaterial /Game/Test/MaterialTest C:/Temp/MaterialExport.ue2code.txt 0
UE_Node2Code.ExportMaterialFunction /Engine/Functions/Engine_MaterialFunctions02/Utility/DebugFloat3Values C:/Temp/DebugFloat3Values.ue2code.txt 0
UE_Node2Code.ExportNiagaraFunctionScript /Niagara/Functions/RandomBool.RandomBool C:/Temp/RandomBool.ue2code.txt 0
UE_Node2Code.ExportNiagaraModuleScript /Niagara/Modules/Emitter/SpawnRate.SpawnRate C:/Temp/SpawnRate.ue2code.txt 2
UE_Node2Code.ExportMaterial /Game/Test/MaterialTest C:/Temp/MaterialExport.depth1.ue2code.txt 1
UE_Node2Code.ExportMaterialProperty /Game/Test/MaterialTest MP_BaseColor C:/Temp/BaseColor.ue2code.txt 2
```

Common `MaterialProperty` values:

```text
MP_BaseColor
MP_EmissiveColor
MP_Metallic
MP_Specular
MP_Roughness
MP_Normal
MP_Opacity
MP_OpacityMask
MP_AmbientOcclusion
MP_WorldPositionOffset
MP_PixelDepthOffset
```

### Output Format Example

```text
UE_NODE2CODE material_export version=3
type_aliases:
  ME=MaterialExpression; MF=MaterialFunction; ME_TS=MaterialExpressionTextureSample

material_settings: domain=MD_Surface blend=BLEND_Opaque shading=MSM_DefaultLit two_sided=false
effective_parameter_overrides:
  - scalar name="Roughness" value=0.35
material_outputs:
  - MP_EmissiveColor <- ME_MFC_2[0]

material_nodes: count=1
  node ME_TS_1 type=ME_TS role=basic
    caption: "Texture Sample"
    outputs:
      - [0] "RGB" channels=RGB
      - [1] "R" channels=R
    properties:
      - Texture = T_IceDecal_normal
      - SamplerType = SAMPLERTYPE_Normal
```

Niagara Function Scripts and Module Scripts use v2 signatures, short IDs, arrow edges, and called-graph definitions:

```text
UE_NODE2CODE niagara_function_script_export version=2
graph:
  script: "ExampleFunction"
  usage: Function
  hierarchy_depth: 2
root_graph:
  signature:
    inputs:
      - "Probability" : Float default="0.5"
    outputs:
      - "Result" : Bool
  nodes: count=2
    node N001 type=Input title="Probability"
      pins:
        - P001 out "Probability" : Float
  connections: count=1
    - N001.P001 "Probability" -> N002.P002 "A"
called_graphs: count=1
  script NS001 name="NestedFunction" usage=DynamicInput
```

Module Scripts use the corresponding header and the same internal structure:

```text
UE_NODE2CODE niagara_module_script_export version=2
graph:
  script: "SpawnRate"
  usage: Module
```

AI reading rules:

```text
ForAitoRead_zh.md
ForAitoRead_en.md
```

### Build From Source

Use `RunUAT BuildPlugin` from the Unreal Engine version you target:

```text
<UE>/Engine/Build/BatchFiles/RunUAT.bat BuildPlugin ^
  -Plugin="<Repo>/UE_Node2Code_Plugin/UE_Node2Code.uplugin" ^
  -Package="<PackageOutputDir>" ^
  -TargetPlatforms=Win64 ^
  -Rocket
```

`<PackageOutputDir>` can be any temporary output directory. This repository no longer keeps a separate build folder. For no-compile downloads, package `UE_Node2Code_Plugin` or the BuildPlugin output and upload it through GitHub Releases.

### FAQ

**What if the exported text is still too long?**

Try `NodeHierarchyDepth=1` or export only one material property chain. Use `NodeHierarchyDepth=0` when AI needs the full internal implementation.

**What if command-line export succeeds but UE4Editor-Cmd returns failure?**

Check the log for unrelated asset version errors. Asset registry errors during project scanning may cause commandlet failure even when the plugin already wrote the output file.

**Why are texture paths shortened?**

The default target is web-based AI. It cannot read local asset files, so full paths are usually not useful. Short names are better semantic hints, such as `T_IceDecal_normal`.

### Roadmap

- Add more material-node semantic compression.
- Add Blueprint node export.
- Add more output format options.
- Continue adding compatibility tests across Unreal Engine versions.

### License

This project is licensed under the MIT License. See [LICENSE](LICENSE).
