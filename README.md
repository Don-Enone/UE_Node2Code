# UE Node2Code

[中文](#中文) | [English](#english)

UE Node2Code is an Unreal Engine 4.26 editor plugin that exports material node graphs into compact, AI-readable text. It can recursively expand Material Functions, deduplicate repeated function definitions, and produce text that is practical to paste into web-based AI chat tools.

Current test exports are compact enough for common AI chat windows: a material node with about 332 shader instructions exports to about 46 KB, and a material node with about 86 shader instructions exports to about 21 KB.

## 中文

### 简介

`UE_Node2Code` 用于把 UE 材质节点图导出为结构化文本，让 AI 能够阅读材质的真实计算逻辑。

它不会只停留在表层 `MaterialFunctionCall` 节点。默认情况下，插件会打开 Material Function，继续导出函数内部节点；如果内部还有嵌套函数，也会继续展开，直到只剩基础材质表达式节点，或达到用户设置的层级/深度限制。

当前版本支持材质节点。蓝图节点暂未支持。

当前测试中，约 332 条 shader instruction 的材质节点导出约 46 KB；约 86 条 shader instruction 的材质节点导出约 21 KB。这个体积通常适合直接复制到网页版 AI 对话窗口。

### 主要功能

- 导出整个材质图。
- 导出单个材质属性链，例如 `MP_BaseColor`、`MP_Normal`。
- 导出单个材质节点的上游链。
- 提供 UE 编辑器 GUI：`Window > UE Node2Code`。
- 支持控制台命令和 C++/蓝图调用。
- 支持 `NodeHierarchyDepth` 控制函数展开层级。
- 同一个 Material Function 定义只导出一次，多次调用使用同一个 `function_ref`。
- Reroute 透传节点会被内联，不作为独立计算节点输出。
- 默认省略完整资源路径、GUID、精确编辑器坐标、未连接引脚、空属性块和默认值。
- 使用 v2 精简格式：文件头别名表、短节点 ID、表格化输入/输出、资源短名。

### 仓库结构

```text
UE_Node2Code/
  Config/
  Source/
  ForAitoRead_zh.md
  ForAitoRead_en.md
  README.md
  UE_Node2Code.uplugin
```

建议 GitHub 仓库只提交源码插件目录。不要提交 `Binaries/`、`Intermediate/` 或任何打包输出目录。

### 安装

把源码插件目录复制到 UE 项目的 `Plugins` 目录：

```text
<YourProject>/Plugins/UE_Node2Code
```

然后重新打开项目，在 `Edit > Plugins` 中确认 `UE Node2Code` 已启用。

如果插件需要重新编译，请使用 UE4.26 打开项目，或从源码打包。

### GUI 使用

在 UE 编辑器菜单中打开：

```text
Window > UE Node2Code
```

窗口选项：

| 选项 | 说明 |
| --- | --- |
| `Material` | 材质资源路径、对象路径或 `.uasset` 文件路径 |
| `Use Selected Material` | 使用当前选中的材质 |
| `Output File` | 输出 `.ue2code.txt` 文件 |
| `Export Mode` | 导出整个材质、某个材质属性链或某个节点上游链 |
| `Material Property` | 属性模式下使用，例如 `MP_BaseColor` |
| `Node Name` | 节点模式下使用，例如 `MaterialExpressionMultiply_3` |
| `Node Hierarchy Depth` | 控制函数展开层级 |
| `Export Unreferenced Material Nodes` | 整材质导出时是否包含未被材质输出引用的节点 |
| `Include Debug Metadata` | 输出对象路径、GUID 等调试信息 |
| `Include Default-Like Properties` | 输出默认值、空值等通常被过滤的信息 |

### 导出模式

| 模式 | 用途 |
| --- | --- |
| `Material` | 导出整个材质图 |
| `Material Property` | 只导出某个材质属性的上游链 |
| `Material Node` | 只导出某个节点的上游链 |

### NodeHierarchyDepth

| 值 | 含义 |
| --- | --- |
| `0` | 默认值，递归展开函数，直到基础节点或达到 `MaxFunctionDepth` |
| `1` | 只记录当前材质图，不展开任何函数 |
| `2` | 展开第一层 Material Function |
| `3` | 展开第二层嵌套 Material Function |
| `4...N` | 继续按自然数增加可展开的嵌套层级 |

当层级限制阻止函数展开时，导出文本会写：

```text
function_ref: unavailable reason="NodeHierarchyDepth=..."
```

### 控制台命令

```text
UE_Node2Code.ExportMaterial <MaterialAssetPathOrUAssetFile> <OutputFilePath> [NodeHierarchyDepth]
UE_Node2Code.ExportMaterialProperty <MaterialAssetPathOrUAssetFile> <MaterialProperty> <OutputFilePath> [NodeHierarchyDepth]
UE_Node2Code.ExportMaterialNode <MaterialAssetPathOrUAssetFile> <ExpressionObjectName> <OutputFilePath> [NodeHierarchyDepth]
```

示例：

```text
UE_Node2Code.ExportMaterial /Game/Test/MaterialTest C:/Temp/MaterialExport.ue2code.txt 0
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
UE_NODE2CODE material_export version=2
aliases:
  ME=MaterialExpression; MF=MaterialFunction; ME_TS=MaterialExpressionTextureSample

material_outputs: property from out mask rgba
  MP_EmissiveColor ME_MFC_2 0 - -

node_begin id="ME_TS_1" type=ME_TS role=basic
  caption: "Texture Sample"
  outputs: index name mask rgba
    0 "RGB" 1 1110
    1 "R" 1 1000
  properties:
    - Texture = T_IceDecal_normal
    - SamplerType = SAMPLERTYPE_Normal
node_end
```

AI 阅读规则见：

```text
ForAitoRead_zh.md
ForAitoRead_en.md
```

### 从源码打包

使用 UE4.26 的 `RunUAT BuildPlugin`：

```text
<UE_4.26>/Engine/Build/BatchFiles/RunUAT.bat BuildPlugin ^
  -Plugin="<Repo>/UE_Node2Code.uplugin" ^
  -Package="<PackageOutputDir>" ^
  -TargetPlatforms=Win64 ^
  -Rocket
```

`<PackageOutputDir>` 可以是任意临时输出目录。打包产物不建议提交到 GitHub 主仓库；需要免编译版本时，可以把打包 zip 放到 GitHub Releases。

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
- 增加跨 UE 版本兼容性测试。

### 许可

当前仓库尚未指定开源许可证。公开发布前建议添加 `LICENSE` 文件。

## English

### Overview

`UE_Node2Code` exports Unreal Engine material node graphs into structured text so AI tools can read the actual material logic.

It does not stop at surface-level `MaterialFunctionCall` nodes. By default, the plugin expands Material Functions, exports their internal nodes, and continues into nested functions until only basic material expressions remain, or until the configured depth limit is reached.

The current version supports material nodes. Blueprint nodes are not supported yet.

In current tests, a material node with about 332 shader instructions exports to about 46 KB, and a material node with about 86 shader instructions exports to about 21 KB. This is usually small enough to paste into a web-based AI chat window.

### Features

- Export a full material graph.
- Export one material property chain, such as `MP_BaseColor` or `MP_Normal`.
- Export the upstream chain of one material expression.
- Editor GUI: `Window > UE Node2Code`.
- Console commands and C++/Blueprint callable APIs.
- Function expansion depth control through `NodeHierarchyDepth`.
- Deduplicate repeated Material Function definitions with `function_ref`.
- Inline passthrough Reroute nodes.
- Omit full asset paths, GUIDs, exact editor coordinates, unconnected pins, empty property blocks, and default values by default.
- Compact v2 output format: alias table, short node IDs, tabular inputs/outputs, and short asset names.

### Repository Layout

```text
UE_Node2Code/
  Config/
  Source/
  ForAitoRead_zh.md
  ForAitoRead_en.md
  README.md
  UE_Node2Code.uplugin
```

For GitHub, commit the source plugin directory only. Do not commit `Binaries/`, `Intermediate/`, or any packaged output directory.

### Installation

Copy the source plugin directory into your Unreal project:

```text
<YourProject>/Plugins/UE_Node2Code
```

Reopen the project and check `Edit > Plugins` to make sure `UE Node2Code` is enabled.

If recompilation is required, open the project with UE4.26 or build the plugin from source.

### GUI Usage

Open the editor window from:

```text
Window > UE Node2Code
```

Window options:

| Option | Description |
| --- | --- |
| `Material` | Material asset path, object path, or `.uasset` file path |
| `Use Selected Material` | Use the currently selected material |
| `Output File` | Target `.ue2code.txt` file |
| `Export Mode` | Export a full material, one property chain, or one node upstream chain |
| `Material Property` | Used in property mode, for example `MP_BaseColor` |
| `Node Name` | Used in node mode, for example `MaterialExpressionMultiply_3` |
| `Node Hierarchy Depth` | Controls function expansion depth |
| `Export Unreferenced Material Nodes` | Include nodes not referenced by material outputs during full-material export |
| `Include Debug Metadata` | Include object paths, GUIDs, and other debug metadata |
| `Include Default-Like Properties` | Include default or empty values that are usually filtered |

### Export Modes

| Mode | Purpose |
| --- | --- |
| `Material` | Export the whole material graph |
| `Material Property` | Export only the upstream chain of one material property |
| `Material Node` | Export only the upstream chain of one expression |

### NodeHierarchyDepth

| Value | Meaning |
| --- | --- |
| `0` | Default. Recursively expand functions until basic nodes or `MaxFunctionDepth` |
| `1` | Current material graph only; do not expand functions |
| `2` | Expand first-level Material Functions only |
| `3` | Expand second-level nested Material Functions |
| `4...N` | Continue increasing the allowed nested expansion depth with any natural number |

When a function is not expanded because of the depth limit, the output contains:

```text
function_ref: unavailable reason="NodeHierarchyDepth=..."
```

### Console Commands

```text
UE_Node2Code.ExportMaterial <MaterialAssetPathOrUAssetFile> <OutputFilePath> [NodeHierarchyDepth]
UE_Node2Code.ExportMaterialProperty <MaterialAssetPathOrUAssetFile> <MaterialProperty> <OutputFilePath> [NodeHierarchyDepth]
UE_Node2Code.ExportMaterialNode <MaterialAssetPathOrUAssetFile> <ExpressionObjectName> <OutputFilePath> [NodeHierarchyDepth]
```

Examples:

```text
UE_Node2Code.ExportMaterial /Game/Test/MaterialTest C:/Temp/MaterialExport.ue2code.txt 0
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
UE_NODE2CODE material_export version=2
aliases:
  ME=MaterialExpression; MF=MaterialFunction; ME_TS=MaterialExpressionTextureSample

material_outputs: property from out mask rgba
  MP_EmissiveColor ME_MFC_2 0 - -

node_begin id="ME_TS_1" type=ME_TS role=basic
  caption: "Texture Sample"
  outputs: index name mask rgba
    0 "RGB" 1 1110
    1 "R" 1 1000
  properties:
    - Texture = T_IceDecal_normal
    - SamplerType = SAMPLERTYPE_Normal
node_end
```

AI reading rules:

```text
ForAitoRead_zh.md
ForAitoRead_en.md
```

### Build From Source

Use Unreal Engine 4.26 `RunUAT BuildPlugin`:

```text
<UE_4.26>/Engine/Build/BatchFiles/RunUAT.bat BuildPlugin ^
  -Plugin="<Repo>/UE_Node2Code.uplugin" ^
  -Package="<PackageOutputDir>" ^
  -TargetPlatforms=Win64 ^
  -Rocket
```

`<PackageOutputDir>` can be any temporary output directory. Packaged build artifacts should not be committed to the main GitHub repository. If you need a no-compile package, upload the packaged zip through GitHub Releases.

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
- Add compatibility tests across Unreal Engine versions.

### License

No open-source license has been specified yet. Add a `LICENSE` file before publishing the repository publicly.
