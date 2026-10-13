#include "FigmaDocument.h"
#include "FigmaSyncCoreModule.h"
#include "Modules/ModuleManager.h"

DEFINE_LOG_CATEGORY(LogFigmaSync);

IMPLEMENT_MODULE(FFigmaSyncCoreModule, FigmaSyncCore);

namespace
{
	float SRGBToLinear(float C)
	{
		C = FMath::Clamp(C, 0.f, 1.f);
		return C <= 0.04045f ? C / 12.92f : FMath::Pow((C + 0.055f) / 1.055f, 2.4f);
	}

	void VisitNodes(const FFigmaNode& Node, TFunctionRef<void(const FFigmaNode&)> Visitor)
	{
		Visitor(Node);
		for (const TSharedPtr<FFigmaNode>& Child : Node.Children)
		{
			if (Child.IsValid())
			{
				VisitNodes(*Child, Visitor);
			}
		}
	}
}

FLinearColor FFigmaColor::ToLinear(float Opacity) const
{
	return FLinearColor(SRGBToLinear(R), SRGBToLinear(G), SRGBToLinear(B), FMath::Clamp(A * Opacity, 0.f, 1.f));
}

FString FFigmaColor::ToHex() const
{
	auto Byte = [](float V) { return FMath::Clamp(FMath::RoundToInt(V * 255.f), 0, 255); };
	if (A >= 0.999f)
	{
		return FString::Printf(TEXT("#%02X%02X%02X"), Byte(R), Byte(G), Byte(B));
	}
	return FString::Printf(TEXT("#%02X%02X%02X%02X"), Byte(R), Byte(G), Byte(B), Byte(A));
}

FLinearColor FFigmaPaint::ToLinearColor() const
{
	if (Type == EFigmaPaintType::Solid)
	{
		return Color.ToLinear(Opacity);
	}
	if (Stops.Num() > 0)
	{
		return Stops[0].Color.ToLinear(Opacity);
	}
	return FLinearColor(1.f, 1.f, 1.f, Opacity);
}

const FFigmaPaint* FFigmaStyle::FindVisibleFill() const
{
	// Paints are ordered bottom to top; the exporter pre-composites simple fills into one.
	for (int32 Index = Fills.Num() - 1; Index >= 0; --Index)
	{
		if (Fills[Index].IsVisible())
		{
			return &Fills[Index];
		}
	}
	return nullptr;
}

const FFigmaPaint* FFigmaStyle::FindVisibleStroke() const
{
	for (int32 Index = Strokes.Num() - 1; Index >= 0; --Index)
	{
		if (Strokes[Index].IsVisible())
		{
			return &Strokes[Index];
		}
	}
	return nullptr;
}

bool FFigmaStyle::HasVisibleEffects() const
{
	for (const FFigmaEffect& Effect : Effects)
	{
		if (Effect.bVisible)
		{
			return true;
		}
	}
	return false;
}

FString FFigmaNode::GetTag(const TCHAR* Key, const FString& Default) const
{
	const FString* Value = Tags.Find(Key);
	return Value ? *Value : Default;
}

FString FFigmaNode::GetPath() const
{
	TArray<FString> Parts;
	for (const FFigmaNode* Node = this; Node; Node = Node->Parent)
	{
		Parts.Insert(Node->CleanName, 0);
	}
	return FString::Join(Parts, TEXT("/"));
}

int32 FFigmaNode::CountDescendants() const
{
	int32 Count = 0;
	for (const TSharedPtr<FFigmaNode>& ChildNode : Children)
	{
		if (ChildNode.IsValid())
		{
			Count += 1 + ChildNode->CountDescendants();
		}
	}
	return Count;
}

const FFigmaAsset* FFigmaDocument::FindAsset(const FString& AssetId) const
{
	return Assets.FindByPredicate([&AssetId](const FFigmaAsset& Asset) { return Asset.Id == AssetId; });
}

const FFigmaComponentSet* FFigmaDocument::FindComponentSet(const FString& SetId) const
{
	return Components.Find(SetId);
}

void FFigmaDocument::ForEachNode(TFunctionRef<void(const FFigmaNode&)> Visitor) const
{
	if (Root.IsValid())
	{
		VisitNodes(*Root, Visitor);
	}
}

const FFigmaNode* FFigmaDocument::FindNodeById(const FString& NodeId) const
{
	const FFigmaNode* Found = nullptr;
	ForEachNode([&](const FFigmaNode& Node)
	{
		if (!Found && Node.Id == NodeId)
		{
			Found = &Node;
		}
	});
	return Found;
}

int32 FFigmaDocument::CountNodes() const
{
	return Root.IsValid() ? 1 + Root->CountDescendants() : 0;
}
