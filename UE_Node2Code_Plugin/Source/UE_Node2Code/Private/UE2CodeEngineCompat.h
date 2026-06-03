#pragma once

#include "Materials/Material.h"
#include "Materials/MaterialExpression.h"
#include "Materials/MaterialFunctionInterface.h"
#include "Runtime/Launch/Resources/Version.h"
#include "UObject/UnrealType.h"

namespace UE2CodeEngineCompat
{
	static const UMaterialFunctionInterface* GetBaseFunction(const UMaterialFunctionInterface* Function)
	{
		if (!Function)
		{
			return nullptr;
		}

#if ENGINE_MAJOR_VERSION >= 5
		const UMaterialFunctionInterface* BaseFunction = Function->GetBaseFunctionInterface();
		return BaseFunction ? static_cast<const UMaterialFunctionInterface*>(BaseFunction) : Function;
#else
		const UMaterialFunctionInterface* BaseFunction = Function->GetBaseFunction();
		return BaseFunction ? BaseFunction : Function;
#endif
	}

	static bool GetMaterialExpressions(UMaterial* Material, TArray<UMaterialExpression*>& OutExpressions)
	{
		OutExpressions.Reset();
		if (!Material)
		{
			return false;
		}

#if ENGINE_MAJOR_VERSION >= 5
		for (const auto& ExpressionPtr : Material->GetExpressions())
		{
			if (UMaterialExpression* Expression = ExpressionPtr.Get())
			{
				OutExpressions.Add(Expression);
			}
		}
#else
		OutExpressions.Append(Material->Expressions);
#endif
		return true;
	}

	static bool GetFunctionExpressions(const UMaterialFunctionInterface* Function, TArray<UMaterialExpression*>& OutExpressions)
	{
		OutExpressions.Reset();
		if (!Function)
		{
			return false;
		}

#if ENGINE_MAJOR_VERSION >= 5
		const UMaterialFunctionInterface* BaseFunctionInterface = GetBaseFunction(Function);
		if (!BaseFunctionInterface)
		{
			return false;
		}

		for (const auto& ExpressionPtr : BaseFunctionInterface->GetExpressions())
		{
			if (UMaterialExpression* Expression = ExpressionPtr.Get())
			{
				OutExpressions.Add(Expression);
			}
		}
#else
		const TArray<UMaterialExpression*>* FunctionExpressions = Function->GetFunctionExpressions();
		if (!FunctionExpressions)
		{
			return false;
		}
		OutExpressions.Append(*FunctionExpressions);
#endif
		return true;
	}

	static TArray<FExpressionInput*> GetExpressionInputs(UMaterialExpression* Expression)
	{
		TArray<FExpressionInput*> Inputs;
		if (!Expression)
		{
			return Inputs;
		}

#if ENGINE_MAJOR_VERSION >= 5
		const int32 InputCount = Expression->CountInputs();
		for (int32 InputIndex = 0; InputIndex < InputCount; ++InputIndex)
		{
			if (FExpressionInput* Input = Expression->GetInput(InputIndex))
			{
				Inputs.Add(Input);
			}
		}
#else
		Inputs = Expression->GetInputs();
#endif
		return Inputs;
	}

	static void ExportPropertyText(const FProperty* Property, FString& OutValue, const void* ValuePtr, UObject* Parent)
	{
		if (!Property)
		{
			return;
		}

#if ENGINE_MAJOR_VERSION >= 5
		Property->ExportText_Direct(OutValue, ValuePtr, nullptr, Parent, PPF_None);
#else
		Property->ExportTextItem(OutValue, ValuePtr, nullptr, Parent, PPF_None);
#endif
	}

	template <typename ElementType, typename AllocatorType>
	static ElementType PopNoShrink(TArray<ElementType, AllocatorType>& Array)
	{
#if ENGINE_MAJOR_VERSION >= 5
		return Array.Pop(EAllowShrinking::No);
#else
		return Array.Pop(false);
#endif
	}

	static FString GetFunctionDescription(const UMaterialFunctionInterface* Function)
	{
		if (!Function)
		{
			return FString();
		}

#if ENGINE_MAJOR_VERSION >= 5
		return Function->GetDescription();
#else
		if (const FString* Description = Function->GetDescription())
		{
			return *Description;
		}
		return FString();
#endif
	}
}
