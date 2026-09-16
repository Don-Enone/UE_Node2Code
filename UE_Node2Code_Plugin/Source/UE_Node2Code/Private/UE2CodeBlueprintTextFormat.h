#pragma once

#include "CoreMinimal.h"
#include "EdGraph/EdGraphPin.h"
#include "Internationalization/Regex.h"
#include "UObject/UnrealType.h"
#include "UObject/Package.h"

namespace UE2CodeBlueprintTextFormat
{
	// Change only the spelling of a number, never round it through float/double.
	static FString Number(const FString& Value)
	{
		static const FRegexPattern Pattern(TEXT("^[+-]?(?:[0-9]+(?:\\.[0-9]*)?|\\.[0-9]+)(?:[eE][+-]?[0-9]+)?$"));
		FRegexMatcher Match(Pattern, Value);
		if (!Match.FindNext()) { return Value; }
		int32 Exponent = Value.Find(TEXT("e"), ESearchCase::IgnoreCase);
		if (Exponent == INDEX_NONE) { Exponent = Value.Len(); }
		FString Mantissa = Value.Left(Exponent);
		if (Mantissa.Contains(TEXT(".")))
		{
			while (Mantissa.EndsWith(TEXT("0"))) { Mantissa.LeftChopInline(1); }
			if (Mantissa.EndsWith(TEXT("."))) { Mantissa.LeftChopInline(1); }
		}
		if (Mantissa.IsEmpty() || Mantissa == TEXT("+") || Mantissa == TEXT("-")) { Mantissa += TEXT("0"); }
		return Mantissa + Value.Mid(Exponent);
	}

	// This is used for identifiers only, never arbitrary text or GUID values.
	static FString FieldName(const FString& Name)
	{
		static const FRegexPattern Suffix(TEXT("_[0-9]+_[0-9A-Fa-f]{32}$"));
		FRegexMatcher Match(Suffix, Name);
		return Match.FindNext() ? Name.Left(Match.GetMatchBeginning()) : Name;
	}

	// Split UE property text without splitting nested structs, containers or strings.
	static TArray<FString> Split(const FString& Value, TCHAR Separator)
	{
		TArray<FString> Parts;
		int32 Start = 0, Depth = 0;
		TCHAR QuoteChar = 0;
		for (int32 Index = 0; Index < Value.Len(); ++Index)
		{
			const TCHAR Char = Value[Index];
			if (QuoteChar)
			{
				if (Char == TEXT('\\')) { ++Index; }
				else if (Char == QuoteChar) { QuoteChar = 0; }
			}
			else if (Char == TEXT('"') || Char == TEXT('\'')) { QuoteChar = Char; }
			else if (Char == TEXT('(')) { ++Depth; }
			else if (Char == TEXT(')')) { --Depth; }
			else if (Char == Separator && Depth == 0)
			{
				Parts.Add(Value.Mid(Start, Index - Start));
				Start = Index + 1;
			}
		}
		Parts.Add(Value.Mid(Start));
		return Parts;
	}

	struct FFormatter
	{
		bool bDebug = false;
		TMap<FString, FString> PathNames;
		TMap<FString, FString> NameOwners;

		FString Path(const FString& Value)
		{
			if (bDebug || !Value.StartsWith(TEXT("/"))) { return Value; }
			if (const FString* Existing = PathNames.Find(Value)) { return *Existing; }
			int32 Delimiter = INDEX_NONE;
			Value.FindLastChar(TEXT('.'), Delimiter);
			if (Delimiter == INDEX_NONE) { Value.FindLastChar(TEXT('/'), Delimiter); }
			FString Short = Value.Mid(Delimiter + 1);
			// Equal short names from different packages must not become the same symbol.
			if (const FString* Owner = NameOwners.Find(Short))
			{
				if (*Owner != Value) { Short = Value; }
			}
			NameOwners.Add(Short, Value);
			PathNames.Add(Value, Short);
			return Short;
		}

		FString ObjectValue(const FString& Value)
		{
			if (bDebug) { return Value; }
			FString ObjectPath = Value;
			int32 QuoteIndex;
			if (Value.FindChar(TEXT('\''), QuoteIndex) && Value.EndsWith(TEXT("'")))
			{
				ObjectPath = Value.Mid(QuoteIndex + 1, Value.Len() - QuoteIndex - 2);
			}
			return Path(ObjectPath);
		}

		FString Property(const FProperty* Prop, const FString& Value, int32 Depth = 0)
		{
			if (!Prop || bDebug || Depth > 64) { return Value; }
			if (const FNumericProperty* Numeric = CastField<FNumericProperty>(Prop))
			{
				return Numeric->IsFloatingPoint() ? Number(Value) : Value;
			}
			if (const FStructProperty* Struct = CastField<FStructProperty>(Prop)) { return StructValue(Struct->Struct, Value, Depth + 1); }
			if (CastField<FObjectPropertyBase>(Prop) || CastField<FSoftObjectProperty>(Prop)) { return ObjectValue(Value); }
			if (Value.StartsWith(TEXT("(")) && Value.EndsWith(TEXT(")")))
			{
				const FArrayProperty* Array = CastField<FArrayProperty>(Prop);
				const FSetProperty* Set = CastField<FSetProperty>(Prop);
				const FMapProperty* Map = CastField<FMapProperty>(Prop);
				if (Array || Set || Map)
				{
					TArray<FString> Parts = Split(Value.Mid(1, Value.Len() - 2), TEXT(','));
					for (FString& Part : Parts)
					{
						if (!Map) { Part = Property(Array ? Array->Inner : Set->ElementProp, Part, Depth + 1); }
						else if (Part.StartsWith(TEXT("(")) && Part.EndsWith(TEXT(")")))
						{
							TArray<FString> Pair = Split(Part.Mid(1, Part.Len() - 2), TEXT(','));
							if (Pair.Num() == 2) { Part = TEXT("(") + Property(Map->KeyProp, Pair[0], Depth + 1) + TEXT(",") + Property(Map->ValueProp, Pair[1], Depth + 1) + TEXT(")"); }
						}
					}
					return TEXT("(") + FString::Join(Parts, TEXT(",")) + TEXT(")");
				}
			}
			return Value;
		}

		FString StructValue(const UScriptStruct* Struct, const FString& Value, int32 Depth = 0)
		{
			if (!Struct || bDebug || Depth > 64) { return Value; }
			if (!Value.StartsWith(TEXT("(")) || !Value.EndsWith(TEXT(")")))
			{
				// K2 vector/rotator defaults can use a positional numeric form.
				const FString Name = Struct->GetName();
				if (Struct->GetOutermost()->GetFName() == FName(TEXT("/Script/CoreUObject"))
					&& (Name == TEXT("Vector") || Name == TEXT("Rotator") || Name == TEXT("Vector2D")))
				{
					TArray<FString> Parts = Split(Value, TEXT(','));
					for (FString& Part : Parts) { Part = Number(Part.TrimStartAndEnd()); }
					return FString::Join(Parts, TEXT(","));
				}
				return Value;
			}
			TMap<FString, int32> Names;
			for (TFieldIterator<FProperty> It(Struct); It; ++It) { ++Names.FindOrAdd(FieldName(It->GetName())); }
			TArray<FString> Fields = Split(Value.Mid(1, Value.Len() - 2), TEXT(','));
			for (FString& Field : Fields)
			{
				TArray<FString> Pair = Split(Field, TEXT('='));
				if (Pair.Num() != 2) { continue; }
				const FProperty* Prop = FindFProperty<FProperty>(Struct, FName(*Pair[0]));
				if (!Prop) { continue; }
				FString Name = FieldName(Prop->GetName());
				if (Names.FindRef(Name) > 1) { Name = Prop->GetName(); }
				Field = Name + TEXT("=") + Property(Prop, Pair[1], Depth + 1);
			}
			return TEXT("(") + FString::Join(Fields, TEXT(",")) + TEXT(")");
		}

		FString Value(const FEdGraphPinType& Type, const FString& Text, int32 Depth = 0)
		{
			if (bDebug || Depth > 64) { return Text; }
			if (Type.IsArray() || Type.IsSet())
			{
				if (!Text.StartsWith(TEXT("(")) || !Text.EndsWith(TEXT(")"))) { return Text; }
				FEdGraphPinType Element = Type;
				Element.ContainerType = EPinContainerType::None;
				TArray<FString> Parts = Split(Text.Mid(1, Text.Len() - 2), TEXT(','));
				for (FString& Part : Parts) { Part = Value(Element, Part, Depth + 1); }
				return TEXT("(") + FString::Join(Parts, TEXT(",")) + TEXT(")");
			}
			if (Type.IsMap()) { return Text; } // Unknown pin map syntax is retained verbatim.
			if (Type.PinCategory == TEXT("float") || Type.PinCategory == TEXT("double") || Type.PinCategory == TEXT("real")) { return Number(Text); }
			if (Type.PinCategory == TEXT("struct")) { return StructValue(Cast<UScriptStruct>(Type.PinSubCategoryObject.Get()), Text, Depth + 1); }
			if (Type.PinCategory == TEXT("object") || Type.PinCategory == TEXT("class")
				|| Type.PinCategory == TEXT("softobject") || Type.PinCategory == TEXT("softclass")) { return ObjectValue(Text); }
			return Text;
		}
	};
}
