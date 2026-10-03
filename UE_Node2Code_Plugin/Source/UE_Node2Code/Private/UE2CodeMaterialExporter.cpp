#include "UE2CodeMaterialExporter.h"

#include "UE2CodeEngineCompat.h"
#include "UE2CodeSelection.h"
#include "UE2CodeTextFormat.h"

#include "HAL/FileManager.h"
#include "Engine/Font.h"
#include "MaterialShared.h"
#include "Materials/Material.h"
#include "Materials/MaterialExpression.h"
#include "Materials/MaterialExpressionCustom.h"
#include "Materials/MaterialExpressionFunctionInput.h"
#include "Materials/MaterialExpressionFunctionOutput.h"
#include "Materials/MaterialExpressionMaterialFunctionCall.h"
#include "Materials/MaterialExpressionReroute.h"
#include "Materials/MaterialFunctionInstance.h"
#include "Materials/MaterialFunctionInterface.h"
#include "Materials/MaterialInterface.h"
#include "Misc/DateTime.h"
#include "Misc/EngineVersion.h"
#include "Misc/FileHelper.h"
#include "Misc/PackageName.h"
#include "Misc/Paths.h"
#include "UObject/Package.h"
#include "UObject/UnrealType.h"
#include "VT/RuntimeVirtualTexture.h"

#if ENGINE_MAJOR_VERSION >= 5
#include "Materials/MaterialExpressionNamedReroute.h"
#include "Materials/MaterialExpressionRerouteBase.h"
#endif

#if ENGINE_MAJOR_VERSION > 5 || (ENGINE_MAJOR_VERSION == 5 && ENGINE_MINOR_VERSION >= 3)
#include "SparseVolumeTexture/SparseVolumeTexture.h"
#endif

#if WITH_DEV_AUTOMATION_TESTS
#include "Materials/MaterialExpressionConstant.h"
#include "Materials/MaterialExpressionMultiply.h"
#include "Materials/MaterialExpressionScalarParameter.h"
#include "Misc/AutomationTest.h"
#endif

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
		Value.TrimStartAndEndInline();
		return UE2CodeTextFormat::Escape(MoveTemp(Value));
	}

	static FString Quote(const FString& Value)
	{
		return UE2CodeTextFormat::Quote(Value);
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

	static FString MaskChannelsToString(int32 R, int32 G, int32 B, int32 A)
	{
		FString Channels;
		if (R) { Channels += TEXT("R"); }
		if (G) { Channels += TEXT("G"); }
		if (B) { Channels += TEXT("B"); }
		if (A) { Channels += TEXT("A"); }
		return Channels.IsEmpty() ? TEXT("-") : Channels;
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
	#if ENGINE_MAJOR_VERSION >= 5
		return Expression && Expression->IsA<UMaterialExpressionRerouteBase>();
	#else
		return Expression && Expression->IsA<UMaterialExpressionReroute>();
	#endif
	}

	static bool GetReroutePassthroughInput(const UMaterialExpression* Expression, FExpressionInput& OutInput)
	{
		OutInput = FExpressionInput();
#if WITH_EDITOR
		if (!IsRerouteExpression(Expression))
		{
			return false;
		}

	#if ENGINE_MAJOR_VERSION >= 5
		if (const UMaterialExpressionRerouteBase* Reroute = Cast<UMaterialExpressionRerouteBase>(Expression))
		{
			OutInput = Reroute->TraceInputsToRealInput();
			return OutInput.Expression != nullptr;
		}
	#else
		if (const UMaterialExpressionReroute* Reroute = Cast<UMaterialExpressionReroute>(Expression))
		{
			OutInput = Reroute->TraceInputsToRealInput();
			return OutInput.Expression != nullptr;
		}
	#endif
#endif
		return false;
	}

	static bool ResolveInputSource(const FExpressionInput& Input, const UMaterialExpression*& OutExpression, int32& OutOutputIndex)
	{
		OutExpression = Input.Expression;
		OutOutputIndex = Input.OutputIndex;

		TSet<const UMaterialExpression*> SeenReroutes;
		while (IsRerouteExpression(OutExpression))
		{
			if (SeenReroutes.Contains(OutExpression))
			{
				OutExpression = nullptr;
				return false;
			}

			SeenReroutes.Add(OutExpression);
			FExpressionInput PassthroughInput;
			if (!GetReroutePassthroughInput(OutExpression, PassthroughInput))
			{
				OutExpression = nullptr;
				return false;
			}

			OutExpression = PassthroughInput.Expression;
			OutOutputIndex = PassthroughInput.OutputIndex;
		}

		return OutExpression != nullptr;
	}

	static FString CompactConnectionFields(const FExpressionInput& Input)
	{
		const UMaterialExpression* SourceExpression = nullptr;
		int32 SourceOutputIndex = 0;
		if (!ResolveInputSource(Input, SourceExpression, SourceOutputIndex))
		{
			return TEXT("<- none");
		}

		FString Result = FString::Printf(TEXT("<- %s[%d]"), *ExpressionRef(SourceExpression), SourceOutputIndex);
		if (HasMaskData(Input))
		{
			Result += TEXT(" channels=") + MaskChannelsToString(Input.MaskR, Input.MaskG, Input.MaskB, Input.MaskA);
		}
		return Result;
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

		FString Result = FString::Printf(TEXT("<- %s[%d]"), *ExpressionRef(SourceExpression), SourceOutputIndex);
		if (HasMaskData(Input))
		{
			Result += TEXT(" channels=") + MaskChannelsToString(Input.MaskR, Input.MaskG, Input.MaskB, Input.MaskA);
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

	enum class EInputFallbackState : uint8
	{
		NotFallback,
		Active,
		Inactive
	};

	static EInputFallbackState GetInputFallbackState(UMaterialExpression* Expression, const FProperty* Property)
	{
		if (!Expression || !Property)
		{
			return EInputFallbackState::NotFallback;
		}

		const FString InputPropertyName = Property->GetMetaData(TEXT("OverridingInputProperty"));
		if (InputPropertyName.IsEmpty())
		{
			return EInputFallbackState::NotFallback;
		}

		const FStructProperty* InputProperty = FindFProperty<FStructProperty>(Expression->GetClass(), FName(*InputPropertyName));
		if (!InputProperty || !IsConnectionStructProperty(InputProperty))
		{
			return EInputFallbackState::NotFallback;
		}

		const FExpressionInput* Input = InputProperty->ContainerPtrToValuePtr<FExpressionInput>(Expression);
		if (!Input || Input->Expression)
		{
			return EInputFallbackState::Inactive;
		}

		if (Property->GetFName() == FName(TEXT("PreviewValue")))
		{
			if (const UMaterialExpressionFunctionInput* FunctionInput = Cast<UMaterialExpressionFunctionInput>(Expression))
			{
				return FunctionInput->bUsePreviewValueAsDefault
					? EInputFallbackState::Active
					: EInputFallbackState::Inactive;
			}
		}

		return EInputFallbackState::Active;
	}

	static bool IsSemanticallyRequiredValueProperty(const UMaterialExpression* Expression, const FProperty* Property)
	{
		if (!Expression || !Property)
		{
			return false;
		}

		const FString PropertyName = Property->GetName();
		if (PropertyName.StartsWith(TEXT("Default")))
		{
			return true;
		}

		const FString ClassName = Expression->GetClass()->GetName();
		if (ClassName == TEXT("MaterialExpressionStaticBool") && PropertyName == TEXT("Value"))
		{
			return true;
		}

		const bool bLiteralConstant = ClassName == TEXT("MaterialExpressionConstant")
			|| ClassName == TEXT("MaterialExpressionConstant2Vector")
			|| ClassName == TEXT("MaterialExpressionConstant3Vector")
			|| ClassName == TEXT("MaterialExpressionConstant4Vector");
		return bLiteralConstant
			&& (PropertyName == TEXT("R")
				|| PropertyName == TEXT("G")
				|| PropertyName == TEXT("B")
				|| PropertyName == TEXT("A"));
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

		if (const FStructProperty* StructProperty = CastField<FStructProperty>(Property))
		{
			if (StructProperty->Struct && StructProperty->Struct->GetFName() == FName(TEXT("Guid")))
			{
				return false;
			}
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
		if (const FNumericProperty* NumericProperty = CastField<FNumericProperty>(Property))
		{
			if (NumericProperty->IsFloatingPoint())
			{
				OutValue = FString::SanitizeFloat(NumericProperty->GetFloatingPointPropertyValue(ValuePtr));
				return true;
			}
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

	static FString ParameterFields(const FMaterialParameterInfo& ParameterInfo)
	{
		FString Result = TEXT("name=") + Quote(ParameterInfo.Name.ToString());
		if (ParameterInfo.Association == EMaterialParameterAssociation::LayerParameter)
		{
			Result += FString::Printf(TEXT(" scope=layer index=%d"), ParameterInfo.Index);
		}
		else if (ParameterInfo.Association == EMaterialParameterAssociation::BlendParameter)
		{
			Result += FString::Printf(TEXT(" scope=blend index=%d"), ParameterInfo.Index);
		}
		return Result;
	}

	static FString CompactLinearColor(const FLinearColor& Value)
	{
		return FString::Printf(
			TEXT("(%s,%s,%s,%s)"),
			*FString::SanitizeFloat(Value.R),
			*FString::SanitizeFloat(Value.G),
			*FString::SanitizeFloat(Value.B),
			*FString::SanitizeFloat(Value.A)
		);
	}

	static FString CompactShadingModels(const FMaterialShadingModelField& ShadingModels)
	{
		const UEnum* ShadingModelEnum = StaticEnum<EMaterialShadingModel>();
		FString Result;
		for (int32 Index = 0; Index < MSM_NUM; ++Index)
		{
			const EMaterialShadingModel ShadingModel = static_cast<EMaterialShadingModel>(Index);
			if (!ShadingModels.HasShadingModel(ShadingModel))
			{
				continue;
			}

			if (!Result.IsEmpty())
			{
				Result += TEXT("|");
			}
			Result += ShadingModelEnum
				? ShadingModelEnum->GetNameStringByValue(Index)
				: FString::FromInt(Index);
		}
		return Result.IsEmpty() ? TEXT("None") : Result;
	}

	static void AppendMaterialSettings(UMaterialInterface* Material, UMaterial* BaseMaterial, FString& OutText)
	{
		if (!Material || !BaseMaterial)
		{
			return;
		}

		const EBlendMode BlendMode = Material->GetBlendMode();
		const UEnum* BlendModeEnum = StaticEnum<EBlendMode>();
		FString Line = FString::Printf(
			TEXT("material_settings: domain=%s blend=%s shading=%s two_sided=%s"),
			*MaterialDomainString(BaseMaterial->MaterialDomain),
			BlendModeEnum ? *BlendModeEnum->GetNameStringByValue(static_cast<int64>(BlendMode)) : TEXT("Unknown"),
			*CompactShadingModels(Material->GetShadingModels()),
			Material->IsTwoSided() ? TEXT("true") : TEXT("false")
		);
		if (BlendMode == BLEND_Masked)
		{
			Line += TEXT(" opacity_mask_clip=") + FString::SanitizeFloat(Material->GetOpacityMaskClipValue());
		}
		if (Material->IsDitheredLODTransition())
		{
			Line += TEXT(" dithered_lod_transition=true");
		}
		AppendLine(OutText, 0, Line);
	}

	static void AddOverrideLine(
		TMap<FString, FString>& LinesByKey,
		const TCHAR* Type,
		const FMaterialParameterInfo& ParameterInfo,
		const FString& Value)
	{
		const FString Key = FString(Type)
			+ TEXT("|") + FString::FromInt(static_cast<int32>(ParameterInfo.Association.GetValue()))
			+ TEXT("|") + FString::FromInt(ParameterInfo.Index)
			+ TEXT("|") + ParameterInfo.Name.ToString();
		LinesByKey.Add(
			Key,
			FString::Printf(TEXT("- %s %s value=%s"), Type, *ParameterFields(ParameterInfo), *Value)
		);
	}

	static void AppendOverrideLines(const TMap<FString, FString>& LinesByKey, FString& OutText, int32 Depth)
	{
		if (LinesByKey.Num() == 0)
		{
			return;
		}

		TArray<FString> Keys;
		LinesByKey.GetKeys(Keys);
		Keys.Sort();
		AppendLine(OutText, Depth, TEXT("effective_parameter_overrides:"));
		for (const FString& Key : Keys)
		{
			AppendLine(OutText, Depth + 1, LinesByKey.FindChecked(Key));
		}
	}

	static void AppendMaterialInstanceOverrides(UMaterialInterface* Material, UMaterial* BaseMaterial, FString& OutText)
	{
		if (!Material || !BaseMaterial || Material == BaseMaterial)
		{
			return;
		}

		TMap<FString, FString> LinesByKey;
		TArray<FMaterialParameterInfo> ParameterInfos;
		TArray<FGuid> ParameterIds;

		Material->GetAllScalarParameterInfo(ParameterInfos, ParameterIds);
		for (const FMaterialParameterInfo& ParameterInfo : ParameterInfos)
		{
			float Value = 0.0f;
			if (Material->GetScalarParameterValue(FHashedMaterialParameterInfo(ParameterInfo), Value, true))
			{
				AddOverrideLine(LinesByKey, TEXT("scalar"), ParameterInfo, FString::SanitizeFloat(Value));
			}
		}

		ParameterInfos.Reset();
		ParameterIds.Reset();
		Material->GetAllVectorParameterInfo(ParameterInfos, ParameterIds);
		for (const FMaterialParameterInfo& ParameterInfo : ParameterInfos)
		{
			FLinearColor Value = FLinearColor::Black;
			if (Material->GetVectorParameterValue(FHashedMaterialParameterInfo(ParameterInfo), Value, true))
			{
				AddOverrideLine(LinesByKey, TEXT("vector"), ParameterInfo, CompactLinearColor(Value));
			}
		}

		ParameterInfos.Reset();
		ParameterIds.Reset();
		Material->GetAllTextureParameterInfo(ParameterInfos, ParameterIds);
		for (const FMaterialParameterInfo& ParameterInfo : ParameterInfos)
		{
			UTexture* Value = nullptr;
			if (Material->GetTextureParameterValue(FHashedMaterialParameterInfo(ParameterInfo), Value, true))
			{
				AddOverrideLine(LinesByKey, TEXT("texture"), ParameterInfo, ShortObjectName(Value));
			}
		}

		ParameterInfos.Reset();
		ParameterIds.Reset();
		Material->GetAllRuntimeVirtualTextureParameterInfo(ParameterInfos, ParameterIds);
		for (const FMaterialParameterInfo& ParameterInfo : ParameterInfos)
		{
			URuntimeVirtualTexture* Value = nullptr;
			if (Material->GetRuntimeVirtualTextureParameterValue(FHashedMaterialParameterInfo(ParameterInfo), Value, true))
			{
				AddOverrideLine(LinesByKey, TEXT("runtime_virtual_texture"), ParameterInfo, ShortObjectName(Value));
			}
		}

	#if ENGINE_MAJOR_VERSION > 5 || (ENGINE_MAJOR_VERSION == 5 && ENGINE_MINOR_VERSION >= 3)
		ParameterInfos.Reset();
		ParameterIds.Reset();
		Material->GetAllSparseVolumeTextureParameterInfo(ParameterInfos, ParameterIds);
		for (const FMaterialParameterInfo& ParameterInfo : ParameterInfos)
		{
			USparseVolumeTexture* Value = nullptr;
			if (Material->GetSparseVolumeTextureParameterValue(FHashedMaterialParameterInfo(ParameterInfo), Value, true))
			{
				AddOverrideLine(LinesByKey, TEXT("sparse_volume_texture"), ParameterInfo, ShortObjectName(Value));
			}
		}
	#endif

		ParameterInfos.Reset();
		ParameterIds.Reset();
		Material->GetAllFontParameterInfo(ParameterInfos, ParameterIds);
		for (const FMaterialParameterInfo& ParameterInfo : ParameterInfos)
		{
			UFont* Font = nullptr;
			int32 Page = 0;
			if (Material->GetFontParameterValue(FHashedMaterialParameterInfo(ParameterInfo), Font, Page, true))
			{
				AddOverrideLine(
					LinesByKey,
					TEXT("font"),
					ParameterInfo,
					FString::Printf(TEXT("%s page=%d"), *ShortObjectName(Font), Page)
				);
			}
		}

	#if WITH_EDITORONLY_DATA
		ParameterInfos.Reset();
		ParameterIds.Reset();
		Material->GetAllStaticSwitchParameterInfo(ParameterInfos, ParameterIds);
		for (const FMaterialParameterInfo& ParameterInfo : ParameterInfos)
		{
			bool bValue = false;
			FGuid ExpressionGuid;
			if (Material->GetStaticSwitchParameterValue(FHashedMaterialParameterInfo(ParameterInfo), bValue, ExpressionGuid, true))
			{
				AddOverrideLine(LinesByKey, TEXT("static_switch"), ParameterInfo, bValue ? TEXT("true") : TEXT("false"));
			}
		}
	#endif

		AppendOverrideLines(LinesByKey, OutText, 0);
	}

	static void AppendMaterialFunctionInstanceOverrides(
		UMaterialFunctionInterface* MaterialFunction,
		FString& OutText,
		int32 Depth,
		bool bAppendTrailingBlank)
	{
		const UMaterialFunctionInstance* FunctionInstance = Cast<UMaterialFunctionInstance>(MaterialFunction);
		if (!FunctionInstance)
		{
			return;
		}

		TArray<const UMaterialFunctionInstance*> InstanceChain;
		for (const UMaterialFunctionInstance* Current = FunctionInstance; Current; Current = Cast<UMaterialFunctionInstance>(Current->Parent))
		{
			InstanceChain.Add(Current);
		}

		TMap<FString, FString> LinesByKey;
		for (int32 ChainIndex = InstanceChain.Num() - 1; ChainIndex >= 0; --ChainIndex)
		{
			const UMaterialFunctionInstance* Current = InstanceChain[ChainIndex];
			for (const FScalarParameterValue& Parameter : Current->ScalarParameterValues)
			{
				AddOverrideLine(LinesByKey, TEXT("scalar"), Parameter.ParameterInfo, FString::SanitizeFloat(Parameter.ParameterValue));
			}
			for (const FVectorParameterValue& Parameter : Current->VectorParameterValues)
			{
				AddOverrideLine(LinesByKey, TEXT("vector"), Parameter.ParameterInfo, CompactLinearColor(Parameter.ParameterValue));
			}
			for (const FTextureParameterValue& Parameter : Current->TextureParameterValues)
			{
				UTexture* Value = Parameter.ParameterValue;
				AddOverrideLine(LinesByKey, TEXT("texture"), Parameter.ParameterInfo, ShortObjectName(Value));
			}
			for (const FRuntimeVirtualTextureParameterValue& Parameter : Current->RuntimeVirtualTextureParameterValues)
			{
				URuntimeVirtualTexture* Value = Parameter.ParameterValue;
				AddOverrideLine(LinesByKey, TEXT("runtime_virtual_texture"), Parameter.ParameterInfo, ShortObjectName(Value));
			}
		#if ENGINE_MAJOR_VERSION > 5 || (ENGINE_MAJOR_VERSION == 5 && ENGINE_MINOR_VERSION >= 3)
			for (const FSparseVolumeTextureParameterValue& Parameter : Current->SparseVolumeTextureParameterValues)
			{
				USparseVolumeTexture* Value = Parameter.ParameterValue;
				AddOverrideLine(LinesByKey, TEXT("sparse_volume_texture"), Parameter.ParameterInfo, ShortObjectName(Value));
			}
		#endif
			for (const FFontParameterValue& Parameter : Current->FontParameterValues)
			{
				UFont* Font = Parameter.FontValue;
				AddOverrideLine(
					LinesByKey,
					TEXT("font"),
					Parameter.ParameterInfo,
					FString::Printf(TEXT("%s page=%d"), *ShortObjectName(Font), Parameter.FontPage)
				);
			}
			for (const FStaticSwitchParameter& Parameter : Current->StaticSwitchParameterValues)
			{
				if (Parameter.bOverride)
				{
					AddOverrideLine(LinesByKey, TEXT("static_switch"), Parameter.ParameterInfo, Parameter.Value ? TEXT("true") : TEXT("false"));
				}
			}
		}

		AppendOverrideLines(LinesByKey, OutText, Depth);
		if (bAppendTrailingBlank && LinesByKey.Num() > 0)
		{
			AppendLine(OutText, Depth, TEXT(""));
		}
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
			const FString AType = A.GetClass()->GetName();
			const FString BType = B.GetClass()->GetName();
			if (AType == BType)
			{
				return A.GetName() < B.GetName();
			}
			return AType < BType;
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
		if (Expression && IsRerouteExpression(Expression))
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
			FExpressionInput PassthroughInput;
			if (GetReroutePassthroughInput(Expression, PassthroughInput))
			{
				CollectUpstreamExpressions(PassthroughInput.Expression, Expressions, Seen);
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
			return Expression && IsRerouteExpression(Expression);
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
		// Set only for selected-node exports; applies to the root graph, not function definitions.
		const TSet<const UMaterialExpression*>* SelectedExpressions = nullptr;
		int32 NextFunctionId = 1;
	};

	static bool IsSelectedSource(const FExpressionInput& Input, const TSet<const UMaterialExpression*>& Selected)
	{
		const UMaterialExpression* SourceExpression = nullptr;
		int32 SourceOutputIndex = 0;
		return ResolveInputSource(Input, SourceExpression, SourceOutputIndex) && Selected.Contains(SourceExpression);
	}

	static FString ConnectionFields(const FExpressionInput& Input, const FExportContext& Context)
	{
		FString Result = CompactConnectionFields(Input);
		if (Context.SelectedExpressions && Context.FunctionStack.Num() == 0 && Input.Expression && !IsSelectedSource(Input, *Context.SelectedExpressions))
		{
			Result += TEXT(" unselected");
		}
		return Result;
	}

	static TSet<const UMaterialExpression*> ExpressionSet(const TArray<UMaterialExpression*>& Expressions)
	{
		TSet<const UMaterialExpression*> Result;
		for (const UMaterialExpression* Expression : Expressions) { Result.Add(Expression); }
		return Result;
	}

	static void SelectExpressions(const TArray<UMaterialExpression*>& Candidates, const UE2CodeSelection::FNodeSelection& Selection, TArray<UMaterialExpression*>& OutExpressions)
	{
		TSet<UMaterialExpression*> Seen;
		for (UMaterialExpression* Expression : Candidates)
		{
			if (Expression && Selection.Contains(Expression, Expression->GetMaterialExpressionId()))
			{
				AddUniqueExpression(Expression, OutExpressions, Seen);
			}
		}
	}

	static void AppendExpressionBlock(UMaterialExpression* Expression, FString& OutText, int32 Depth, FExportContext& Context);
	static void AppendFunctionDefinitions(FString& OutText, int32 Depth, FExportContext& Context);

	static void CacheApproximateLayoutHints(const TArray<UMaterialExpression*>& Expressions, const UMaterialFunctionInterface* OwningFunction, FExportContext& Context)
	{
		if (!Context.Options.bIncludeDebugMetadata || !ShouldRecordApproximateLayout(OwningFunction))
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

		if (Context.Options.NodeHierarchyDepth == 0
			&& Context.FunctionStack.Num() >= FMath::Max(1, Context.Options.MaxFunctionDepth))
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
		SortExpressions(FunctionExpressions);

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
		AppendMaterialFunctionInstanceOverrides(FunctionCall->MaterialFunction, OutText, Depth + 1, false);

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
			FString BindingLine = FString::Printf(TEXT("- [%d] %s %s"), InputIndex, *MaybeQuotedName(InputName), *ConnectionFields(FunctionInput.Input, Context));
			if (Context.Options.bIncludeDebugMetadata)
			{
				BindingLine += FString::Printf(TEXT(" input_id=%s"), *GuidToString(FunctionInput.ExpressionInputId));
			}
			AppendLine(CallInputText, Depth + 2, BindingLine);
			++ConnectedCallInputCount;
		}
		if (ConnectedCallInputCount > 0)
		{
			AppendLine(OutText, Depth + 1, TEXT("call_inputs:"));
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
		AppendLine(OutText, Depth, FString::Printf(TEXT("function %s asset=\"%s\""), *FunctionId, *ObjectRef(Function)));

		const FString Description = UE2CodeEngineCompat::GetFunctionDescription(Function);
		if (!Description.IsEmpty())
		{
			AppendLine(OutText, Depth + 1, FString::Printf(TEXT("description: \"%s\""), *OneLine(Description)));
		}

		TArray<UMaterialExpression*> InternalExpressions;
		if (!UE2CodeEngineCompat::GetFunctionExpressions(Function, InternalExpressions))
		{
			AppendLine(OutText, Depth + 1, TEXT("internal_nodes: unavailable"));
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

	}

	static void AppendFunctionDefinitions(FString& OutText, int32 Depth, FExportContext& Context)
	{
		if (!Context.Options.bExpandMaterialFunctions)
		{
			return;
		}

		AppendLine(OutText, Depth, TEXT(""));
		if (Context.FunctionOrder.Num() == 0)
		{
			AppendLine(OutText, Depth, TEXT("function_definitions: none"));
			return;
		}

		AppendLine(OutText, Depth, FString::Printf(TEXT("function_definitions: count=%d"), Context.FunctionOrder.Num()));
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

			const EInputFallbackState FallbackState = GetInputFallbackState(Expression, Property);
			if (FallbackState == EInputFallbackState::Inactive)
			{
				continue;
			}

			FString ValueText;
			if (!ExportPropertyValue(Property, Expression, ValueText))
			{
				continue;
			}

			if (!Context.Options.bIncludeDefaultLikeProperties
				&& FallbackState != EInputFallbackState::Active
				&& !IsSemanticallyRequiredValueProperty(Expression, Property)
				&& IsDefaultLikePropertyValue(Property, Expression, ValueText))
			{
				continue;
			}

			const UMaterialExpressionCustom* Custom = Cast<UMaterialExpressionCustom>(Expression);
			if (Custom && Property->GetFName() == FName(TEXT("Code")))
			{
				// Read the source directly: unescaping ValueText would also alter literal backslashes.
				FString Code = Custom->Code;
				Code.ReplaceInline(TEXT("\r\n"), TEXT("\n"));
				Code.ReplaceInline(TEXT("\r"), TEXT("\n"));
				TArray<FString> CodeLines;
				Code.ParseIntoArray(CodeLines, TEXT("\n"), false);
				AppendLine(PropertyText, Depth + 1, TEXT("- Code = |"));
				for (const FString& CodeLine : CodeLines)
				{
					AppendLine(PropertyText, Depth + 2, CodeLine);
				}
			}
			else
			{
				AppendLine(
					PropertyText,
					Depth + 1,
					FString::Printf(TEXT("- %s = %s"), *Property->GetName(), *ValueText)
				);
			}
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

		AppendLine(OutText, Depth, TEXT("outputs:"));
		for (int32 OutputIndex = 0; OutputIndex < Outputs.Num(); ++OutputIndex)
		{
			const FExpressionOutput& Output = Outputs[OutputIndex];
			FString Line = FString::Printf(TEXT("- [%d] %s"), OutputIndex, *MaybeQuotedName(Output.OutputName.ToString()));
			if (HasMaskData(Output))
			{
				Line += TEXT(" channels=") + MaskChannelsToString(Output.MaskR, Output.MaskG, Output.MaskB, Output.MaskA);
			}
			AppendLine(
				OutText,
				Depth + 1,
				Line
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
				TEXT("node %s type=%s role=%s"),
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
						FString::Printf(TEXT("- [%d] %s %s"), InputIndex, *MaybeQuotedName(InputName), *ConnectionFields(*Input, Context))
					);
					++ConnectedInputCount;
				}
			}
			if (ConnectedInputCount > 0)
			{
				AppendLine(OutText, Depth + 1, TEXT("inputs:"));
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

	}

	// With a selection, only outputs fed directly by a selected node are listed.
	static void AppendMaterialRoots(UMaterial* Material, FString& OutText, int32 Depth, const TSet<const UMaterialExpression*>* Selected = nullptr)
	{
		AppendLine(OutText, Depth, TEXT("material_outputs:"));
		int32 ConnectedCount = 0;

		for (int32 PropertyIndex = 0; PropertyIndex < MP_MAX; ++PropertyIndex)
		{
			const EMaterialProperty MaterialProperty = static_cast<EMaterialProperty>(PropertyIndex);
			FExpressionInput* Input = Material->GetExpressionInputForProperty(MaterialProperty);
			if (!Input || !Input->Expression || (Selected && !IsSelectedSource(*Input, *Selected)))
			{
				continue;
			}

			AppendLine(
				OutText,
				Depth + 1,
				FString::Printf(
					TEXT("- %s %s"),
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

	// With a selection, only outputs that are selected or fed by a selected node are listed.
	static void AppendFunctionOutputs(const UMaterialFunctionInterface* Function, FString& OutText, int32 Depth, const TSet<const UMaterialExpression*>* Selected = nullptr)
	{
		AppendLine(OutText, Depth, TEXT("function_outputs:"));
		int32 ConnectedCount = 0;

		TArray<UMaterialExpression*> FunctionExpressions;
		if (UE2CodeEngineCompat::GetFunctionExpressions(Function, FunctionExpressions))
		{
			for (UMaterialExpression* Expression : FunctionExpressions)
			{
				UMaterialExpressionFunctionOutput* FunctionOutput = Cast<UMaterialExpressionFunctionOutput>(Expression);
				if (!FunctionOutput || !FunctionOutput->A.Expression
					|| (Selected && !Selected->Contains(FunctionOutput) && !IsSelectedSource(FunctionOutput->A, *Selected)))
				{
					continue;
				}

				AppendLine(
					OutText,
					Depth + 1,
					FString::Printf(TEXT("- %s %s"), *MaybeQuotedName(FunctionOutput->OutputName.ToString()), *CompactConnectionFields(FunctionOutput->A))
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
		AppendLine(OutText, 0, TEXT("type_aliases:"));

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

	static void AppendExportOptions(const FUE2CodeExportOptions& Options, FString& OutText, bool bIncludeUnreferencedOption)
	{
		AppendLine(OutText, 0, TEXT("options:"));
		AppendLine(OutText, 1, FString::Printf(TEXT("hierarchy_depth: %d"), Options.NodeHierarchyDepth));
		AppendLine(OutText, 1, FString::Printf(TEXT("expand_called_graphs: %s"), Options.bExpandMaterialFunctions ? TEXT("true") : TEXT("false")));
		AppendLine(OutText, 1, FString::Printf(TEXT("max_depth_safety_limit: %d"), Options.MaxFunctionDepth));
		if (bIncludeUnreferencedOption)
		{
			AppendLine(OutText, 1, FString::Printf(TEXT("include_unreferenced_nodes: %s"), Options.bExportUnreferencedMaterialExpressions ? TEXT("true") : TEXT("false")));
		}
		AppendLine(OutText, 1, FString::Printf(TEXT("debug: %s"), Options.bIncludeDebugMetadata ? TEXT("true") : TEXT("false")));
		AppendLine(OutText, 1, FString::Printf(TEXT("default_properties: %s"), Options.bIncludeDefaultLikeProperties ? TEXT("true") : TEXT("false")));
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

	const UE2CodeSelection::FNodeSelection Selection(Options);
	TArray<UMaterialExpression*> Expressions;
	TSet<UMaterialExpression*> SeenExpressions;

	if (Selection.IsActive())
	{
		TArray<UMaterialExpression*> MaterialExpressions;
		UE2CodeEngineCompat::GetMaterialExpressions(BaseMaterial, MaterialExpressions);
		SelectExpressions(MaterialExpressions, Selection, Expressions);
		if (Expressions.Num() == 0)
		{
			OutText.Reset();
			OutError = UE2CodeSelection::NoMatchError(ObjectRef(Material));
			return false;
		}
	}
	else
	{
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
	}

	SortExpressions(Expressions);
	RemovePassthroughReroutes(Expressions);
	const TSet<const UMaterialExpression*> SelectedExpressions = ExpressionSet(Expressions);

	OutText.Reset();
	AppendLine(OutText, 0, TEXT("UE_NODE2CODE material_export version=3"));
	AppendAliasTable(OutText);
	if (Options.bIncludeDebugMetadata)
	{
		AppendLine(OutText, 0, FString::Printf(TEXT("exported_at_local: %s"), *FDateTime::Now().ToString(TEXT("%Y-%m-%d %H:%M:%S"))));
	}
	AppendEngineVersion(OutText);
	AppendLine(OutText, 0, FString::Printf(TEXT("material_interface: %s"), *ObjectRef(Material)));
	AppendLine(OutText, 0, FString::Printf(TEXT("base_material: %s"), *ObjectRef(BaseMaterial)));
	AppendExportOptions(Options, OutText, true);
	if (Selection.IsActive())
	{
		AppendLine(OutText, 0, UE2CodeSelection::HeaderLine(Expressions.Num()));
	}
	AppendLine(OutText, 0, TEXT(""));
	AppendMaterialSettings(Material, BaseMaterial, OutText);
	AppendMaterialInstanceOverrides(Material, BaseMaterial, OutText);

	AppendMaterialRoots(BaseMaterial, OutText, 0, Selection.IsActive() ? &SelectedExpressions : nullptr);
	AppendLine(OutText, 0, TEXT(""));

	AppendLine(OutText, 0, FString::Printf(TEXT("material_nodes: count=%d"), Expressions.Num()));

	FExportContext Context;
	Context.Options = Options;
	Context.SelectedExpressions = Selection.IsActive() ? &SelectedExpressions : nullptr;
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

	const UE2CodeSelection::FNodeSelection Selection(Options);
	if (Selection.IsActive())
	{
		const TArray<UMaterialExpression*> FunctionExpressions = Expressions;
		Expressions.Reset();
		SelectExpressions(FunctionExpressions, Selection, Expressions);
		if (Expressions.Num() == 0)
		{
			OutText.Reset();
			OutError = UE2CodeSelection::NoMatchError(ObjectRef(MaterialFunction));
			return false;
		}
	}

	RemovePassthroughReroutes(Expressions);
	SortExpressions(Expressions);
	const TSet<const UMaterialExpression*> SelectedExpressions = ExpressionSet(Expressions);

	OutText.Reset();
	AppendLine(OutText, 0, TEXT("UE_NODE2CODE material_function_export version=3"));
	AppendAliasTable(OutText);
	if (Options.bIncludeDebugMetadata)
	{
		AppendLine(OutText, 0, FString::Printf(TEXT("exported_at_local: %s"), *FDateTime::Now().ToString(TEXT("%Y-%m-%d %H:%M:%S"))));
	}
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
	AppendExportOptions(Options, OutText, false);
	if (Selection.IsActive())
	{
		AppendLine(OutText, 0, UE2CodeSelection::HeaderLine(Expressions.Num()));
	}
	AppendLine(OutText, 0, TEXT(""));
	AppendMaterialFunctionInstanceOverrides(MaterialFunction, OutText, 0, true);

	AppendFunctionOutputs(FunctionForExport, OutText, 0, Selection.IsActive() ? &SelectedExpressions : nullptr);
	AppendLine(OutText, 0, TEXT(""));

	AppendLine(OutText, 0, FString::Printf(TEXT("function_nodes: count=%d"), Expressions.Num()));

	FExportContext Context;
	Context.Options = Options;
	Context.SelectedExpressions = Selection.IsActive() ? &SelectedExpressions : nullptr;
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
	AppendLine(OutText, 0, TEXT("UE_NODE2CODE material_property_export version=3"));
	AppendAliasTable(OutText);
	AppendEngineVersion(OutText);
	AppendLine(OutText, 0, FString::Printf(TEXT("material_interface: %s"), *ObjectRef(Material)));
	AppendLine(OutText, 0, FString::Printf(TEXT("base_material: %s"), *ObjectRef(BaseMaterial)));
	AppendLine(OutText, 0, FString::Printf(TEXT("root_property: %s"), *MaterialPropertyToString(MaterialProperty)));
	AppendLine(OutText, 0, FString::Printf(TEXT("root_connection: %s"), *DescribeInput(*RootInput)));
	AppendExportOptions(Options, OutText, false);
	AppendMaterialSettings(Material, BaseMaterial, OutText);
	AppendMaterialInstanceOverrides(Material, BaseMaterial, OutText);
	AppendLine(OutText, 0, FString::Printf(TEXT("material_nodes: count=%d"), Expressions.Num()));

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

	FString SourcePrefix;
	AppendLine(SourcePrefix, 0, FString::Printf(TEXT("source_material_interface: %s"), *ObjectRef(Material)));
	AppendMaterialSettings(Material, BaseMaterial, SourcePrefix);
	AppendMaterialInstanceOverrides(Material, BaseMaterial, SourcePrefix);
	OutText = SourcePrefix + OutText;
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
	AppendLine(OutText, 0, TEXT("UE_NODE2CODE material_node_export version=3"));
	AppendAliasTable(OutText);
	AppendEngineVersion(OutText);
	AppendLine(OutText, 0, FString::Printf(TEXT("root_node: %s"), *ExpressionRef(RootExpression)));
	if (Options.bIncludeDebugMetadata)
	{
		AppendLine(OutText, 0, FString::Printf(TEXT("root_path: %s"), *RootExpression->GetPathName()));
	}
	AppendExportOptions(Options, OutText, false);
	AppendLine(OutText, 0, FString::Printf(TEXT("material_nodes: count=%d"), Expressions.Num()));

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
	Normalized = FPackageName::ExportTextPathToObjectPath(Normalized);
	FPaths::NormalizeFilename(Normalized);

	if (Normalized.EndsWith(TEXT(".uasset"), ESearchCase::IgnoreCase))
	{
		FString FullPath = FPaths::ConvertRelativePathToFull(Normalized);
		FPaths::NormalizeFilename(FullPath);

		FString LongPackageName;
		if (FPackageName::TryConvertFilenameToLongPackageName(FullPath, LongPackageName))
		{
			return LongPackageName;
		}

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

#if WITH_DEV_AUTOMATION_TESTS
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FUE2CodeMaterialCompactSemanticsTest,
	"UE_Node2Code.Material.CompactSemantics",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FUE2CodeMaterialCompactSemanticsTest::RunTest(const FString& Parameters)
{
	TestEqual(
		TEXT("Quoted text escapes quotes, slashes, tabs, and newlines"),
		UE2CodeTextFormat::Quote(TEXT("a\"b\\c\tline\nnext")),
		FString(TEXT("\"a\\\"b\\\\c\\tline\\nnext\""))
	);

	UMaterialExpressionConstant* Constant = NewObject<UMaterialExpressionConstant>();
	Constant->R = 0.0f;

	UMaterialExpressionReroute* Reroute = NewObject<UMaterialExpressionReroute>();
	Reroute->Input.Expression = Constant;

	UMaterialExpressionMultiply* Multiply = NewObject<UMaterialExpressionMultiply>();
	Multiply->ConstA = 1.0f;
	Multiply->ConstB = 9.0f;
	Multiply->B.Expression = Reroute;

	FUE2CodeExportOptions Options;
	Options.bExportUnreferencedMaterialExpressions = false;
	FString Text;
	FString Error;
	TestTrue(
		TEXT("A compact material chain exports"),
		FUE2CodeMaterialExporter::ExportMaterialExpressionToString(Multiply, Options, Text, Error)
	);
	TestTrue(TEXT("The compact material format uses the v3 header"), Text.Contains(TEXT("UE_NODE2CODE material_node_export version=3")));
	TestTrue(TEXT("Material inputs use readable upstream arrows"), Text.Contains(TEXT(" <- ")));
	TestTrue(TEXT("Material nodes use indentation without redundant end markers"), Text.Contains(TEXT("node ME_")) && !Text.Contains(TEXT("node_end")));
	TestTrue(TEXT("Hierarchy depth is exposed as a common graph option"), Text.Contains(TEXT("hierarchy_depth: 0")));
	TestTrue(TEXT("An active default input fallback is retained"), Text.Contains(TEXT("ConstA =")));
	TestFalse(TEXT("An inactive fallback behind a connected input is omitted"), Text.Contains(TEXT("ConstB =")));
	const bool bHasLiteralZero = Text.Contains(TEXT("- R = 0"));
	if (!bHasLiteralZero)
	{
		AddError(TEXT("Literal-zero export was:") LINE_TERMINATOR + Text);
	}
	TestTrue(TEXT("A literal zero remains explicit"), bHasLiteralZero);
	TestFalse(TEXT("Literal floats omit redundant trailing zeros"), Text.Contains(TEXT("- R = 0.000000")));
	TestFalse(TEXT("A normal reroute is inlined"), Text.Contains(TEXT("MaterialExpressionReroute")));

	FString SecondText;
	Error.Reset();
	TestTrue(
		TEXT("The same compact material chain exports twice"),
		FUE2CodeMaterialExporter::ExportMaterialExpressionToString(Multiply, Options, SecondText, Error)
	);
	TestEqual(TEXT("Default exports are deterministic"), Text, SecondText);

	UMaterialExpressionScalarParameter* ScalarParameter = NewObject<UMaterialExpressionScalarParameter>();
	ScalarParameter->ParameterName = TEXT("ZeroParameter");
	ScalarParameter->DefaultValue = 0.0f;
	Text.Reset();
	Error.Reset();
	TestTrue(
		TEXT("A scalar parameter exports"),
		FUE2CodeMaterialExporter::ExportMaterialExpressionToString(ScalarParameter, Options, Text, Error)
	);
	const bool bHasParameterDefault = Text.Contains(TEXT("DefaultValue ="));
	if (!bHasParameterDefault)
	{
		AddError(TEXT("Zero-parameter export was:") LINE_TERMINATOR + Text);
	}
	TestTrue(TEXT("A parameter default remains explicit even when zero"), bHasParameterDefault);

	UMaterial* Material = NewObject<UMaterial>();
	Text.Reset();
	Error.Reset();
	TestTrue(
		TEXT("A base material exports"),
		FUE2CodeMaterialExporter::ExportMaterialToString(Material, Options, Text, Error)
	);
	TestTrue(TEXT("Rendering-critical material settings are retained"), Text.Contains(TEXT("material_settings: domain=")));
	TestFalse(TEXT("Default output omits volatile timestamps"), Text.Contains(TEXT("exported_at_local:")));

	UMaterial* SelectionMaterial = NewObject<UMaterial>();
	UMaterialExpressionConstant* SelectionConstant = NewObject<UMaterialExpressionConstant>(SelectionMaterial);
	UMaterialExpressionMultiply* SelectionMultiply = NewObject<UMaterialExpressionMultiply>(SelectionMaterial);
	SelectionMultiply->A.Expression = SelectionConstant;
#if ENGINE_MAJOR_VERSION >= 5
	SelectionMaterial->GetExpressionCollection().AddExpression(SelectionConstant);
	SelectionMaterial->GetExpressionCollection().AddExpression(SelectionMultiply);
#else
	SelectionMaterial->Expressions.Add(SelectionConstant);
	SelectionMaterial->Expressions.Add(SelectionMultiply);
#endif
	SelectionMaterial->GetExpressionInputForProperty(MP_BaseColor)->Expression = SelectionMultiply;
	FUE2CodeExportOptions SelectionOptions;
	SelectionOptions.SelectedNodeIds.Add(SelectionMultiply->GetName());
	Text.Reset();
	Error.Reset();
	TestTrue(TEXT("Selected material nodes export"), FUE2CodeMaterialExporter::ExportMaterialToString(SelectionMaterial, SelectionOptions, Text, Error));
	TestTrue(TEXT("Material selection header is written"), Text.Contains(TEXT("selection: nodes=1")) && Text.Contains(TEXT("material_nodes: count=1")));
	TestTrue(TEXT("Inputs from unselected material nodes are marked"), Text.Contains(TEXT(" unselected")));
	TestTrue(TEXT("Outputs fed by the selection are kept"), Text.Contains(TEXT("- MP_BaseColor <- ")));
	SelectionOptions.SelectedNodeIds = {SelectionConstant->GetName()};
	TestTrue(TEXT("Selecting an upstream material node exports"), FUE2CodeMaterialExporter::ExportMaterialToString(SelectionMaterial, SelectionOptions, Text, Error));
	TestFalse(TEXT("Outputs not fed by the selection are omitted"), Text.Contains(TEXT("- MP_BaseColor <- ")));
	SelectionOptions.SelectedNodeIds = {TEXT("NoSuchNode")};
	TestFalse(TEXT("An unmatched material selection fails"), FUE2CodeMaterialExporter::ExportMaterialToString(SelectionMaterial, SelectionOptions, Text, Error));

#if ENGINE_MAJOR_VERSION >= 5
	UMaterialExpressionNamedRerouteDeclaration* Declaration = NewObject<UMaterialExpressionNamedRerouteDeclaration>();
	Declaration->VariableGuid = FGuid::NewGuid();
	Declaration->Input.Expression = Constant;
	UMaterialExpressionNamedRerouteUsage* Usage = NewObject<UMaterialExpressionNamedRerouteUsage>();
	Usage->Declaration = Declaration;
	Usage->DeclarationGuid = Declaration->VariableGuid;
	Multiply->B.Expression = Usage;

	Text.Reset();
	Error.Reset();
	TestTrue(
		TEXT("A chain through a named reroute exports"),
		FUE2CodeMaterialExporter::ExportMaterialExpressionToString(Multiply, Options, Text, Error)
	);
	TestTrue(TEXT("The named reroute resolves to its declaration input"), Text.Contains(TEXT("type=ME_C ")));
	TestFalse(TEXT("The named reroute is inlined"), Text.Contains(TEXT("NamedReroute")));
#endif

	const FString EngineMaterialPackage = TEXT("/Engine/EngineMaterials/DefaultMaterial");
	const FString EngineMaterialFilename = FPackageName::LongPackageNameToFilename(
		EngineMaterialPackage,
		FPackageName::GetAssetPackageExtension()
	);
	TestEqual(
		TEXT("Mounted engine .uasset filenames normalize to package paths"),
		FUE2CodeMaterialExporter::NormalizeMaterialAssetPath(EngineMaterialFilename),
		EngineMaterialPackage
	);
	return true;
}
#endif
