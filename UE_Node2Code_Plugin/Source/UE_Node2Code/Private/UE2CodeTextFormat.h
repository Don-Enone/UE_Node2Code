#pragma once

#include "CoreMinimal.h"

namespace UE2CodeTextFormat
{
	static FString Escape(FString Value)
	{
		Value.ReplaceInline(TEXT("\\"), TEXT("\\\\"));
		Value.ReplaceInline(TEXT("\""), TEXT("\\\""));
		Value.ReplaceInline(TEXT("\r\n"), TEXT("\\n"));
		Value.ReplaceInline(TEXT("\n"), TEXT("\\n"));
		Value.ReplaceInline(TEXT("\r"), TEXT("\\n"));
		Value.ReplaceInline(TEXT("\t"), TEXT("\\t"));
		return Value;
	}

	static FString Quote(const FString& Value)
	{
		return FString::Printf(TEXT("\"%s\""), *Escape(Value));
	}
}
