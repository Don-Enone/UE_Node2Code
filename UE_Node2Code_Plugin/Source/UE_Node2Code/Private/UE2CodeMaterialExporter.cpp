#include "UE2CodeMaterialExporter.h"

#include "UE2CodeEngineCompat.h"

#include "HAL/FileManager.h"
#include "Materials/Material.h"
#include "Materials/MaterialExpression.h"
#include "Materials/MaterialExpressionFunctionInput.h"
#include "Materials/MaterialExpressionFunctionOutput.h"
#include "Materials/MaterialExpressionMaterialFunctionCall.h"
#include "Materials/MaterialFunctionInterface.h"
#include "Materials/MaterialInterface.h"
#include "Misc/DateTime.h"
#include "Misc/EngineVersion.h"
#include "Misc/FileHelper.h"
#include "Misc/PackageName.h"
#include "Misc/Paths.h"
#include "UObject/Package.h"
#include "UObject/UnrealType.h"

DEFINE_LOG_CATEGORY_STATIC(LogUE2CodeMaterialExporter, Log, All);

namespace UE2CodeMaterialExporterPrivate
{
	struct FTermAlias
	{
		const TCHAR* Alias;
		const TCHAR* Full;
	};

	static const FTermAlias* GetTermAliases(int32& OutCount)
	{
		static const FTermAlias Aliases[] =
		{
			{ TEXT("ME"), TEXT("MaterialExpression") },
			{ TEXT("MF"), TEXT("MaterialFunction") },
			{ TEXT("Mat"), TEXT("Material") },
			{ TEXT("MI"), TEXT("MaterialInterface") },
			{ TEXT("ME_TS"), TEXT("MaterialExpressionTextureSample") },
			{ TEXT("ME_TC"), TEXT("MaterialExpressionTextureCoordinate") },
			{ TEXT("ME_TO"), TEXT("MaterialExpressionTextureObject") },
			{ TEXT("ME_MFC"), TEXT("MaterialExpressionMaterialFunctionCall") },
			{ TEXT("ME_FI"), TEXT("MaterialExpressionFunctionInput") },
			{ TEXT("ME_FO"), TEXT("MaterialExpressionFunctionOutput") },
			{ TEXT("ME_CM"), TEXT("MaterialExpressionComponentMask") },
			{ TEXT("ME_Add"), TEXT("MaterialExpressionAdd") },
			{ TEXT("ME_Sub"), TEXT("MaterialExpressionSubtract") },
			{ TEXT("ME_Mul"), TEXT("MaterialExpressionMultiply") },
			{ TEXT("ME_Div"), TEXT("MaterialExpressionDivide") },
			{ TEXT("ME_App"), TEXT("MaterialExpressionAppendVector") },
			{ TEXT("ME_C"), TEXT("MaterialExpressionConstant") },
			{ TEXT("ME_C2"), TEXT("MaterialExpressionConstant2Vector") },
			{ TEXT("ME_C3"), TEXT("MaterialExpressionConstant3Vector") },
			{ TEXT("ME_C4"), TEXT("MaterialExpressionConstant4Vector") },
			{ TEXT("ME_Dot"), TEXT("MaterialExpressionDotProduct") },
			{ TEXT("ME_DDX"), TEXT("MaterialExpressionDDX") },
			{ TEXT("ME_DDY"), TEXT("MaterialExpressionDDY") },
			{ TEXT("ME_If"), TEXT("MaterialExpressionIf") },
			{ TEXT("ME_Sqrt"), TEXT("MaterialExpressionSquareRoot") },
			{ TEXT("ME_SM"), TEXT("MaterialExpressionSphereMask") },
			{ TEXT("ME_SS"), TEXT("MaterialExpressionStaticSwitch") },
			{ TEXT("ME_SB"), TEXT("MaterialExpressionStaticBool") },
			{ TEXT("ME_Custom"), TEXT("MaterialExpressionCustom") }
		};
		OutCount = UE_ARRAY_COUNT(Aliases);
		return Aliases;
	}

	static FString Indent(int32 Depth)
	{
		FString Result;
		for (int32 Index = 0; Index < Depth; ++Index)
		{
			Result += TEXT("  ");
		}
		return Result;
	}

	static void AppendLine(FString& OutText, int32 Depth, const FString& Line)
	{
		OutText += Indent(Depth);
		OutText += Line;
		OutText += LINE_TERMINATOR;
	}

	static FString OneLine(FString Value)
	{
		Value.ReplaceInline(TEXT("\r\n"), TEXT("\\n"));
		Value.ReplaceInline(TEXT("\n"), TEXT("\\n"));
		Value.ReplaceInline(TEXT("\r"), TEXT("\\n"));
		Value.TrimStartAndEndInline();
		return Value;
	}

	static FString Quote(const FString& Value)
	{
		return FString::Printf(TEXT("\"%s\""), *Value);
	}

	static bool IsNoneName(const FString& Value)
	{
		return Value.IsEmpty() || Value == TEXT("None");
	}

	static FString MaybeQuotedName(const FString& Value)
	{
		return IsNoneName(Value) ? TEXT("-") : Quote(Value);
	}

	static FString ShortObjectName(const UObject* Object)
	{
		if (!Object)
		{
			return TEXT("None");
		}
		return Object->GetName();
	}

	static FString AliasForFullTerm(const FString& FullTerm)
	{
		int32 AliasCount = 0;
		const FTermAlias* Aliases = GetTermAliases(AliasCount);
		for (int32 AliasIndex = 0; AliasIndex < AliasCount; ++AliasIndex)
		{
			if (FullTerm == Aliases[AliasIndex].Full)
			{
				return Aliases[AliasIndex].Alias;
			}
		}

		if (FullTerm.StartsWith(TEXT("MaterialExpression")))
		{
			return TEXT("ME_") + FullTerm.RightChop(18);
		}

		return FullTerm;
	}

	static FString ShortExpressionName(const UMaterialExpression* Expression)
	{
		if (!Expression)
		{
			return TEXT("None");
		}

		const FString ClassName = Expression->GetClass()->GetName();
		const FString Alias = AliasForFullTerm(ClassName);
		const FString ObjectName = Expression->GetName();
		if (ObjectName.StartsWith(ClassName))
		{
			return Alias + ObjectName.RightChop(ClassName.Len());
		}

		if (ObjectName.StartsWith(TEXT("MaterialExpression")))
		{
			return TEXT("ME_") + ObjectName.RightChop(18);
		}

		return ObjectName;
	}

	static FString ObjectRef(const UObject* Object)
	{
		if (!Object)
		{
			return TEXT("None");
		}
		return ShortObjectName(Object);
	}

	static FString ExpressionRef(const UMaterialExpression* Expression)
	{
		if (!Expression)
		{
			return TEXT("None");
		}
		return ShortExpressionName(Expression);
	}

	static FString GuidToString(const FGuid& Guid)
	{
		if (!Guid.IsValid())
		{
			return TEXT("Invalid");
		}
		return Guid.ToString(EGuidFormats::DigitsWithHyphens);
	}

	static FString MaskBitsToString(int32 R, int32 G, int32 B, int32 A)
	{
		return FString::Printf(TEXT("%d%d%d%d"), R, G, B, A);
	}

	static bool HasMaskData(const FExpressionInput& Input)
	{
		return Input.Mask != 0
			|| Input.MaskR != 0
			|| Input.MaskG != 0
			|| Input.MaskB != 0
			|| Input.MaskA != 0;
	}

	static bool HasMaskData(const FExpressionOutput& Output)
	{
		return Output.Mask != 0
			|| Output.MaskR != 0
			|| Output.MaskG != 0
			|| Output.MaskB != 0
			|| Output.MaskA != 0;
	}

	static bool IsRerouteExpression(const UMaterialExpression* Expression)
	{
		return Expression && Expression->GetClass()->GetName().Contains(TEXT("Reroute"));
	}

	static FExpressionInput* GetReroutePassthroughInput(const UMaterialExpression* Expression)
	{
#if WITH_EDITOR
		if (!IsRerouteExpression(Expression))
		{
			return nullptr;
		}

		const TArray<FExpressionInput*> Inputs = UE2CodeEngineCompat::GetExpressionInputs(const_cast<UMaterialExpression*>(Expression));
		for (FExpressionInput* Input : Inputs)
		{
			if (Input && Input->Expression)
			{
				return Input;
			}
		}
#endif
		return nullptr;
	}

	static bool ResolveInputSource(const FExpressionInput& Input, const UMaterialExpression*& OutExpression, int32& OutOutputIndex)
	{
		OutExpression = Input.Expression;
		OutOutputIndex = Input.OutputIndex;

		TSet<const UMaterialExpression*> SeenReroutes;
		while (OutExpression)
		{
			if (SeenReroutes.Contains(OutExpression))
			{
				break;
			}

			FExpressionInput* PassthroughInput = GetReroutePassthroughInput(OutExpression);
			if (!PassthroughInput || !PassthroughInput->Expression)
			{
				break;
			}

			SeenReroutes.Add(OutExpression);
			OutExpression = PassthroughInput->Expression;
			OutOutputIndex = PassthroughInput->OutputIndex;
		}

		return OutExpression != nullptr;
	}

	static FString CompactConnectionFields(const FExpressionInput& Input)
	{
		const UMaterialExpression* SourceExpression = nullptr;
		int32 SourceOutputIndex = 0;
		if (!ResolveInputSource(Input, SourceExpression, SourceOutputIndex))
		{
			return TEXT("None - - -");
		}

		const FString MaskValue = HasMaskData(Input) ? FString::FromInt(Input.Mask) : TEXT("-");
		const FString RgbaValue = HasMaskData(Input) ? MaskBitsToString(Input.MaskR, Input.MaskG, Input.MaskB, Input.MaskA) : TEXT("-");
		return FString::Printf(TEXT("%s %d %s %s"), *ExpressionRef(SourceExpression), SourceOutputIndex, *MaskValue, *RgbaValue);
	}

	static FString DescribeInput(const FExpressionInput& Input)
	{
		if (!Input.Expression)
		{
			return TEXT("unconnected");
		}

		const UMaterialExpression* SourceExpression = nullptr;
		int32 SourceOutputIndex = 0;
		if (!ResolveInputSource(Input, SourceExpression, SourceOutputIndex))
		{
			return TEXT("unconnected");
		}

		FString Result = FString::Printf(TEXT("from=%s out=%d"), *ExpressionRef(SourceExpression), SourceOutputIndex);
		if (HasMaskData(Input))
		{
			Result += FString::Printf(TEXT(" mask=%d rgba=%s"), Input.Mask, *MaskBitsToString(Input.MaskR, Input.MaskG, Input.MaskB, Input.MaskA));
		}
		return Result;
	}

	static bool ShouldSkipProperty(const FName PropertyName)
	{
		static const FName NAME_Material(TEXT("Material"));
		static const FName NAME_FunctionProperty(TEXT("Function"));
		static const FName NAME_MaterialFunction(TEXT("MaterialFunction"));
		static const FName NAME_GraphNode(TEXT("GraphNode"));
		static const FName NAME_Outputs(TEXT("Outputs"));
		static const FName NAME_FunctionInputs(TEXT("FunctionInputs"));
		static const FName NAME_FunctionOutputs(TEXT("FunctionOutputs"));
		static const FName NAME_MaterialExpressionEditorX(TEXT("MaterialExpressionEditorX"));
		static const FName NAME_MaterialExpressionEditorY(TEXT("MaterialExpressionEditorY"));
		static const FName NAME_MaterialExpressionGuid(TEXT("MaterialExpressionGuid"));
		static const FName NAME_A(TEXT("A"));
		static const FName NAME_Preview(TEXT("Preview"));
		static const FName NAME_bShowMaskColorsOnPin(TEXT("bShowMaskColorsOnPin"));
		static const FName NAME_bShowInputs(TEXT("bShowInputs"));
		static const FName NAME_bIsParameterExpression(TEXT("bIsParameterExpression"));
		static const FName NAME_bNeedToUpdatePreview(TEXT("bNeedToUpdatePreview"));
		static const FName NAME_bShaderInputData(TEXT("bShaderInputData"));
		static const FName NAME_bRealtimePreview(TEXT("bRealtimePreview"));
		static const FName NAME_bHidePreviewWindow(TEXT("bHidePreviewWindow"));
		static const FName NAME_bShowOutputNameOnPin(TEXT("bShowOutputNameOnPin"));
		static const FName NAME_bShowOutputs(TEXT("bShowOutputs"));
		static const FName NAME_bCommentBubbleVisible(TEXT("bCommentBubbleVisible"));
		static const FName NAME_bCollapsed(TEXT("bCollapsed"));
		static const FName NAME_bShowPreviewWindow(TEXT("bShowPreviewWindow"));
		static const FName NAME_bHidePreviewWindowTransient(TEXT("bHidePreviewWindowTransient"));
		static const FName NAME_Desc(TEXT("Desc"));
		static const FName NAME_MenuCategories(TEXT("MenuCategories"));
		static const FName NAME_Keywords(TEXT("Keywords"));
		static const FName NAME_Id(TEXT("Id"));
		static const FName NAME_SortPriority(TEXT("SortPriority"));

		return PropertyName == NAME_Material
			|| PropertyName == NAME_FunctionProperty
			|| PropertyName == NAME_MaterialFunction
			|| PropertyName == NAME_GraphNode
			|| PropertyName == NAME_Outputs
			|| PropertyName == NAME_FunctionInputs
			|| PropertyName == NAME_FunctionOutputs
			|| PropertyName == NAME_MaterialExpressionEditorX
			|| PropertyName == NAME_MaterialExpressionEditorY
			|| PropertyName == NAME_MaterialExpressionGuid
			|| PropertyName == NAME_A
			|| PropertyName == NAME_Preview
			|| PropertyName == NAME_bShowMaskColorsOnPin
			|| PropertyName == NAME_bShowInputs
			|| PropertyName == NAME_bIsParameterExpression
			|| PropertyName == NAME_bNeedToUpdatePreview
			|| PropertyName == NAME_bShaderInputData
			|| PropertyName == NAME_bRealtimePreview
			|| PropertyName == NAME_bHidePreviewWindow
			|| PropertyName == NAME_bShowOutputNameOnPin
			|| PropertyName == NAME_bShowOutputs
			|| PropertyName == NAME_bCommentBubbleVisible
			|| PropertyName == NAME_bCollapsed
			|| PropertyName == NAME_bShowPreviewWindow
			|| PropertyName == NAME_bHidePreviewWindowTransient
			|| PropertyName == NAME_Desc
			|| PropertyName == NAME_MenuCategories
			|| PropertyName == NAME_Keywords
			|| PropertyName == NAME_Id
			|| PropertyName == NAME_SortPriority;
	}

	static bool IsConnectionStructProperty(const FProperty* Property)
	{
		const FStructProperty* StructProperty = CastField<FStructProperty>(Property);
		if (!StructProperty || !StructProperty->Struct)
		{
			return false;
		}

		const FString StructName = StructProperty->Struct->GetName();
		return StructName.Contains(TEXT("ExpressionInput"))
			|| StructName.Contains(TEXT("MaterialAttributesInput"));
	}

	static bool IsPrintableProperty(const FProperty* Property)
	{
		if (!Property || Property->HasAnyPropertyFlags(CPF_Deprecated))
		{
			return false;
		}

		if (IsConnectionStructProperty(Property))
		{
			return false;
		}

		if (CastField<FArrayProperty>(Property) || CastField<FMapProperty>(Property) || CastField<FSetProperty>(Property))
		{
			return false;
		}

		return CastField<FBoolProperty>(Property)
			|| CastField<FNumericProperty>(Property)
			|| CastField<FNameProperty>(Property)
			|| CastField<FStrProperty>(Property)
			|| CastField<FTextProperty>(Property)
			|| CastField<FEnumProperty>(Property)
			|| CastField<FByteProperty>(Property)
			|| CastField<FObjectPropertyBase>(Property)
			|| CastField<FStructProperty>(Property);
	}

	static bool TryCompactPreviewVector(const FString& ValueText, FString& OutValue)
	{
		if (!ValueText.StartsWith(TEXT("(X=")))
		{
			return false;
		}

		float X = 0.0f;
		float Y = 0.0f;
		float Z = 0.0f;
		float W = 0.0f;
		if (!FParse::Value(*ValueText, TEXT("X="), X)
			|| !FParse::Value(*ValueText, TEXT("Y="), Y)
			|| !FParse::Value(*ValueText, TEXT("Z="), Z)
			|| !FParse::Value(*ValueText, TEXT("W="), W))
		{
			return false;
		}

		if (FMath::IsNearlyZero(Y) && FMath::IsNearlyZero(Z) && FMath::IsNearlyEqual(W, 1.0f))
		{
			OutValue = FString::SanitizeFloat(X);
			return true;
		}

		return false;
	}

	static FString CompactPropertyValue(const FProperty* Property, const FString& ValueText)
	{
		static const FName NAME_PreviewValue(TEXT("PreviewValue"));
		if (Property && Property->GetFName() == NAME_PreviewValue)
		{
			FString CompactPreviewValue;
			if (TryCompactPreviewVector(ValueText, CompactPreviewValue))
			{
				return CompactPreviewValue;
			}
		}

		return ValueText;
	}

	static bool ExportPropertyValue(const FProperty* Property, const void* Container, FString& OutValue)
	{
		if (!Property || !Container)
		{
			return false;
		}

		const void* ValuePtr = Property->ContainerPtrToValuePtr<const void>(Container);
		if (!ValuePtr)
		{
			return false;
		}

		if (const FObjectPropertyBase* ObjectProperty = CastField<FObjectPropertyBase>(Property))
		{
			OutValue = ShortObjectName(ObjectProperty->GetObjectPropertyValue(ValuePtr));
			return !OutValue.IsEmpty();
		}

		UE2CodeEngineCompat::ExportPropertyText(Property, OutValue, ValuePtr, nullptr);
		OutValue = OneLine(OutValue);
		OutValue = CompactPropertyValue(Property, OutValue);
		return !OutValue.IsEmpty();
	}

	static bool IsDefaultLikePropertyValue(const FProperty* Property, const void* Container, const FString& ValueText)
	{
		if (!Property || !Container)
		{
			return true;
		}

		if (ValueText.IsEmpty() || ValueText == TEXT("()"))
		{
			return true;
		}

		if (ValueText == TEXT("None") && CastField<FObjectPropertyBase>(Property))
		{
			return true;
		}

		const UObject* Object = reinterpret_cast<const UObject*>(Container);
		const UObject* Defaults = Object ? Object->GetClass()->GetDefaultObject() : nullptr;
		if (Defaults && Property->Identical_InContainer(Container, Defaults))
		{
			return true;
		}

		const FName PropertyName = Property->GetFName();
		static const FName NAME_SamplerSource(TEXT("SamplerSource"));
		static const FName NAME_AutomaticViewMipBias(TEXT("AutomaticViewMipBias"));
		static const FName NAME_IsDefaultMeshpaintTexture(TEXT("IsDefaultMeshpaintTexture"));
		static const FName NAME_ConstCoordinate(TEXT("ConstCoordinate"));
		static const FName NAME_ConstMipValue(TEXT("ConstMipValue"));
		static const FName NAME_UnMirrorU(TEXT("UnMirrorU"));
		static const FName NAME_UnMirrorV(TEXT("UnMirrorV"));
		static const FName NAME_MipValueMode(TEXT("MipValueMode"));
		static const FName NAME_FunctionParameterInfo(TEXT("FunctionParameterInfo"));
		static const FName NAME_bUsePreviewValueAsDefault(TEXT("bUsePreviewValueAsDefault"));
		static const FName NAME_bLastPreviewed(TEXT("bLastPreviewed"));

		return (PropertyName == NAME_SamplerSource && ValueText == TEXT("SSM_FromTextureAsset"))
			|| (PropertyName == NAME_AutomaticViewMipBias && ValueText == TEXT("True"))
			|| (PropertyName == NAME_IsDefaultMeshpaintTexture && ValueText == TEXT("False"))
			|| (PropertyName == NAME_ConstCoordinate && ValueText == TEXT("0"))
			|| (PropertyName == NAME_ConstMipValue && ValueText == TEXT("-1"))
			|| (PropertyName == NAME_UnMirrorU && ValueText == TEXT("False"))
			|| (PropertyName == NAME_UnMirrorV && ValueText == TEXT("False"))
			|| (PropertyName == NAME_MipValueMode && ValueText == TEXT("TMVM_None"))
			|| (PropertyName == NAME_FunctionParameterInfo && ValueText == TEXT("(Association=GlobalParameter,Index=-1)"))
			|| (PropertyName == NAME_bUsePreviewValueAsDefault && ValueText == TEXT("True"))
			|| (PropertyName == NAME_bLastPreviewed && ValueText == TEXT("True"));
	}

	static UMaterial* GetBaseMaterial(UMaterialInterface* MaterialInterface)
	{
		if (!MaterialInterface)
		{
			return nullptr;
		}
		return MaterialInterface->GetMaterial();
	}

	static FString NormalizePropertyToken(FString Token)
	{
		Token.TrimStartAndEndInline();
		Token.ToUpperInline();
		Token.ReplaceInline(TEXT("_"), TEXT(""));
		if (Token.StartsWith(TEXT("MP")))
		{
			Token = Token.RightChop(2);
		}
		return Token;
	}

	static void SortExpressions(TArray<UMaterialExpression*>& Expressions)
	{
		Expressions.Sort([](const UMaterialExpression& A, const UMaterialExpression& B)
		{
			if (A.MaterialExpressionEditorX == B.MaterialExpressionEditorX)
			{
				if (A.MaterialExpressionEditorY == B.MaterialExpressionEditorY)
				{
					return A.GetName() < B.GetName();
				}
				return A.MaterialExpressionEditorY < B.MaterialExpressionEditorY;
			}
			return A.MaterialExpressionEditorX < B.MaterialExpressionEditorX;
		});
	}

	static bool IsEngineFunction(const UMaterialFunctionInterface* Function)
	{
		return Function && Function->GetPathName().StartsWith(TEXT("/Engine/"), ESearchCase::IgnoreCase);
	}

	static bool ShouldRecordApproximateLayout(const UMaterialFunctionInterface* Function)
	{
		return !Function || !IsEngineFunction(Function);
	}

	static FString LayoutBand(int32 Band)
	{
		switch (Band)
		{
		case 0:
			return TEXT("left_or_upper");
		case 1:
			return TEXT("middle");
		default:
			return TEXT("right_or_lower");
		}
	}

	static void AddUniqueExpression(UMaterialExpression* Expression, TArray<UMaterialExpression*>& Expressions, TSet<UMaterialExpression*>& Seen)
	{
		if (Expression && IsRerouteExpression(Expression) && GetReroutePassthroughInput(Expression))
		{
			Seen.Add(Expression);
			return;
		}

		if (Expression && !Seen.Contains(Expression))
		{
			Seen.Add(Expression);
			Expressions.Add(Expression);
		}
	}

	static void CollectUpstreamExpressions(UMaterialExpression* Expression, TArray<UMaterialExpression*>& Expressions, TSet<UMaterialExpression*>& Seen)
	{
		if (!Expression || Seen.Contains(Expression))
		{
			return;
		}

		if (Expression && IsRerouteExpression(Expression))
		{
			Seen.Add(Expression);
			if (FExpressionInput* PassthroughInput = GetReroutePassthroughInput(Expression))
			{
				CollectUpstreamExpressions(PassthroughInput->Expression, Expressions, Seen);
			}
			return;
		}

		Seen.Add(Expression);
		Expressions.Add(Expression);

#if WITH_EDITOR
		const TArray<FExpressionInput*> Inputs = UE2CodeEngineCompat::GetExpressionInputs(Expression);
		for (FExpressionInput* Input : Inputs)
		{
			if (Input && Input->Expression)
			{
				CollectUpstreamExpressions(Input->Expression, Expressions, Seen);
			}
		}
#endif
	}

	static void RemovePassthroughReroutes(TArray<UMaterialExpression*>& Expressions)
	{
		Expressions.RemoveAll([](UMaterialExpression* Expression)
		{
			return Expression && IsRerouteExpression(Expression) && GetReroutePassthroughInput(Expression);
		});
	}

	static UMaterialExpressionFunctionInput* FindFunctionInputById(const UMaterialFunctionInterface* Function, const FGuid& InputId)
	{
		TArray<UMaterialExpression*> FunctionExpressions;
		if (!UE2CodeEngineCompat::GetFunctionExpressions(Function, FunctionExpressions))
		{
			return nullptr;
		}

		for (UMaterialExpression* Expression : FunctionExpressions)
		{
			UMaterialExpressionFunctionInput* FunctionInput = Cast<UMaterialExpressionFunctionInput>(Expression);
			if (FunctionInput && FunctionInput->Id == InputId)
			{
				return FunctionInput;
			}
		}
		return nullptr;
	}

	static UMaterialExpressionFunctionOutput* FindFunctionOutputById(const UMaterialFunctionInterface* Function, const FGuid& OutputId)
	{
		TArray<UMaterialExpression*> FunctionExpressions;
		if (!UE2CodeEngineCompat::GetFunctionExpressions(Function, FunctionExpressions))
		{
			return nullptr;
		}

		for (UMaterialExpression* Expression : FunctionExpressions)
		{
			UMaterialExpressionFunctionOutput* FunctionOutput = Cast<UMaterialExpressionFunctionOutput>(Expression);
			if (FunctionOutput && FunctionOutput->Id == OutputId)
			{
				return FunctionOutput;
			}
		}
		return nullptr;
	}

	struct FExportContext
	{
		FUE2CodeExportOptions Options;
		TMap<const UMaterialFunctionInterface*, FString> FunctionIds;
		TArray<const UMaterialFunctionInterface*> FunctionOrder;
		TArray<const UMaterialFunctionInterface*> FunctionStack;
		TMap<const UMaterialExpression*, FString> LayoutHints;
		int32 NextFunctionId = 1;
	};

	static void AppendExpressionBlock(UMaterialExpression* Expression, FString& OutText, int32 Depth, FExportContext& Context);
	static void AppendFunctionDefinitions(FString& OutText, int32 Depth, FExportContext& Context);

	static void CacheApproximateLayoutHints(const TArray<UMaterialExpression*>& Expressions, const UMaterialFunctionInterface* OwningFunction, FExportContext& Context)
	{
		if (!ShouldRecordApproximateLayout(OwningFunction))
		{
			for (UMaterialExpression* Expression : Expressions)
			{
				Context.LayoutHints.Remove(Expression);
			}
			return;
		}

		if (Expressions.Num() == 0)
		{
			return;
		}

		int32 MinX = TNumericLimits<int32>::Max();
		int32 MaxX = TNumericLimits<int32>::Lowest();
		int32 MinY = TNumericLimits<int32>::Max();
		int32 MaxY = TNumericLimits<int32>::Lowest();
		for (UMaterialExpression* Expression : Expressions)
		{
			if (!Expression)
			{
				continue;
			}
			MinX = FMath::Min(MinX, Expression->MaterialExpressionEditorX);
			MaxX = FMath::Max(MaxX, Expression->MaterialExpressionEditorX);
			MinY = FMath::Min(MinY, Expression->MaterialExpressionEditorY);
			MaxY = FMath::Max(MaxY, Expression->MaterialExpressionEditorY);
		}

		const int32 RangeX = FMath::Max(1, MaxX - MinX);
		const int32 RangeY = FMath::Max(1, MaxY - MinY);
		for (UMaterialExpression* Expression : Expressions)
		{
			if (!Expression)
			{
				continue;
			}

			const int32 Column = FMath::Clamp(((Expression->MaterialExpressionEditorX - MinX) * 3) / (RangeX + 1), 0, 2);
			const int32 Row = FMath::Clamp(((Expression->MaterialExpressionEditorY - MinY) * 3) / (RangeY + 1), 0, 2);
			Context.LayoutHints.Add(
				Expression,
				FString::Printf(
					TEXT("layout_hint: logical_flow=\"inputs_to_outputs\" rough_column=\"%s\" rough_row=\"%s\""),
					*LayoutBand(Column),
					*LayoutBand(Row)
				)
			);
		}
	}

	static const UMaterialFunctionInterface* ResolveFunctionForExpansion(const UMaterialExpressionMaterialFunctionCall* FunctionCall)
	{
		if (!FunctionCall || !FunctionCall->MaterialFunction)
		{
			return nullptr;
		}

		return UE2CodeEngineCompat::GetBaseFunction(FunctionCall->MaterialFunction);
	}

	static FString GetFunctionDefinitionId(const UMaterialFunctionInterface* Function, FExportContext& Context)
	{
		if (!Function)
		{
			return TEXT("");
		}

		if (const FString* ExistingId = Context.FunctionIds.Find(Function))
		{
			return *ExistingId;
		}

		const FString NewId = FString::Printf(TEXT("FN%03d"), Context.NextFunctionId++);
		Context.FunctionIds.Add(Function, NewId);
		Context.FunctionOrder.Add(Function);
		return NewId;
	}

	static bool RegisterFunctionDefinition(const UMaterialFunctionInterface* Function, FExportContext& Context, FString& OutReason)
	{
		if (!Context.Options.bExpandMaterialFunctions)
		{
			OutReason = TEXT("material function expansion is disabled");
			return false;
		}

		if (!Function)
		{
			OutReason = TEXT("no material function asset is assigned");
			return false;
		}

		if (Context.Options.NodeHierarchyDepth > 0)
		{
			const int32 FunctionGraphLayer = Context.FunctionStack.Num() + 2;
			if (FunctionGraphLayer > Context.Options.NodeHierarchyDepth)
			{
				OutReason = FString::Printf(TEXT("NodeHierarchyDepth=%d keeps this function as an external call"), Context.Options.NodeHierarchyDepth);
				return false;
			}
		}

		if (Context.FunctionIds.Contains(Function))
		{
			return true;
		}

		const int32 MaxDepth = FMath::Max(1, Context.Options.MaxFunctionDepth);
		if (Context.FunctionStack.Num() >= MaxDepth)
		{
			OutReason = FString::Printf(TEXT("MaxFunctionDepth=%d was reached"), Context.Options.MaxFunctionDepth);
			return false;
		}

		if (Context.FunctionStack.Contains(Function))
		{
			GetFunctionDefinitionId(Function, Context);
			OutReason = TEXT("recursive function reference detected; using the existing function definition id");
			return true;
		}

		GetFunctionDefinitionId(Function, Context);

		TArray<UMaterialExpression*> FunctionExpressions;
		if (!UE2CodeEngineCompat::GetFunctionExpressions(Function, FunctionExpressions))
		{
			return true;
		}

		Context.FunctionStack.Add(Function);
		for (UMaterialExpression* Expression : FunctionExpressions)
		{
			if (UMaterialExpressionMaterialFunctionCall* NestedFunctionCall = Cast<UMaterialExpressionMaterialFunctionCall>(Expression))
			{
				FString NestedReason;
				RegisterFunctionDefinition(ResolveFunctionForExpansion(NestedFunctionCall), Context, NestedReason);
			}
		}
		UE2CodeEngineCompat::PopNoShrink(Context.FunctionStack);
		return true;
	}

	static void AppendFunctionCallDetails(UMaterialExpressionMaterialFunctionCall* FunctionCall, FString& OutText, int32 Depth, FExportContext& Context)
	{
		const UMaterialFunctionInterface* FunctionForExpansion = ResolveFunctionForExpansion(FunctionCall);

		AppendLine(OutText, Depth, TEXT("material_function_call:"));
		AppendLine(OutText, Depth + 1, FString::Printf(TEXT("function_asset: %s"), *ObjectRef(FunctionCall->MaterialFunction)));
		if (FunctionForExpansion && FunctionForExpansion != FunctionCall->MaterialFunction)
		{
			AppendLine(OutText, Depth + 1, FString::Printf(TEXT("expanded_base_function_asset: %s"), *ObjectRef(FunctionForExpansion)));
		}

		FString RegisterReason;
		if (RegisterFunctionDefinition(FunctionForExpansion, Context, RegisterReason))
		{
			AppendLine(OutText, Depth + 1, FString::Printf(TEXT("function_ref: %s"), *GetFunctionDefinitionId(FunctionForExpansion, Context)));
			if (!RegisterReason.IsEmpty())
			{
				AppendLine(OutText, Depth + 1, FString::Printf(TEXT("function_ref_note: \"%s\""), *OneLine(RegisterReason)));
			}
		}
		else
		{
			AppendLine(OutText, Depth + 1, FString::Printf(TEXT("function_ref: unavailable reason=\"%s\""), *OneLine(RegisterReason)));
		}

#if WITH_EDITORONLY_DATA
		int32 ConnectedCallInputCount = 0;
		FString CallInputText;
		for (int32 InputIndex = 0; InputIndex < FunctionCall->FunctionInputs.Num(); ++InputIndex)
		{
			const FFunctionExpressionInput& FunctionInput = FunctionCall->FunctionInputs[InputIndex];
			if (!FunctionInput.Input.Expression)
			{
				continue;
			}

			UMaterialExpressionFunctionInput* InputExpression = FunctionInput.ExpressionInput;
			if (!InputExpression)
			{
				InputExpression = FindFunctionInputById(FunctionForExpansion, FunctionInput.ExpressionInputId);
			}

			const FString InputName = InputExpression ? InputExpression->InputName.ToString() : FunctionCall->GetInputName(InputIndex).ToString();
			FString BindingLine = FString::Printf(TEXT("%d %s %s"), InputIndex, *MaybeQuotedName(InputName), *CompactConnectionFields(FunctionInput.Input));
			if (Context.Options.bIncludeDebugMetadata)
			{
				BindingLine += FString::Printf(TEXT(" input_id=%s"), *GuidToString(FunctionInput.ExpressionInputId));
			}
			AppendLine(CallInputText, Depth + 2, BindingLine);
			++ConnectedCallInputCount;
		}
		if (ConnectedCallInputCount > 0)
		{
			AppendLine(OutText, Depth + 1, TEXT("call_inputs: index name from out mask rgba"));
			OutText += CallInputText;
		}
#endif
	}

	static void AppendFunctionDefinition(const UMaterialFunctionInterface* Function, FString& OutText, int32 Depth, FExportContext& Context)
	{
		if (!Function)
		{
			return;
		}

		const FString FunctionId = GetFunctionDefinitionId(Function, Context);
		AppendLine(OutText, Depth, FString::Printf(TEXT("function_definition_begin id=%s asset=\"%s\""), *FunctionId, *ObjectRef(Function)));

		const FString Description = UE2CodeEngineCompat::GetFunctionDescription(Function);
		if (!Description.IsEmpty())
		{
			AppendLine(OutText, Depth + 1, FString::Printf(TEXT("description: \"%s\""), *OneLine(Description)));
		}

		TArray<UMaterialExpression*> InternalExpressions;
		if (!UE2CodeEngineCompat::GetFunctionExpressions(Function, InternalExpressions))
		{
			AppendLine(OutText, Depth + 1, TEXT("internal_nodes: unavailable"));
			AppendLine(OutText, Depth, TEXT("function_definition_end"));
			return;
		}

		RemovePassthroughReroutes(InternalExpressions);
		SortExpressions(InternalExpressions);
		CacheApproximateLayoutHints(InternalExpressions, Function, Context);

		AppendLine(OutText, Depth + 1, TEXT("internal_nodes:"));

		Context.FunctionStack.Add(Function);
		for (UMaterialExpression* InternalExpression : InternalExpressions)
		{
			AppendExpressionBlock(InternalExpression, OutText, Depth + 2, Context);
		}
		UE2CodeEngineCompat::PopNoShrink(Context.FunctionStack);

		AppendLine(OutText, Depth, TEXT("function_definition_end"));
	}

	static void AppendFunctionDefinitions(FString& OutText, int32 Depth, FExportContext& Context)
	{
		if (!Context.Options.bExpandMaterialFunctions)
		{
			return;
		}

		AppendLine(OutText, Depth, TEXT(""));
		AppendLine(OutText, Depth, FString::Printf(TEXT("function_definition_count: %d"), Context.FunctionOrder.Num()));
		if (Context.FunctionOrder.Num() == 0)
		{
			AppendLine(OutText, Depth, TEXT("function_definitions: none"));
			return;
		}

		AppendLine(OutText, Depth, TEXT("function_definitions:"));
		for (int32 FunctionIndex = 0; FunctionIndex < Context.FunctionOrder.Num(); ++FunctionIndex)
		{
			AppendFunctionDefinition(Context.FunctionOrder[FunctionIndex], OutText, Depth + 1, Context);
		}
	}

	static bool ShouldSkipExpressionProperty(UMaterialExpression* Expression, const FName PropertyName)
	{
		if (!Expression)
		{
			return true;
		}

		if (Expression->GetClass()->GetName() == TEXT("MaterialExpressionComponentMask"))
		{
			static const FName NAME_R(TEXT("R"));
			static const FName NAME_G(TEXT("G"));
			static const FName NAME_B(TEXT("B"));
			static const FName NAME_A(TEXT("A"));
			if (PropertyName == NAME_R || PropertyName == NAME_G || PropertyName == NAME_B || PropertyName == NAME_A)
			{
				return true;
			}
		}

		return false;
	}

	static void AppendInspectableProperties(UMaterialExpression* Expression, FString& OutText, int32 Depth, FExportContext& Context)
	{
		int32 PropertyCount = 0;
		FString PropertyText;
		for (TFieldIterator<FProperty> It(Expression->GetClass(), EFieldIteratorFlags::IncludeSuper); It; ++It)
		{
			FProperty* Property = *It;
			if (!Property || ShouldSkipProperty(Property->GetFName()) || ShouldSkipExpressionProperty(Expression, Property->GetFName()) || !IsPrintableProperty(Property))
			{
				continue;
			}

			FString ValueText;
			if (!ExportPropertyValue(Property, Expression, ValueText))
			{
				continue;
			}

			if (!Context.Options.bIncludeDefaultLikeProperties && IsDefaultLikePropertyValue(Property, Expression, ValueText))
			{
				continue;
			}

			AppendLine(
				PropertyText,
				Depth + 1,
				FString::Printf(TEXT("- %s = %s"), *Property->GetName(), *ValueText)
			);
			++PropertyCount;
		}

		if (PropertyCount == 0)
		{
			return;
		}

		AppendLine(OutText, Depth, TEXT("properties:"));
		OutText += PropertyText;
	}

	static bool IsDefaultOutput(const FExpressionOutput& Output)
	{
		return IsNoneName(Output.OutputName.ToString()) && !HasMaskData(Output);
	}

	static void AppendOutputsTable(UMaterialExpression* Expression, FString& OutText, int32 Depth)
	{
#if WITH_EDITOR
		TArray<FExpressionOutput>& Outputs = Expression->GetOutputs();
		if (Outputs.Num() == 0 || (Outputs.Num() == 1 && IsDefaultOutput(Outputs[0])))
		{
			return;
		}

		AppendLine(OutText, Depth, TEXT("outputs: index name mask rgba"));
		for (int32 OutputIndex = 0; OutputIndex < Outputs.Num(); ++OutputIndex)
		{
			const FExpressionOutput& Output = Outputs[OutputIndex];
			const FString MaskValue = HasMaskData(Output) ? FString::FromInt(Output.Mask) : TEXT("-");
			const FString RgbaValue = HasMaskData(Output) ? MaskBitsToString(Output.MaskR, Output.MaskG, Output.MaskB, Output.MaskA) : TEXT("-");
			AppendLine(
				OutText,
				Depth + 1,
				FString::Printf(TEXT("%d %s %s %s"), OutputIndex, *MaybeQuotedName(Output.OutputName.ToString()), *MaskValue, *RgbaValue)
			);
		}
#endif
	}

	static void AppendExpressionBlock(UMaterialExpression* Expression, FString& OutText, int32 Depth, FExportContext& Context)
	{
		if (!Expression)
		{
			return;
		}

		const bool bIsMaterialFunctionCall = Expression->IsA<UMaterialExpressionMaterialFunctionCall>();
		const bool bIsFunctionInput = Expression->IsA<UMaterialExpressionFunctionInput>();
		const bool bIsFunctionOutput = Expression->IsA<UMaterialExpressionFunctionOutput>();

		AppendLine(
			OutText,
			Depth,
			FString::Printf(
				TEXT("node_begin id=\"%s\" type=%s role=%s"),
				*ExpressionRef(Expression),
				*AliasForFullTerm(Expression->GetClass()->GetName()),
				bIsMaterialFunctionCall ? TEXT("function_call") : (bIsFunctionInput ? TEXT("function_input") : (bIsFunctionOutput ? TEXT("function_output") : TEXT("basic")))
			)
		);

		if (const FString* LayoutHint = Context.LayoutHints.Find(Expression))
		{
			AppendLine(OutText, Depth + 1, *LayoutHint);
		}

		if (Context.Options.bIncludeDebugMetadata)
		{
			AppendLine(OutText, Depth + 1, FString::Printf(TEXT("object_path: %s"), *Expression->GetPathName()));
			AppendLine(OutText, Depth + 1, FString::Printf(TEXT("guid: %s"), *GuidToString(Expression->GetMaterialExpressionId())));
		}

#if WITH_EDITOR
		TArray<FString> Captions;
		Expression->GetCaption(Captions);
		FString CaptionText;
		if (Captions.Num() > 0)
		{
			CaptionText = OneLine(FString::Join(Captions, TEXT(" | ")));
			AppendLine(OutText, Depth + 1, FString::Printf(TEXT("caption: \"%s\""), *CaptionText));
		}

		const FString Description = Expression->GetDescription();
		const FString DescriptionText = OneLine(Description);
		if (!DescriptionText.IsEmpty() && (CaptionText.IsEmpty() || !DescriptionText.Contains(CaptionText)))
		{
			AppendLine(OutText, Depth + 1, FString::Printf(TEXT("description: \"%s\""), *DescriptionText));
		}

		if (!bIsMaterialFunctionCall)
		{
			const TArray<FExpressionInput*> Inputs = UE2CodeEngineCompat::GetExpressionInputs(Expression);
			int32 ConnectedInputCount = 0;
			FString InputText;
			for (int32 InputIndex = 0; InputIndex < Inputs.Num(); ++InputIndex)
			{
				FExpressionInput* Input = Inputs[InputIndex];
				const FString InputName = Expression->GetInputName(InputIndex).ToString();
				if (Input && Input->Expression)
				{
					AppendLine(
						InputText,
						Depth + 2,
						FString::Printf(TEXT("%d %s %s"), InputIndex, *MaybeQuotedName(InputName), *CompactConnectionFields(*Input))
					);
					++ConnectedInputCount;
				}
			}
			if (ConnectedInputCount > 0)
			{
				AppendLine(OutText, Depth + 1, TEXT("inputs: index name from out mask rgba"));
				OutText += InputText;
			}
		}

		AppendOutputsTable(Expression, OutText, Depth + 1);
#endif

		if (UMaterialExpressionMaterialFunctionCall* FunctionCall = Cast<UMaterialExpressionMaterialFunctionCall>(Expression))
		{
			AppendFunctionCallDetails(FunctionCall, OutText, Depth + 1, Context);
		}

		AppendInspectableProperties(Expression, OutText, Depth + 1, Context);

		AppendLine(OutText, Depth, TEXT("node_end"));
	}

	static void AppendMaterialRoots(UMaterial* Material, FString& OutText, int32 Depth)
	{
		AppendLine(OutText, Depth, TEXT("material_outputs: property from out mask rgba"));
		int32 ConnectedCount = 0;

		for (int32 PropertyIndex = 0; PropertyIndex < MP_MAX; ++PropertyIndex)
		{
			const EMaterialProperty MaterialProperty = static_cast<EMaterialProperty>(PropertyIndex);
			FExpressionInput* Input = Material->GetExpressionInputForProperty(MaterialProperty);
			if (!Input || !Input->Expression)
			{
				continue;
			}

			AppendLine(
				OutText,
				Depth + 1,
				FString::Printf(
					TEXT("%s %s"),
					*FUE2CodeMaterialExporter::MaterialPropertyToString(MaterialProperty),
					*CompactConnectionFields(*Input)
				)
			);
			++ConnectedCount;
		}

		if (ConnectedCount == 0)
		{
			AppendLine(OutText, Depth + 1, TEXT("- none"));
		}
	}

	static void AppendFunctionOutputs(const UMaterialFunctionInterface* Function, FString& OutText, int32 Depth)
	{
		AppendLine(OutText, Depth, TEXT("function_outputs: name from out mask rgba"));
		int32 ConnectedCount = 0;

		TArray<UMaterialExpression*> FunctionExpressions;
		if (UE2CodeEngineCompat::GetFunctionExpressions(Function, FunctionExpressions))
		{
			for (UMaterialExpression* Expression : FunctionExpressions)
			{
				UMaterialExpressionFunctionOutput* FunctionOutput = Cast<UMaterialExpressionFunctionOutput>(Expression);
				if (!FunctionOutput || !FunctionOutput->A.Expression)
				{
					continue;
				}

				AppendLine(
					OutText,
					Depth + 1,
					FString::Printf(TEXT("%s %s"), *MaybeQuotedName(FunctionOutput->OutputName.ToString()), *CompactConnectionFields(FunctionOutput->A))
				);
				++ConnectedCount;
			}
		}

		if (ConnectedCount == 0)
		{
			AppendLine(OutText, Depth + 1, TEXT("- none"));
		}
	}

	static void AppendAliasTable(FString& OutText)
	{
		AppendLine(OutText, 0, TEXT("aliases:"));

		int32 AliasCount = 0;
		const FTermAlias* Aliases = GetTermAliases(AliasCount);
		FString AliasLine;
		for (int32 AliasIndex = 0; AliasIndex < AliasCount; ++AliasIndex)
		{
			const FString PairText = FString::Printf(TEXT("%s=%s"), Aliases[AliasIndex].Alias, Aliases[AliasIndex].Full);
			if (!AliasLine.IsEmpty() && AliasLine.Len() + PairText.Len() > 180)
			{
				AppendLine(OutText, 1, AliasLine);
				AliasLine.Reset();
			}

			if (!AliasLine.IsEmpty())
			{
				AliasLine += TEXT("; ");
			}
			AliasLine += PairText;
		}

		if (!AliasLine.IsEmpty())
		{
			AppendLine(OutText, 1, AliasLine);
		}
		AppendLine(OutText, 0, TEXT(""));
	}

	static void AppendEngineVersion(FString& OutText)
	{
		AppendLine(OutText, 0, FString::Printf(TEXT("engine_version: %s"), *FEngineVersion::Current().ToString()));
	}

	static bool SaveTextToFile(const FString& OutputFilePath, const FString& Text, FString& OutError)
	{
		if (OutputFilePath.IsEmpty())
		{
			OutError = TEXT("OutputFilePath is empty.");
			return false;
		}

		const FString OutputDirectory = FPaths::GetPath(OutputFilePath);
		if (!OutputDirectory.IsEmpty())
		{
			IFileManager::Get().MakeDirectory(*OutputDirectory, true);
		}

		if (!FFileHelper::SaveStringToFile(Text, *OutputFilePath, FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM))
		{
			OutError = FString::Printf(TEXT("Failed to save text to '%s'."), *OutputFilePath);
			return false;
		}

		return true;
	}
}

bool FUE2CodeMaterialExporter::ExportMaterialAssetPathToText(const FString& MaterialAssetPath, const FString& OutputFilePath, const FUE2CodeExportOptions& Options, FString& OutError)
{
	const FString AssetPath = NormalizeMaterialAssetPath(MaterialAssetPath);
	UMaterialInterface* Material = Cast<UMaterialInterface>(StaticLoadObject(UMaterialInterface::StaticClass(), nullptr, *AssetPath));
	if (!Material && !AssetPath.Contains(TEXT(".")))
	{
		const FString ObjectPath = AssetPath + TEXT(".") + FPackageName::GetShortName(AssetPath);
		Material = Cast<UMaterialInterface>(StaticLoadObject(UMaterialInterface::StaticClass(), nullptr, *ObjectPath));
	}

	if (!Material)
	{
		OutError = FString::Printf(TEXT("Could not load material asset from '%s' (normalized from '%s')."), *AssetPath, *MaterialAssetPath);
		return false;
	}

	return ExportMaterialToText(Material, OutputFilePath, Options, OutError);
}

bool FUE2CodeMaterialExporter::ExportMaterialToText(UMaterialInterface* Material, const FString& OutputFilePath, const FUE2CodeExportOptions& Options, FString& OutError)
{
	FString Text;
	if (!ExportMaterialToString(Material, Options, Text, OutError))
	{
		return false;
	}

	return UE2CodeMaterialExporterPrivate::SaveTextToFile(OutputFilePath, Text, OutError);
}

bool FUE2CodeMaterialExporter::ExportMaterialToString(UMaterialInterface* Material, const FUE2CodeExportOptions& Options, FString& OutText, FString& OutError)
{
	using namespace UE2CodeMaterialExporterPrivate;

	UMaterial* BaseMaterial = GetBaseMaterial(Material);
	if (!BaseMaterial)
	{
		OutError = TEXT("Material is null or does not resolve to a base UMaterial.");
		return false;
	}

	OutText.Reset();
	AppendLine(OutText, 0, TEXT("UE_NODE2CODE material_export version=2"));
	AppendAliasTable(OutText);
	AppendLine(OutText, 0, FString::Printf(TEXT("exported_at_local: %s"), *FDateTime::Now().ToString(TEXT("%Y-%m-%d %H:%M:%S"))));
	AppendEngineVersion(OutText);
	AppendLine(OutText, 0, FString::Printf(TEXT("material_interface: %s"), *ObjectRef(Material)));
	AppendLine(OutText, 0, FString::Printf(TEXT("base_material: %s"), *ObjectRef(BaseMaterial)));
	AppendLine(OutText, 0, FString::Printf(TEXT("options: expand_material_functions=%s function_definitions=deduplicated export_unreferenced_material_expressions=%s node_hierarchy_depth=%d max_function_depth=%d include_debug_metadata=%s include_default_like_properties=%s"), Options.bExpandMaterialFunctions ? TEXT("true") : TEXT("false"), Options.bExportUnreferencedMaterialExpressions ? TEXT("true") : TEXT("false"), Options.NodeHierarchyDepth, Options.MaxFunctionDepth, Options.bIncludeDebugMetadata ? TEXT("true") : TEXT("false"), Options.bIncludeDefaultLikeProperties ? TEXT("true") : TEXT("false")));
	AppendLine(OutText, 0, TEXT(""));

	AppendMaterialRoots(BaseMaterial, OutText, 0);
	AppendLine(OutText, 0, TEXT(""));

	TArray<UMaterialExpression*> Expressions;
	TSet<UMaterialExpression*> SeenExpressions;

	for (int32 PropertyIndex = 0; PropertyIndex < MP_MAX; ++PropertyIndex)
	{
		FExpressionInput* Input = BaseMaterial->GetExpressionInputForProperty(static_cast<EMaterialProperty>(PropertyIndex));
		if (Input && Input->Expression)
		{
			CollectUpstreamExpressions(Input->Expression, Expressions, SeenExpressions);
		}
	}

	if (Options.bExportUnreferencedMaterialExpressions)
	{
		TArray<UMaterialExpression*> MaterialExpressions;
		UE2CodeEngineCompat::GetMaterialExpressions(BaseMaterial, MaterialExpressions);
		for (UMaterialExpression* Expression : MaterialExpressions)
		{
			AddUniqueExpression(Expression, Expressions, SeenExpressions);
		}
	}

	SortExpressions(Expressions);
	RemovePassthroughReroutes(Expressions);

	AppendLine(OutText, 0, FString::Printf(TEXT("material_node_count: %d"), Expressions.Num()));
	AppendLine(OutText, 0, TEXT("material_nodes:"));

	FExportContext Context;
	Context.Options = Options;
	CacheApproximateLayoutHints(Expressions, nullptr, Context);
	for (UMaterialExpression* Expression : Expressions)
	{
		AppendExpressionBlock(Expression, OutText, 1, Context);
	}
	AppendFunctionDefinitions(OutText, 0, Context);

	OutError.Reset();
	return true;
}

bool FUE2CodeMaterialExporter::ExportMaterialFunctionAssetPathToText(const FString& MaterialFunctionAssetPath, const FString& OutputFilePath, const FUE2CodeExportOptions& Options, FString& OutError)
{
	const FString AssetPath = NormalizeMaterialAssetPath(MaterialFunctionAssetPath);
	UMaterialFunctionInterface* MaterialFunction = Cast<UMaterialFunctionInterface>(StaticLoadObject(UMaterialFunctionInterface::StaticClass(), nullptr, *AssetPath));
	if (!MaterialFunction && !AssetPath.Contains(TEXT(".")))
	{
		const FString ObjectPath = AssetPath + TEXT(".") + FPackageName::GetShortName(AssetPath);
		MaterialFunction = Cast<UMaterialFunctionInterface>(StaticLoadObject(UMaterialFunctionInterface::StaticClass(), nullptr, *ObjectPath));
	}

	if (!MaterialFunction)
	{
		OutError = FString::Printf(TEXT("Could not load material function asset from '%s' (normalized from '%s')."), *AssetPath, *MaterialFunctionAssetPath);
		return false;
	}

	return ExportMaterialFunctionToText(MaterialFunction, OutputFilePath, Options, OutError);
}

bool FUE2CodeMaterialExporter::ExportMaterialFunctionToText(UMaterialFunctionInterface* MaterialFunction, const FString& OutputFilePath, const FUE2CodeExportOptions& Options, FString& OutError)
{
	FString Text;
	if (!ExportMaterialFunctionToString(MaterialFunction, Options, Text, OutError))
	{
		return false;
	}

	return UE2CodeMaterialExporterPrivate::SaveTextToFile(OutputFilePath, Text, OutError);
}

bool FUE2CodeMaterialExporter::ExportMaterialFunctionToString(UMaterialFunctionInterface* MaterialFunction, const FUE2CodeExportOptions& Options, FString& OutText, FString& OutError)
{
	using namespace UE2CodeMaterialExporterPrivate;

	if (!MaterialFunction)
	{
		OutError = TEXT("MaterialFunction is null.");
		return false;
	}

	const UMaterialFunctionInterface* FunctionForExport = UE2CodeEngineCompat::GetBaseFunction(MaterialFunction);

	TArray<UMaterialExpression*> Expressions;
	if (!UE2CodeEngineCompat::GetFunctionExpressions(FunctionForExport, Expressions))
	{
		OutError = FString::Printf(TEXT("Material function '%s' has no readable expression list."), *ObjectRef(FunctionForExport));
		return false;
	}

	RemovePassthroughReroutes(Expressions);
	SortExpressions(Expressions);

	OutText.Reset();
	AppendLine(OutText, 0, TEXT("UE_NODE2CODE material_function_export version=2"));
	AppendAliasTable(OutText);
	AppendLine(OutText, 0, FString::Printf(TEXT("exported_at_local: %s"), *FDateTime::Now().ToString(TEXT("%Y-%m-%d %H:%M:%S"))));
	AppendEngineVersion(OutText);
	AppendLine(OutText, 0, FString::Printf(TEXT("material_function: %s"), *ObjectRef(MaterialFunction)));
	if (FunctionForExport != MaterialFunction)
	{
		AppendLine(OutText, 0, FString::Printf(TEXT("base_material_function: %s"), *ObjectRef(FunctionForExport)));
	}
	const FString Description = UE2CodeEngineCompat::GetFunctionDescription(FunctionForExport);
	if (!Description.IsEmpty())
	{
		AppendLine(OutText, 0, FString::Printf(TEXT("description: \"%s\""), *OneLine(Description)));
	}
	AppendLine(OutText, 0, FString::Printf(TEXT("options: expand_material_functions=%s function_definitions=deduplicated node_hierarchy_depth=%d max_function_depth=%d include_debug_metadata=%s include_default_like_properties=%s"), Options.bExpandMaterialFunctions ? TEXT("true") : TEXT("false"), Options.NodeHierarchyDepth, Options.MaxFunctionDepth, Options.bIncludeDebugMetadata ? TEXT("true") : TEXT("false"), Options.bIncludeDefaultLikeProperties ? TEXT("true") : TEXT("false")));
	AppendLine(OutText, 0, TEXT(""));

	AppendFunctionOutputs(FunctionForExport, OutText, 0);
	AppendLine(OutText, 0, TEXT(""));

	AppendLine(OutText, 0, FString::Printf(TEXT("function_node_count: %d"), Expressions.Num()));
	AppendLine(OutText, 0, TEXT("function_nodes:"));

	FExportContext Context;
	Context.Options = Options;
	CacheApproximateLayoutHints(Expressions, FunctionForExport, Context);
	for (UMaterialExpression* Expression : Expressions)
	{
		AppendExpressionBlock(Expression, OutText, 1, Context);
	}
	AppendFunctionDefinitions(OutText, 0, Context);

	OutError.Reset();
	return true;
}

bool FUE2CodeMaterialExporter::ExportMaterialPropertyToText(UMaterialInterface* Material, EMaterialProperty MaterialProperty, const FString& OutputFilePath, const FUE2CodeExportOptions& Options, FString& OutError)
{
	FString Text;
	if (!ExportMaterialPropertyToString(Material, MaterialProperty, Options, Text, OutError))
	{
		return false;
	}

	return UE2CodeMaterialExporterPrivate::SaveTextToFile(OutputFilePath, Text, OutError);
}

bool FUE2CodeMaterialExporter::ExportMaterialPropertyToString(UMaterialInterface* Material, EMaterialProperty MaterialProperty, const FUE2CodeExportOptions& Options, FString& OutText, FString& OutError)
{
	using namespace UE2CodeMaterialExporterPrivate;

	UMaterial* BaseMaterial = GetBaseMaterial(Material);
	if (!BaseMaterial)
	{
		OutError = TEXT("Material is null or does not resolve to a base UMaterial.");
		return false;
	}

	FExpressionInput* RootInput = BaseMaterial->GetExpressionInputForProperty(MaterialProperty);
	if (!RootInput || !RootInput->Expression)
	{
		OutError = FString::Printf(TEXT("Material property '%s' is not connected."), *MaterialPropertyToString(MaterialProperty));
		return false;
	}

	TArray<UMaterialExpression*> Expressions;
	TSet<UMaterialExpression*> SeenExpressions;
	CollectUpstreamExpressions(RootInput->Expression, Expressions, SeenExpressions);
	SortExpressions(Expressions);
	RemovePassthroughReroutes(Expressions);

	OutText.Reset();
	AppendLine(OutText, 0, TEXT("UE_NODE2CODE material_property_export version=2"));
	AppendAliasTable(OutText);
	AppendEngineVersion(OutText);
	AppendLine(OutText, 0, FString::Printf(TEXT("material_interface: %s"), *ObjectRef(Material)));
	AppendLine(OutText, 0, FString::Printf(TEXT("base_material: %s"), *ObjectRef(BaseMaterial)));
	AppendLine(OutText, 0, FString::Printf(TEXT("root_property: %s"), *MaterialPropertyToString(MaterialProperty)));
	AppendLine(OutText, 0, FString::Printf(TEXT("root_connection: %s"), *DescribeInput(*RootInput)));
	AppendLine(OutText, 0, FString::Printf(TEXT("options: expand_material_functions=%s function_definitions=deduplicated node_hierarchy_depth=%d max_function_depth=%d include_debug_metadata=%s include_default_like_properties=%s"), Options.bExpandMaterialFunctions ? TEXT("true") : TEXT("false"), Options.NodeHierarchyDepth, Options.MaxFunctionDepth, Options.bIncludeDebugMetadata ? TEXT("true") : TEXT("false"), Options.bIncludeDefaultLikeProperties ? TEXT("true") : TEXT("false")));
	AppendLine(OutText, 0, FString::Printf(TEXT("material_node_count: %d"), Expressions.Num()));
	AppendLine(OutText, 0, TEXT("material_nodes:"));

	FExportContext Context;
	Context.Options = Options;
	CacheApproximateLayoutHints(Expressions, nullptr, Context);
	for (UMaterialExpression* Expression : Expressions)
	{
		AppendExpressionBlock(Expression, OutText, 1, Context);
	}
	AppendFunctionDefinitions(OutText, 0, Context);

	OutError.Reset();
	return true;
}

bool FUE2CodeMaterialExporter::ExportMaterialNodeByNameToText(UMaterialInterface* Material, const FString& ExpressionObjectName, const FString& OutputFilePath, const FUE2CodeExportOptions& Options, FString& OutError)
{
	FString Text;
	if (!ExportMaterialNodeByNameToString(Material, ExpressionObjectName, Options, Text, OutError))
	{
		return false;
	}

	return UE2CodeMaterialExporterPrivate::SaveTextToFile(OutputFilePath, Text, OutError);
}

bool FUE2CodeMaterialExporter::ExportMaterialNodeByNameToString(UMaterialInterface* Material, const FString& ExpressionObjectName, const FUE2CodeExportOptions& Options, FString& OutText, FString& OutError)
{
	using namespace UE2CodeMaterialExporterPrivate;

	UMaterial* BaseMaterial = GetBaseMaterial(Material);
	if (!BaseMaterial)
	{
		OutError = TEXT("Material is null or does not resolve to a base UMaterial.");
		return false;
	}

	TArray<UMaterialExpression*> SearchExpressions;
	TSet<UMaterialExpression*> SeenExpressions;

	for (int32 PropertyIndex = 0; PropertyIndex < MP_MAX; ++PropertyIndex)
	{
		FExpressionInput* Input = BaseMaterial->GetExpressionInputForProperty(static_cast<EMaterialProperty>(PropertyIndex));
		if (Input && Input->Expression)
		{
			CollectUpstreamExpressions(Input->Expression, SearchExpressions, SeenExpressions);
		}
	}
	TArray<UMaterialExpression*> MaterialExpressions;
	UE2CodeEngineCompat::GetMaterialExpressions(BaseMaterial, MaterialExpressions);
	for (UMaterialExpression* Expression : MaterialExpressions)
	{
		AddUniqueExpression(Expression, SearchExpressions, SeenExpressions);
	}

	UMaterialExpression* RootExpression = nullptr;
	const FString Target = ExpressionObjectName.TrimStartAndEnd();
	for (UMaterialExpression* Expression : SearchExpressions)
	{
		if (!Expression)
		{
			continue;
		}

		const FString GuidString = GuidToString(Expression->GetMaterialExpressionId());
		if (Expression->GetName().Equals(Target, ESearchCase::IgnoreCase)
			|| Expression->GetPathName().Equals(Target, ESearchCase::IgnoreCase)
			|| GuidString.Equals(Target, ESearchCase::IgnoreCase))
		{
			RootExpression = Expression;
			break;
		}
	}

	if (!RootExpression)
	{
		OutError = FString::Printf(TEXT("Could not find material expression '%s' in %s."), *ExpressionObjectName, *ObjectRef(BaseMaterial));
		return false;
	}

	FString InnerError;
	if (!ExportMaterialExpressionToString(RootExpression, Options, OutText, InnerError))
	{
		OutError = InnerError;
		return false;
	}

	OutText = FString::Printf(TEXT("source_material_interface: %s%s"), *ObjectRef(Material), LINE_TERMINATOR) + OutText;
	OutError.Reset();
	return true;
}

bool FUE2CodeMaterialExporter::ExportMaterialExpressionToString(UMaterialExpression* RootExpression, const FUE2CodeExportOptions& Options, FString& OutText, FString& OutError)
{
	using namespace UE2CodeMaterialExporterPrivate;

	if (!RootExpression)
	{
		OutError = TEXT("RootExpression is null.");
		return false;
	}

	TArray<UMaterialExpression*> Expressions;
	TSet<UMaterialExpression*> SeenExpressions;
	CollectUpstreamExpressions(RootExpression, Expressions, SeenExpressions);
	SortExpressions(Expressions);
	RemovePassthroughReroutes(Expressions);

	OutText.Reset();
	AppendLine(OutText, 0, TEXT("UE_NODE2CODE material_node_export version=2"));
	AppendAliasTable(OutText);
	AppendEngineVersion(OutText);
	AppendLine(OutText, 0, FString::Printf(TEXT("root_node: %s"), *ExpressionRef(RootExpression)));
	if (Options.bIncludeDebugMetadata)
	{
		AppendLine(OutText, 0, FString::Printf(TEXT("root_path: %s"), *RootExpression->GetPathName()));
	}
	AppendLine(OutText, 0, FString::Printf(TEXT("options: expand_material_functions=%s function_definitions=deduplicated node_hierarchy_depth=%d max_function_depth=%d include_debug_metadata=%s include_default_like_properties=%s"), Options.bExpandMaterialFunctions ? TEXT("true") : TEXT("false"), Options.NodeHierarchyDepth, Options.MaxFunctionDepth, Options.bIncludeDebugMetadata ? TEXT("true") : TEXT("false"), Options.bIncludeDefaultLikeProperties ? TEXT("true") : TEXT("false")));
	AppendLine(OutText, 0, FString::Printf(TEXT("material_node_count: %d"), Expressions.Num()));
	AppendLine(OutText, 0, TEXT("material_nodes:"));

	FExportContext Context;
	Context.Options = Options;
	CacheApproximateLayoutHints(Expressions, nullptr, Context);
	for (UMaterialExpression* Expression : Expressions)
	{
		AppendExpressionBlock(Expression, OutText, 1, Context);
	}
	AppendFunctionDefinitions(OutText, 0, Context);

	OutError.Reset();
	return true;
}

bool FUE2CodeMaterialExporter::ParseMaterialProperty(const FString& PropertyName, EMaterialProperty& OutProperty)
{
	const FString Wanted = UE2CodeMaterialExporterPrivate::NormalizePropertyToken(PropertyName);
	if (Wanted.IsEmpty())
	{
		return false;
	}

	for (int32 PropertyIndex = 0; PropertyIndex < MP_MAX; ++PropertyIndex)
	{
		const EMaterialProperty Candidate = static_cast<EMaterialProperty>(PropertyIndex);
		if (UE2CodeMaterialExporterPrivate::NormalizePropertyToken(MaterialPropertyToString(Candidate)) == Wanted)
		{
			OutProperty = Candidate;
			return true;
		}
	}

	return false;
}

FString FUE2CodeMaterialExporter::MaterialPropertyToString(EMaterialProperty Property)
{
	if (const UEnum* Enum = StaticEnum<EMaterialProperty>())
	{
		return Enum->GetNameStringByValue(static_cast<int64>(Property));
	}
	return FString::Printf(TEXT("EMaterialProperty_%d"), static_cast<int32>(Property));
}

FString FUE2CodeMaterialExporter::NormalizeMaterialAssetPath(const FString& InputPath)
{
	FString Normalized = InputPath.TrimStartAndEnd();
	FPaths::NormalizeFilename(Normalized);

	if (Normalized.EndsWith(TEXT(".uasset"), ESearchCase::IgnoreCase))
	{
		FString FullPath = FPaths::ConvertRelativePathToFull(Normalized);
		FPaths::NormalizeFilename(FullPath);

		FString ContentDir = FPaths::ConvertRelativePathToFull(FPaths::ProjectContentDir());
		FPaths::NormalizeFilename(ContentDir);
		if (!ContentDir.EndsWith(TEXT("/")))
		{
			ContentDir += TEXT("/");
		}

		if (FullPath.StartsWith(ContentDir, ESearchCase::IgnoreCase))
		{
			FString RelativePath = FullPath.RightChop(ContentDir.Len());
			RelativePath = RelativePath.LeftChop(7);
			return TEXT("/Game/") + RelativePath;
		}
	}

	return Normalized;
}
