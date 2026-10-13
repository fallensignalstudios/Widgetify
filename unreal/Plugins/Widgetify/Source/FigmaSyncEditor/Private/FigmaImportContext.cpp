#include "FigmaImportContext.h"
#include "FigmaFontResolver.h"
#include "FigmaNaming.h"
#include "FigmaSyncEditorModule.h"
#include "FigmaSyncSettings.h"
#include "FigmaSyncStylePreset.h"
#include "FigmaTextureImporter.h"
#include "FigmaWidgetMapper.h"
#include "UObject/Package.h"

#define LOCTEXT_NAMESPACE "FigmaSync"

FText FFigmaImportResult::GetSummary() const
{
	return FText::Format(LOCTEXT("ImportSummary", "{0} widgets created, {1} updated, {2} textures imported ({3} unchanged). {4} warnings, {5} errors."),
		Stats.WidgetsCreated, Stats.WidgetsUpdated, Stats.TexturesImported, Stats.TexturesReused,
		Log->Count(EFigmaLogSeverity::Warning), Log->Count(EFigmaLogSeverity::Error));
}

FFigmaImportContext::FFigmaImportContext(const FFigmaBundle& InBundle, const FFigmaImportOptions& InOptions, const TSharedRef<FFigmaImportLog>& InLog)
	: Bundle(InBundle)
	, Document(InBundle.GetDocument())
	, Options(InOptions)
	, Settings(UFigmaSyncSettings::Get())
	, Log(InLog)
	, Textures(MakeShared<FFigmaTextureImporter>(*this))
{
	if (IFigmaSyncEditorModule::IsAvailable())
	{
		Mappers = IFigmaSyncEditorModule::Get().GetWidgetMappers();
	}
	SetPreset(nullptr);
}

FFigmaImportContext::~FFigmaImportContext() = default;

void FFigmaImportContext::SetPreset(UFigmaSyncStylePreset* InPreset)
{
	Preset = InPreset;
	Fonts = MakeShared<FFigmaFontResolver>(Settings, Preset);
}

void FFigmaImportContext::TouchPackage(UPackage* Package)
{
	if (Package)
	{
		TouchedPackages.Add(Package);
	}
}

bool FFigmaImportContext::IsButtonComponent(const FFigmaNode& Node) const
{
	return Node.Component.Kind == EFigmaComponentKind::Instance && FFigmaNaming::StartsWithAny(Node.Component.GetFamilyName(), Settings.ButtonComponentPrefixes);
}

bool FFigmaImportContext::IsButtonNode(const FFigmaNode& Node) const
{
	if (Node.IsText() || Node.IsRasterized() || Node.HasTag(TEXT("nobutton")))
	{
		return false;
	}
	return Node.HasTag(TEXT("button")) || FFigmaNaming::StartsWithAny(Node.CleanName, Settings.ButtonPrefixes) || IsButtonComponent(Node);
}

bool FFigmaImportContext::IsActionWidgetNode(const FFigmaNode& Node) const
{
	return !Node.IsText() && (Node.HasTag(TEXT("actionwidget")) || FFigmaNaming::StartsWithAny(Node.CleanName, Settings.ActionWidgetPrefixes));
}

bool FFigmaImportContext::ShouldBeVariable(const FFigmaNode& Node) const
{
	if (Node.HasTag(TEXT("novar")))
	{
		return false;
	}
	if (Node.HasTag(TEXT("var")) || Node.HasTag(TEXT("focus")) || Node.HasTag(TEXT("action")))
	{
		return true;
	}
	return FFigmaNaming::StartsWithAny(Node.CleanName, Settings.VariablePrefixes);
}

#undef LOCTEXT_NAMESPACE
