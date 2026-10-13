#include "FigmaLinter.h"
#include "Blueprint/UserWidget.h"
#include "Blueprint/WidgetTree.h"
#include "Components/Button.h"
#include "FigmaFontResolver.h"
#include "FigmaImportContext.h"
#include "FigmaNaming.h"
#include "FigmaReflection.h"
#include "FigmaSyncSettings.h"
#include "FigmaWidgetBuilder.h"
#include "FigmaWidgetMapper.h"
#include "Misc/EngineVersionComparison.h"
#include "UObject/UnrealType.h"
#include "WidgetBlueprint.h"

namespace
{
	bool IsInteractive(const UWidget* Widget)
	{
		return Widget && (Widget->IsA<UButton>() || Widget->IsA<UUserWidget>());
	}

	bool ReadBool(const UObject* Object, FName PropertyName, bool bDefault)
	{
		const FBoolProperty* Property = Object ? CastField<FBoolProperty>(FFigmaReflection::Find(Object->GetClass(), PropertyName)) : nullptr;
		return Property ? Property->GetPropertyValue_InContainer(Object) : bDefault;
	}

#if UE_VERSION_OLDER_THAN(5, 5, 0)
	bool IsCommonActivatable(const UClass* Class)
	{
		for (const UClass* Current = Class; Current; Current = Current->GetSuperClass())
		{
			if (Current->GetName() == TEXT("CommonActivatableWidget"))
			{
				return true;
			}
		}
		return false;
	}
#endif
}

void FFigmaLinter::Run(FFigmaImportContext& Context, UWidgetBlueprint* Blueprint)
{
	if (!Blueprint || !Blueprint->WidgetTree)
	{
		return;
	}
	FFigmaImportLog& Log = Context.Log.Get();
	const UFigmaSyncSettings& Settings = Context.Settings;

	// --- Focus paths ---------------------------------------------------------------------------------------------
	TArray<UWidget*> Interactive;
	Blueprint->WidgetTree->ForEachWidget([&Interactive](UWidget* Widget)
	{
		if (IsInteractive(Widget))
		{
			Interactive.Add(Widget);
		}
	});

	const UUserWidget* Defaults = Blueprint->GeneratedClass ? Cast<UUserWidget>(Blueprint->GeneratedClass->GetDefaultObject()) : nullptr;
	const FName DesiredFocus = Defaults ? Defaults->GetDesiredFocusWidgetName() : NAME_None;
	if (Interactive.Num() > 0 && DesiredFocus.IsNone())
	{
		Log.Lint(EFigmaLogSeverity::Warning, TEXT("Missing focus path: nothing gets initial gamepad/keyboard focus. Tag the first button [focus] in Figma, or set Desired Focus Widget in Class Defaults."));
	}
	if (!DesiredFocus.IsNone() && !Blueprint->WidgetTree->FindWidget(DesiredFocus))
	{
		Log.Lint(EFigmaLogSeverity::Warning, FString::Printf(TEXT("Desired Focus Widget '%s' no longer exists."), *DesiredFocus.ToString()));
	}
#if UE_VERSION_OLDER_THAN(5, 5, 0)
	if (!DesiredFocus.IsNone() && IsCommonActivatable(Blueprint->ParentClass))
	{
		Log.Lint(EFigmaLogSeverity::Info, TEXT("Before UE 5.5, CommonUI ignores Desired Focus Widget on activatable widgets: implement Get Desired Focus Target in the graph and return the focus widget."));
	}
#endif
	for (UWidget* Widget : Interactive)
	{
		if (UButton* Button = Cast<UButton>(Widget))
		{
			if (!ReadBool(Button, TEXT("IsFocusable"), true))
			{
				Log.Lint(EFigmaLogSeverity::Warning, FString::Printf(TEXT("Button '%s' is not focusable, so gamepad navigation will skip it."), *Button->GetName()));
			}
		}
	}

	// --- Input actions --------------------------------------------------------------------------------------------
	for (const FString& Unbound : Context.UnboundActions)
	{
		Log.Lint(EFigmaLogSeverity::Warning, FString::Printf(TEXT("Unbound input action: %s. Add the row to the input action table (or an Enhanced Input action with the configured prefix)."), *Unbound));
	}
	static const TCHAR* BackNames[] = { TEXT("Back"), TEXT("Close"), TEXT("Cancel"), TEXT("Exit"), TEXT("Return") };
	Context.Document.ForEachNode([&](const FFigmaNode& Node)
	{
		if (!Context.IsButtonNode(Node) || Node.HasTag(TEXT("action")))
		{
			return;
		}
		for (const TCHAR* BackName : BackNames)
		{
			if (Node.CleanName.EndsWith(BackName, ESearchCase::IgnoreCase))
			{
				Log.Lint(EFigmaLogSeverity::Info, TEXT("Looks like a back button: tag it [action=Back] so gamepad Back triggers it."), &Node);
				break;
			}
		}
	});

	// --- Fonts ----------------------------------------------------------------------------------------------------
	if (Context.Fonts.IsValid())
	{
		for (const TPair<FString, FFigmaResolvedFont>& Pair : Context.Fonts->GetUnmappedFonts())
		{
			if (Pair.Value.bFallback)
			{
				Log.Lint(EFigmaLogSeverity::Warning, FString::Printf(TEXT("Unmapped font '%s': using %s. Import the font and add it to Project Settings > Plugins > Widgetify > Font Mappings."),
					*Pair.Key, *Pair.Value.Describe()));
			}
			else if (Pair.Value.bAutoMatched)
			{
				Log.Lint(EFigmaLogSeverity::Info, FString::Printf(TEXT("Font '%s' matched by name to %s. Add a font mapping to make it explicit."), *Pair.Key, *Pair.Value.Describe()));
			}
		}
	}

	// --- Names that promise something -----------------------------------------------------------------------------
	Context.Document.ForEachNode([&](const FFigmaNode& Node)
	{
		if (FFigmaNaming::StartsWithAny(Node.CleanName, Settings.ButtonPrefixes) && (Node.IsText() || Node.IsRasterized()))
		{
			Log.Lint(EFigmaLogSeverity::Info, TEXT("Named like a button but it's a text/image layer; wrap it in a frame named Btn_... to make it clickable."), &Node);
		}
		if (Node.HasTag(TEXT("focus")) && FFigmaWidgetBuilder::Classify(Node, Context) == EFigmaWidgetKind::Container)
		{
			Log.Lint(EFigmaLogSeverity::Warning, TEXT("[focus] is on a container; put it on a button so something focusable gets focus."), &Node);
		}
	});

	// --- Resolution -----------------------------------------------------------------------------------------------
	if (Context.Document.Root.IsValid())
	{
		const FFigmaNode& Root = *Context.Document.Root;
		const FIntPoint Design = Settings.DesignResolution;
		const bool bFullScreen = Root.Width >= Design.X * 0.9f && Root.Height >= Design.Y * 0.9f;
		if (bFullScreen && (FMath::Abs(Root.Width - Design.X) > 1.f || FMath::Abs(Root.Height - Design.Y) > 1.f))
		{
			Log.Lint(EFigmaLogSeverity::Info, FString::Printf(TEXT("Frame is %.0fx%.0f but the design resolution is %dx%d. Pixel sizes map 1:1 to Slate units, so either design at %dx%d or change Project Settings > Widgetify > Design Resolution and the UI DPI curve."),
				Root.Width, Root.Height, Design.X, Design.Y, Design.X, Design.Y));
		}
	}

	for (const TSharedRef<IFigmaWidgetMapper>& Mapper : Context.Mappers)
	{
		Mapper->Lint(Blueprint, Context);
	}
}
