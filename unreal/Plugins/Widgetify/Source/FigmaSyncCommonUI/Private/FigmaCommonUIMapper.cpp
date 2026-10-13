#include "FigmaCommonUIMapper.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "AssetRegistry/IAssetRegistry.h"
#include "Blueprint/UserWidget.h"
#include "Blueprint/WidgetTree.h"
#include "Components/TextBlock.h"
#include "EdGraph/EdGraph.h"
#include "EdGraphSchema_K2.h"
#include "Engine/Blueprint.h"
#include "Engine/DataTable.h"
#include "FigmaAssetUtils.h"
#include "FigmaButtonStates.h"
#include "FigmaFontResolver.h"
#include "FigmaImportContext.h"
#include "FigmaNaming.h"
#include "FigmaReflection.h"
#include "FigmaStyleConverter.h"
#include "FigmaSyncBlueprintExtension.h"
#include "FigmaSyncSettings.h"
#include "FigmaSyncStylePreset.h"
#include "FigmaWidgetBuilder.h"
#include "Fonts/SlateFontInfo.h"
#include "K2Node_CallFunction.h"
#include "K2Node_Event.h"
#include "K2Node_VariableGet.h"
#include "Kismet2/BlueprintEditorUtils.h"
#include "Layout/Margin.h"
#include "Modules/ModuleManager.h"
#include "Styling/SlateBrush.h"
#include "Templates/UnrealTemplate.h"
#include "UObject/UnrealType.h"
#include "WidgetBlueprint.h"

#define LOCTEXT_NAMESPACE "FigmaSyncCommonUI"

namespace
{
	const TCHAR* GraphMarker = TEXT("Widgetify");

	FString GroupSuffix(const FString& Group)
	{
		return Group.IsEmpty() ? FString() : TEXT("_") + FFigmaNaming::SanitizeIdentifier(Group.Replace(TEXT("="), TEXT("_")).Replace(TEXT(","), TEXT("_")), TEXT("Group"));
	}

	UObject* FindEnhancedInputAction(const FString& AssetName)
	{
		IAssetRegistry& Registry = FModuleManager::LoadModuleChecked<FAssetRegistryModule>(TEXT("AssetRegistry")).Get();
		TArray<FAssetData> Assets;
		Registry.GetAssetsByClass(FTopLevelAssetPath(TEXT("/Script/EnhancedInput"), TEXT("InputAction")), Assets, true);
		for (const FAssetData& Asset : Assets)
		{
			if (Asset.AssetName.ToString().Equals(AssetName, ESearchCase::IgnoreCase))
			{
				return Asset.GetAsset();
			}
		}
		return nullptr;
	}
}

// ---------------------------------------------------------------------------------------------------------------- Setup

bool FFigmaCommonUIMapper::LoadCommonUI(FFigmaImportContext& Context)
{
	const UFigmaSyncSettings& Settings = Context.Settings;
	ButtonStyleClass = FFigmaReflection::LoadClass(TEXT("/Script/CommonUI.CommonButtonStyle"));
	TextStyleClass = FFigmaReflection::LoadClass(TEXT("/Script/CommonUI.CommonTextStyle"));
	TextBlockClass = FFigmaReflection::LoadClass(TEXT("/Script/CommonUI.CommonTextBlock"));
	ActionWidgetClass = FFigmaReflection::LoadClass(TEXT("/Script/CommonUI.CommonActionWidget"));

	const FSoftClassPath BasePath = Context.Preset && Context.Preset->ButtonBaseClass.IsValid() ? Context.Preset->ButtonBaseClass : Settings.ButtonBaseClass;
	ButtonBaseClass = FFigmaReflection::LoadClass(BasePath.ToString());

	return Settings.bEnableCommonUI && ButtonStyleClass.IsValid() && TextStyleClass.IsValid() && ButtonBaseClass.IsValid() &&
		ButtonBaseClass->IsChildOf(UUserWidget::StaticClass());
}

void FFigmaCommonUIMapper::PrepareImport(FFigmaImportContext& Context)
{
	Sets.Reset();
	SyntheticSets.Reset();
	bBuildingButton = false;
	bAvailable = LoadCommonUI(Context);
	if (!bAvailable)
	{
		if (Context.Settings.bEnableCommonUI)
		{
			Context.Log->Info(TEXT("CommonUI is not enabled in this project (or the button base class is missing); buttons are plain UMG buttons."));
		}
		return;
	}

	TArray<const FFigmaNode*> Instances;
	Context.Document.ForEachNode([&](const FFigmaNode& Node)
	{
		if (Context.IsButtonComponent(Node))
		{
			Instances.Add(&Node);
		}
	});
	for (const FFigmaNode* Instance : Instances)
	{
		PrepareButtonSet(*Instance, Context);
	}
}

FString FFigmaCommonUIMapper::GetSetId(const FFigmaNode& Node)
{
	if (!Node.Component.ComponentSetId.IsEmpty())
	{
		return Node.Component.ComponentSetId;
	}
	return Node.Component.ComponentId.IsEmpty() ? TEXT("instance:") + Node.Id : TEXT("component:") + Node.Component.ComponentId;
}

const FFigmaNode* FFigmaCommonUIMapper::FindLabelNode(const FFigmaNode& Root, const FString& PropertyName)
{
	const FFigmaNode* FirstText = nullptr;
	TArray<const FFigmaNode*> Stack;
	Stack.Add(&Root);
	while (Stack.Num() > 0)
	{
		const FFigmaNode* Node = Stack.Pop();
		if (Node->IsText())
		{
			if (!PropertyName.IsEmpty() && Node->PropRefs.FindRef(TEXT("characters")) == PropertyName)
			{
				return Node;
			}
			FirstText = FirstText ? FirstText : Node;
		}
		for (int32 Index = Node->Children.Num() - 1; Index >= 0; --Index)
		{
			if (Node->Children[Index].IsValid())
			{
				Stack.Add(Node->Children[Index].Get());
			}
		}
	}
	return FirstText;
}

const FFigmaCommonUIMapper::FButtonSet* FFigmaCommonUIMapper::FindSetForNode(const FFigmaNode& Node) const
{
	return Node.Component.IsSet() ? Sets.Find(GetSetId(Node)) : nullptr;
}

void FFigmaCommonUIMapper::PrepareButtonSet(const FFigmaNode& Instance, FFigmaImportContext& Context)
{
	const FString SetId = GetSetId(Instance);
	if (Sets.Contains(SetId))
	{
		return;
	}

	const FFigmaComponentSet* Set = Context.Document.FindComponentSet(Instance.Component.ComponentSetId);
	bool bDerived = false;
	if (!Set || Set->Variants.Num() == 0)
	{
		// Variants weren't exported (e.g. a library component): the instance itself is the only state we know.
		FFigmaComponentSet& Synthetic = SyntheticSets.Add(SetId);
		Synthetic.Id = SetId;
		Synthetic.Name = Instance.Component.GetFamilyName().IsEmpty() ? Instance.CleanName : Instance.Component.GetFamilyName();
		Instance.Component.Variant.GetKeys(Synthetic.PropertyNames);
		FFigmaVariant Variant;
		Variant.Id = Instance.Id;
		Variant.Name = Instance.Component.ComponentName;
		Variant.Properties = Instance.Component.Variant;
		Variant.Node = MakeShared<FFigmaNode>(Instance);
		Synthetic.Variants.Add(Variant);
		Set = &Synthetic;
		bDerived = true;
	}

	FButtonSet& Info = Sets.Add(SetId);
	Info.Name = Set->Name;
	Info.StateProperty = FFigmaButtonStates::FindStateProperty(*Set);
	Info.bDerivedStates = bDerived;
	for (const TCHAR* Preferred : { TEXT("Label"), TEXT("Text"), TEXT("Title"), TEXT("Caption") })
	{
		if (Instance.Component.TextProperties.Contains(Preferred))
		{
			Info.LabelComponentProperty = Preferred;
			break;
		}
	}
	if (Info.LabelComponentProperty.IsEmpty() && Instance.Component.TextProperties.Num() > 0)
	{
		TArray<FString> Keys;
		Instance.Component.TextProperties.GetKeys(Keys);
		Info.LabelComponentProperty = Keys[0];
	}

	const FString Base = FFigmaNaming::PathToIdentifier(Set->Name);
	const FString Folder = Context.SharedFolder;
	const FString StyleFolder = Folder / TEXT("Styles");
	const TArray<FString> Groups = FFigmaButtonStates::GetGroups(*Set, Info.StateProperty);

	const FFigmaButtonStyleOverride* Override = Context.Preset ? Context.Preset->FindButtonStyle(Set->Name) : nullptr;
	for (const FString& Group : Groups)
	{
		UClass* Style = Override && Override->CommonButtonStyle.IsValid() ? Override->CommonButtonStyle.TryLoadClass<UObject>() : nullptr;
		if (!Style)
		{
			Style = MakeButtonStyle(StyleFolder, TEXT("BS_") + Base + GroupSuffix(Group), *Set, Info, Group, Context);
		}
		if (Style)
		{
			Info.StyleByGroup.Add(Group, Style);
		}
	}

	const FString FirstGroup = Groups.Num() > 0 ? Groups[0] : FString();
	const FFigmaVariant* ContentVariant = FFigmaButtonStates::FindVariant(*Set, Info.StateProperty, FirstGroup, EFigmaButtonState::Normal);
	if (!ContentVariant)
	{
		ContentVariant = &Set->Variants[0];
	}

	// The label layer inside the button uses the Normal text style.
	UClass* LabelStyle = nullptr;
	if (const TWeakObjectPtr<UClass>* DefaultStyle = Info.StyleByGroup.Find(FirstGroup))
	{
		if (UObject* StyleDefaults = DefaultStyle->IsValid() ? DefaultStyle->Get()->GetDefaultObject() : nullptr)
		{
			if (FClassProperty* TextStyleProperty = CastField<FClassProperty>(FFigmaReflection::Find(StyleDefaults->GetClass(), TEXT("NormalTextStyle"))))
			{
				LabelStyle = Cast<UClass>(TextStyleProperty->GetObjectPropertyValue_InContainer(StyleDefaults));
			}
		}
	}

	if (ContentVariant->Node.IsValid())
	{
		UWidgetBlueprint* ButtonBlueprint = MakeButtonWidget(Folder, TEXT("WBP_") + Base, *ContentVariant->Node, Info, LabelStyle, Context);
		Info.Blueprint = ButtonBlueprint;
		Info.WidgetClass = ButtonBlueprint ? ButtonBlueprint->GeneratedClass.Get() : nullptr;
	}

	if (UObject* ButtonDefaults = Info.WidgetClass.IsValid() ? Info.WidgetClass->GetDefaultObject() : nullptr)
	{
		ButtonDefaults->Modify();
		if (const TWeakObjectPtr<UClass>* DefaultStyle = Info.StyleByGroup.Find(FirstGroup))
		{
			FFigmaReflection::SetClass(ButtonDefaults, TEXT("Style"), DefaultStyle->Get());
		}
		ButtonDefaults->MarkPackageDirty();
	}
	if (Info.WidgetClass.IsValid())
	{
		Context.Log->Info(FString::Printf(TEXT("CommonUI button %s: %d style(s)%s."), *Info.WidgetClass->GetName(), Info.StyleByGroup.Num(),
			Info.bDerivedStates ? TEXT(", states derived (no variants exported)") : TEXT("")));
	}
}

UClass* FFigmaCommonUIMapper::FindOrCreateBlueprintClass(const FString& Folder, const FString& AssetName, UClass* ParentClass, FFigmaImportContext& Context)
{
	if (!ParentClass)
	{
		return nullptr;
	}
	if (UBlueprint* Existing = Cast<UBlueprint>(FFigmaAssetUtils::FindAsset(Folder, AssetName, UBlueprint::StaticClass())))
	{
		if (Existing->GeneratedClass && Existing->GeneratedClass->IsChildOf(ParentClass))
		{
			Context.TouchPackage(Existing->GetPackage());
			return Existing->GeneratedClass;
		}
		Context.Log->Warning(FString::Printf(TEXT("%s/%s exists but is not a %s; it was left alone."), *Folder, *AssetName, *ParentClass->GetName()));
		return nullptr;
	}
	if (FFigmaAssetUtils::DoesAssetExist(Folder, AssetName))
	{
		Context.Log->Warning(FString::Printf(TEXT("%s/%s exists but is not a Blueprint; it was left alone."), *Folder, *AssetName));
		return nullptr;
	}

	FString Error;
	UBlueprint* Blueprint = FFigmaAssetUtils::CreateBlueprintClass(Folder, AssetName, ParentClass, Error);
	if (!Blueprint)
	{
		Context.Log->Error(Error);
		return nullptr;
	}
	FFigmaAssetUtils::CompileBlueprint(Blueprint, &Context.Log.Get());
	Context.TouchPackage(Blueprint->GetPackage());
	return Blueprint->GeneratedClass;
}

// ---------------------------------------------------------------------------------------------------------------- Styles

UClass* FFigmaCommonUIMapper::MakeTextStyle(const FString& Folder, const FString& AssetName, const FFigmaNode& TextNode, FFigmaImportContext& Context)
{
	UClass* StyleClass = FindOrCreateBlueprintClass(Folder, AssetName, TextStyleClass.Get(), Context);
	UObject* Defaults = StyleClass ? StyleClass->GetDefaultObject() : nullptr;
	if (!Defaults || !TextNode.bHasText)
	{
		return StyleClass;
	}
	Defaults->Modify();
	const FFigmaText& Text = TextNode.Text;
	FFigmaReflection::SetStruct(Defaults, TEXT("Font"), FSlateFontInfo::StaticStruct(), Context.Fonts->MakeFontInfo(Text));
	FFigmaReflection::SetStruct(Defaults, TEXT("Color"), TBaseStructure<FLinearColor>::Get(), Text.Color.ToLinear());
	FFigmaReflection::SetBool(Defaults, TEXT("bUsesDropShadow"), Text.bHasShadow);
	if (Text.bHasShadow)
	{
		FFigmaReflection::SetStruct(Defaults, TEXT("ShadowOffset"), TBaseStructure<FVector2D>::Get(), Text.ShadowOffset);
		FFigmaReflection::SetStruct(Defaults, TEXT("ShadowColor"), TBaseStructure<FLinearColor>::Get(), Text.ShadowColor.ToLinear());
	}
	Defaults->MarkPackageDirty();
	return StyleClass;
}

UClass* FFigmaCommonUIMapper::MakeButtonStyle(const FString& Folder, const FString& AssetName, const FFigmaComponentSet& Set, const FButtonSet& Info, const FString& Group, FFigmaImportContext& Context)
{
	UClass* StyleClass = FindOrCreateBlueprintClass(Folder, AssetName, ButtonStyleClass.Get(), Context);
	UObject* Defaults = StyleClass ? StyleClass->GetDefaultObject() : nullptr;
	if (!Defaults)
	{
		return StyleClass;
	}
	Defaults->Modify();

	const FString TextureFolder = Context.SharedFolder / TEXT("Textures");
	auto VariantFor = [&](EFigmaButtonState State) { return FFigmaButtonStates::FindVariant(Set, Info.StateProperty, Group, State); };
	const FFigmaVariant* Normal = VariantFor(EFigmaButtonState::Normal);
	if (!Normal)
	{
		for (const FFigmaVariant& Variant : Set.Variants)
		{
			if (FFigmaButtonStates::GroupKey(Variant.Properties, Info.StateProperty) == Group)
			{
				Normal = &Variant;
				break;
			}
		}
	}
	if (!Normal || !Normal->Node.IsValid())
	{
		return StyleClass;
	}

	const float HoverBrightness = Context.Preset ? Context.Preset->HoverBrightness : 1.15f;
	const float PressedBrightness = Context.Preset ? Context.Preset->PressedBrightness : 0.85f;
	const float DisabledOpacity = Context.Preset ? Context.Preset->DisabledOpacity : 0.45f;

	const FSlateBrush NormalBrush = FFigmaWidgetBuilder::MakeNodeBackgroundBrush(*Normal->Node, Context, TextureFolder);
	auto StateBrush = [&](EFigmaButtonState State, const FSlateBrush& Fallback)
	{
		const FFigmaVariant* Variant = VariantFor(State);
		return Variant && Variant->Node.IsValid() ? FFigmaWidgetBuilder::MakeNodeBackgroundBrush(*Variant->Node, Context, TextureFolder) : Fallback;
	};
	const FSlateBrush Hovered = StateBrush(EFigmaButtonState::Hovered, FFigmaStyleConverter::ScaleBrush(NormalBrush, HoverBrightness));
	const FSlateBrush Pressed = StateBrush(EFigmaButtonState::Pressed, FFigmaStyleConverter::ScaleBrush(NormalBrush, PressedBrightness));
	const FSlateBrush Disabled = StateBrush(EFigmaButtonState::Disabled, FFigmaStyleConverter::ScaleBrush(NormalBrush, 1.f, DisabledOpacity));
	const FSlateBrush Selected = StateBrush(EFigmaButtonState::Selected, NormalBrush);
	const FSlateBrush SelectedHovered = StateBrush(EFigmaButtonState::SelectedHovered, FFigmaStyleConverter::ScaleBrush(Selected, HoverBrightness));
	const FSlateBrush SelectedPressed = StateBrush(EFigmaButtonState::SelectedPressed, FFigmaStyleConverter::ScaleBrush(Selected, PressedBrightness));

	const UScriptStruct* BrushStruct = FSlateBrush::StaticStruct();
	FFigmaReflection::SetBool(Defaults, TEXT("bSingleMaterial"), false);
	FFigmaReflection::SetStruct(Defaults, TEXT("NormalBase"), BrushStruct, NormalBrush);
	FFigmaReflection::SetStruct(Defaults, TEXT("NormalHovered"), BrushStruct, Hovered);
	FFigmaReflection::SetStruct(Defaults, TEXT("NormalPressed"), BrushStruct, Pressed);
	FFigmaReflection::SetStruct(Defaults, TEXT("SelectedBase"), BrushStruct, Selected);
	FFigmaReflection::SetStruct(Defaults, TEXT("SelectedHovered"), BrushStruct, SelectedHovered);
	FFigmaReflection::SetStruct(Defaults, TEXT("SelectedPressed"), BrushStruct, SelectedPressed);
	FFigmaReflection::SetStruct(Defaults, TEXT("Disabled"), BrushStruct, Disabled);

	// Auto-layout buttons hug their label like in Figma (fixed-size instances get a SizeBox on the screen);
	// free-form buttons keep their design size as the minimum.
	const FFigmaNode& Frame = *Normal->Node;
	const bool bContentSized = Frame.IsAutoLayout();
	if (bContentSized)
	{
		FFigmaReflection::SetStruct(Defaults, TEXT("ButtonPadding"), FMargin::StaticStruct(), FFigmaStyleConverter::ToMargin(Frame.Layout.Padding));
	}
	FFigmaReflection::SetInt(Defaults, TEXT("MinWidth"), bContentSized ? 0 : FMath::RoundToInt(Frame.Width));
	FFigmaReflection::SetInt(Defaults, TEXT("MinHeight"), bContentSized ? 0 : FMath::RoundToInt(Frame.Height));

	// Label text style per state, from each variant's label layer.
	const FString TextBase = TEXT("TS_") + FFigmaNaming::PathToIdentifier(Set.Name) + GroupSuffix(Group);
	auto TextStyleFor = [&](EFigmaButtonState State, const TCHAR* Suffix) -> UClass*
	{
		const FFigmaVariant* Variant = VariantFor(State);
		const FFigmaNode* Label = Variant && Variant->Node.IsValid() ? FindLabelNode(*Variant->Node, Info.LabelComponentProperty) : nullptr;
		return Label ? MakeTextStyle(Folder, TextBase + Suffix, *Label, Context) : nullptr;
	};
	UClass* NormalText = nullptr;
	if (const FFigmaNode* Label = FindLabelNode(Frame, Info.LabelComponentProperty))
	{
		NormalText = MakeTextStyle(Folder, TextBase + TEXT("_Normal"), *Label, Context);
	}
	UClass* HoveredText = TextStyleFor(EFigmaButtonState::Hovered, TEXT("_Hovered"));
	UClass* DisabledText = TextStyleFor(EFigmaButtonState::Disabled, TEXT("_Disabled"));
	UClass* SelectedText = TextStyleFor(EFigmaButtonState::Selected, TEXT("_Selected"));
	UClass* SelectedHoveredText = TextStyleFor(EFigmaButtonState::SelectedHovered, TEXT("_SelectedHovered"));
	FFigmaReflection::SetClass(Defaults, TEXT("NormalTextStyle"), NormalText);
	FFigmaReflection::SetClass(Defaults, TEXT("NormalHoveredTextStyle"), HoveredText ? HoveredText : NormalText);
	FFigmaReflection::SetClass(Defaults, TEXT("DisabledTextStyle"), DisabledText ? DisabledText : NormalText);
	FFigmaReflection::SetClass(Defaults, TEXT("SelectedTextStyle"), SelectedText ? SelectedText : NormalText);
	FFigmaReflection::SetClass(Defaults, TEXT("SelectedHoveredTextStyle"), SelectedHoveredText ? SelectedHoveredText : (SelectedText ? SelectedText : NormalText));

	Defaults->MarkPackageDirty();
	return StyleClass;
}

// ---------------------------------------------------------------------------------------------------------------- Button widget

UWidgetBlueprint* FFigmaCommonUIMapper::MakeButtonWidget(const FString& Folder, const FString& AssetName, const FFigmaNode& ContentRoot, FButtonSet& Info, UClass* LabelStyle, FFigmaImportContext& Context)
{
	UWidgetBlueprint* Blueprint = Cast<UWidgetBlueprint>(FFigmaAssetUtils::FindAsset(Folder, AssetName, UWidgetBlueprint::StaticClass()));
	if (!Blueprint)
	{
		if (FFigmaAssetUtils::DoesAssetExist(Folder, AssetName))
		{
			Context.Log->Warning(FString::Printf(TEXT("%s/%s exists but is not a Widget Blueprint; CommonUI button skipped."), *Folder, *AssetName));
			return nullptr;
		}
		FString Error;
		Blueprint = FFigmaAssetUtils::CreateWidgetBlueprint(Folder, AssetName, ButtonBaseClass.Get(), Error);
		if (!Blueprint)
		{
			Context.Log->Error(Error);
			return nullptr;
		}
	}
	else if (!Blueprint->ParentClass || !Blueprint->ParentClass->IsChildOf(ButtonBaseClass.Get()))
	{
		Context.Log->Warning(FString::Printf(TEXT("%s is not a %s anymore; its content was still synced."), *AssetName, *ButtonBaseClass->GetName()));
	}

	UFigmaSyncBlueprintExtension* State = UFigmaSyncBlueprintExtension::FindOrAdd(Blueprint);
	const FFigmaNode* LabelNode = FindLabelNode(ContentRoot, Info.LabelComponentProperty);
	{
		TGuardValue<bool> BuildingGuard(bBuildingButton, true);
		// A [focus] tag inside the component must not become the screen's focus target.
		TGuardValue<FName> FocusGuard(Context.FocusWidgetName, Context.FocusWidgetName);
		CurrentLabelNodeId = LabelNode ? LabelNode->Id : FString();
		CurrentLabelStyle = LabelStyle;

		FFigmaBuildOptions BuildOptions;
		BuildOptions.bComponentContent = true;
		BuildOptions.TextureFolder = Folder / TEXT("Textures");
		FFigmaWidgetBuilder Builder(Context, Blueprint, State, BuildOptions);
		Builder.Build(ContentRoot);

		CurrentLabelNodeId.Reset();
		CurrentLabelStyle.Reset();
	}
	if (State)
	{
		State->SourcePath = Context.Options.SourcePath.IsEmpty() ? Context.Bundle.GetSourcePath() : Context.Options.SourcePath;
		State->FigmaFileName = Context.Document.FileName;
		State->FrameId = ContentRoot.Id;
		State->FrameName = Info.Name;
		State->LastSyncTime = FDateTime::Now();
	}

	const FName LabelWidget = LabelNode ? Context.BuiltWidgets.FindRef(LabelNode->Id) : NAME_None;
	if (UWidget* Label = !LabelWidget.IsNone() ? FindObject<UWidget>(Blueprint->WidgetTree, *LabelWidget.ToString()) : nullptr)
	{
		Label->Modify();
		Label->bIsVariable = true;
	}
	FFigmaAssetUtils::CompileBlueprint(Blueprint, &Context.Log.Get());

	// The instance label goes into a text property: the base class's own (e.g. a studio button base), or a
	// ButtonText variable that a small PreConstruct graph copies into the label.
	const FName TextProperty = Context.Settings.ButtonTextPropertyName.IsNone() ? FName(TEXT("ButtonText")) : Context.Settings.ButtonTextPropertyName;
	if (CastField<FTextProperty>(FFigmaReflection::Find(ButtonBaseClass.Get(), TextProperty)))
	{
		Info.LabelProperty = TextProperty;
	}
	else if (!LabelWidget.IsNone() && AddLabelGraph(Blueprint, LabelWidget, TextProperty, Context))
	{
		FFigmaAssetUtils::CompileBlueprint(Blueprint, &Context.Log.Get());
		Info.LabelProperty = TextProperty;
	}
	Context.TouchPackage(Blueprint->GetPackage());
	return Blueprint;
}

bool FFigmaCommonUIMapper::AddLabelGraph(UWidgetBlueprint* Blueprint, FName LabelWidget, FName TextVariable, FFigmaImportContext& Context)
{
	for (const UEdGraph* Page : Blueprint->UbergraphPages)
	{
		if (!Page)
		{
			continue;
		}
		for (const UEdGraphNode* Node : Page->Nodes)
		{
			if (Node && Node->NodeComment.Contains(GraphMarker))
			{
				return true; // Wired on an earlier sync.
			}
		}
	}

	// New Widget Blueprints come with a disabled "ghost" PreConstruct node; that one (or any PreConstruct with
	// nothing connected yet) is used. Existing logic is never touched.
	UK2Node_Event* EventNode = FBlueprintEditorUtils::FindOverrideForFunction(Blueprint, UUserWidget::StaticClass(), TEXT("PreConstruct"));
	UEdGraphPin* ExistingThen = EventNode ? EventNode->FindPin(UEdGraphSchema_K2::PN_Then) : nullptr;
	if (EventNode && (!ExistingThen || ExistingThen->LinkedTo.Num() > 0))
	{
		Context.Log->Info(FString::Printf(TEXT("%s already has PreConstruct logic, so %s wasn't wired. Set the label text from your graph."), *Blueprint->GetName(), *TextVariable.ToString()));
		return false;
	}
	UEdGraph* EventGraph = EventNode ? EventNode->GetGraph() : FBlueprintEditorUtils::FindEventGraph(Blueprint);
	if (!EventGraph)
	{
		return false;
	}

	if (FBlueprintEditorUtils::FindNewVariableIndex(Blueprint, TextVariable) == INDEX_NONE)
	{
		FEdGraphPinType TextType;
		TextType.PinCategory = UEdGraphSchema_K2::PC_Text;
		if (!FBlueprintEditorUtils::AddMemberVariable(Blueprint, TextVariable, TextType))
		{
			return false;
		}
		FBlueprintEditorUtils::SetBlueprintOnlyEditableFlag(Blueprint, TextVariable, false);
		FBlueprintEditorUtils::SetBlueprintVariableMetaData(Blueprint, TextVariable, nullptr, FBlueprintMetadata::MD_ExposeOnSpawn, TEXT("true"));
		FBlueprintEditorUtils::SetBlueprintVariableCategory(Blueprint, TextVariable, nullptr, LOCTEXT("ButtonCategory", "Button"), true);
	}
	// The getters need the label widget variable and ButtonText on the skeleton class.
	FFigmaAssetUtils::CompileBlueprint(Blueprint, nullptr);

	auto Place = [EventGraph](UK2Node* Node, int32 X, int32 Y)
	{
		Node->NodePosX = X;
		Node->NodePosY = Y;
		EventGraph->AddNode(Node, false, false);
		Node->CreateNewGuid();
		Node->PostPlacedNewNode();
		Node->AllocateDefaultPins();
	};

	if (EventNode)
	{
		EventNode->Modify();
		if (!EventNode->IsNodeEnabled())
		{
			EventNode->SetEnabledState(ENodeEnabledState::Enabled, false);
		}
	}
	else
	{
		EventNode = NewObject<UK2Node_Event>(EventGraph);
		EventNode->EventReference.SetExternalMember(TEXT("PreConstruct"), UUserWidget::StaticClass());
		EventNode->bOverrideFunction = true;
		Place(EventNode, 0, -400);
	}
	const int32 BaseX = EventNode->NodePosX;
	const int32 BaseY = EventNode->NodePosY;

	UK2Node_VariableGet* GetLabel = NewObject<UK2Node_VariableGet>(EventGraph);
	GetLabel->VariableReference.SetSelfMember(LabelWidget);
	Place(GetLabel, BaseX + 48, BaseY + 160);

	UK2Node_VariableGet* GetText = NewObject<UK2Node_VariableGet>(EventGraph);
	GetText->VariableReference.SetSelfMember(TextVariable);
	Place(GetText, BaseX + 48, BaseY + 250);

	UK2Node_CallFunction* SetText = NewObject<UK2Node_CallFunction>(EventGraph);
	SetText->FunctionReference.SetExternalMember(GET_FUNCTION_NAME_CHECKED(UTextBlock, SetText), UTextBlock::StaticClass());
	Place(SetText, BaseX + 380, BaseY);

	const UEdGraphSchema_K2* Schema = GetDefault<UEdGraphSchema_K2>();
	UEdGraphPin* Then = EventNode->FindPin(UEdGraphSchema_K2::PN_Then);
	UEdGraphPin* Execute = SetText->GetExecPin();
	UEdGraphPin* Target = SetText->FindPin(UEdGraphSchema_K2::PN_Self);
	UEdGraphPin* InText = SetText->FindPin(TEXT("InText"));
	const bool bWired = Then && Execute && Target && InText && GetLabel->GetValuePin() && GetText->GetValuePin() &&
		Schema->TryCreateConnection(Then, Execute) &&
		Schema->TryCreateConnection(GetLabel->GetValuePin(), Target) &&
		Schema->TryCreateConnection(GetText->GetValuePin(), InText);
	if (bWired)
	{
		// Set after wiring: connecting a ghost node clears its comment. Also marks the graph for later syncs.
		EventNode->NodeComment = FString::Printf(TEXT("%s: copies %s into the label (runs in the designer too)"), GraphMarker, *TextVariable.ToString());
		EventNode->bCommentBubbleVisible = true;
	}
	else
	{
		Context.Log->Warning(FString::Printf(TEXT("%s: could not wire %s to the label automatically; connect PreConstruct > Set Text by hand."), *Blueprint->GetName(), *TextVariable.ToString()));
	}
	FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(Blueprint);
	return bWired;
}

// ---------------------------------------------------------------------------------------------------------------- Mapping

UClass* FFigmaCommonUIMapper::ChooseScreenParentClass(FFigmaImportContext& Context)
{
	if (!bAvailable || (Context.Document.Root.IsValid() && Context.Document.Root->HasTag(TEXT("widget"))))
	{
		return nullptr;
	}
	const FSoftClassPath Path = Context.Preset && Context.Preset->ScreenParentClass.IsValid() ? Context.Preset->ScreenParentClass : Context.Settings.ScreenParentClass;
	UClass* Class = FFigmaReflection::LoadClass(Path.ToString());
	return Class && Class->IsChildOf(UUserWidget::StaticClass()) ? Class : nullptr;
}

UClass* FFigmaCommonUIMapper::ChooseLeafClass(const FFigmaNode& Node, FFigmaImportContext& Context)
{
	if (!bAvailable || bBuildingButton)
	{
		return nullptr;
	}
	if (Context.IsButtonComponent(Node))
	{
		const FButtonSet* Set = FindSetForNode(Node);
		return Set && Set->WidgetClass.IsValid() ? Set->WidgetClass.Get() : nullptr;
	}
	if (ActionWidgetClass.IsValid() && Context.IsActionWidgetNode(Node))
	{
		return ActionWidgetClass.Get();
	}
	return nullptr;
}

UClass* FFigmaCommonUIMapper::ChooseTextClass(const FFigmaNode& Node, FFigmaImportContext& Context)
{
	if (!bAvailable || !TextBlockClass.IsValid())
	{
		return nullptr;
	}
	if (bBuildingButton && Node.Id == CurrentLabelNodeId)
	{
		return TextBlockClass.Get();
	}
	if (Context.Preset)
	{
		const FFigmaTextStyleOverride* Override = Context.Preset->FindTextStyle(Node.Text.TextStyle);
		if (Override && Override->CommonTextStyle.IsValid())
		{
			return TextBlockClass.Get();
		}
	}
	return nullptr;
}

bool FFigmaCommonUIMapper::BindInputAction(UObject* Widget, const FString& Action, bool bActionWidget, FFigmaImportContext& Context)
{
	if (UDataTable* Table = Cast<UDataTable>(Context.Settings.InputActionTable.TryLoad()))
	{
		const FName RowName(*Action);
		if (Table->GetRowMap().Contains(RowName))
		{
			FDataTableRowHandle Handle;
			Handle.DataTable = Table;
			Handle.RowName = RowName;
			return bActionWidget
				? FFigmaReflection::AddToStructArray(Widget, TEXT("InputActions"), FDataTableRowHandle::StaticStruct(), Handle, true)
				: FFigmaReflection::SetStruct(Widget, TEXT("TriggeringInputAction"), FDataTableRowHandle::StaticStruct(), Handle);
		}
	}
	if (UObject* InputAction = FindEnhancedInputAction(Context.Settings.EnhancedInputActionPrefix + Action))
	{
		return FFigmaReflection::SetObject(Widget, bActionWidget ? TEXT("EnhancedInputAction") : TEXT("TriggeringEnhancedInputAction"), InputAction);
	}
	return false;
}

void FFigmaCommonUIMapper::ConfigureWidget(UWidget* Widget, const FFigmaNode& Node, FFigmaImportContext& Context)
{
	if (!bAvailable || !Widget)
	{
		return;
	}
	UClass* WidgetClass = Widget->GetClass();

	if (TextBlockClass.IsValid() && WidgetClass->IsChildOf(TextBlockClass.Get()))
	{
		UClass* Style = bBuildingButton && Node.Id == CurrentLabelNodeId ? CurrentLabelStyle.Get() : nullptr;
		if (Context.Preset)
		{
			const FFigmaTextStyleOverride* Override = Context.Preset->FindTextStyle(Node.Text.TextStyle);
			if (Override && Override->CommonTextStyle.IsValid())
			{
				Style = Override->CommonTextStyle.TryLoadClass<UObject>();
			}
		}
		if (Style)
		{
			FFigmaReflection::SetClass(Widget, TEXT("Style"), Style);
		}
		return;
	}

	if (ActionWidgetClass.IsValid() && WidgetClass->IsChildOf(ActionWidgetClass.Get()))
	{
		const FString Action = Node.GetTag(TEXT("action"));
		if (Action.IsEmpty())
		{
			Context.UnboundActions.Add(FString::Printf(TEXT("%s (no [action=...] tag)"), *Widget->GetName()));
		}
		else if (!BindInputAction(Widget, Action, true, Context))
		{
			Context.UnboundActions.Add(FString::Printf(TEXT("%s: %s"), *Widget->GetName(), *Action));
		}
		return;
	}

	const FButtonSet* Set = FindSetForNode(Node);
	if (!Set || !Set->WidgetClass.IsValid() || !WidgetClass->IsChildOf(Set->WidgetClass.Get()))
	{
		return;
	}
	const FString Group = FFigmaButtonStates::GroupKey(Node.Component.Variant, Set->StateProperty);
	if (const TWeakObjectPtr<UClass>* Style = Set->StyleByGroup.Find(Group))
	{
		FFigmaReflection::SetClass(Widget, TEXT("Style"), Style->Get());
	}

	FString Label = Set->LabelComponentProperty.IsEmpty() ? FString() : Node.Component.TextProperties.FindRef(Set->LabelComponentProperty);
	if (Label.IsEmpty())
	{
		if (const FFigmaNode* LabelNode = FindLabelNode(Node, Set->LabelComponentProperty))
		{
			Label = LabelNode->Text.Characters;
		}
	}
	if (!Set->LabelProperty.IsNone() && !Label.IsEmpty())
	{
		FFigmaReflection::SetText(Widget, Set->LabelProperty, FText::FromString(Label));
	}

	const FString* StateValue = Set->StateProperty.IsEmpty() ? nullptr : Node.Component.Variant.Find(Set->StateProperty);
	if (StateValue && FFigmaButtonStates::Classify(*StateValue) == EFigmaButtonState::Disabled)
	{
		Widget->SetIsEnabled(false);
	}

	const FString Action = Node.GetTag(TEXT("action"));
	if (!Action.IsEmpty() && !BindInputAction(Widget, Action, false, Context))
	{
		Context.UnboundActions.Add(FString::Printf(TEXT("%s: %s"), *Widget->GetName(), *Action));
	}
}

void FFigmaCommonUIMapper::Lint(UWidgetBlueprint* Blueprint, FFigmaImportContext& Context)
{
	if (!bAvailable)
	{
		return;
	}
	for (const TPair<FString, FButtonSet>& Pair : Sets)
	{
		if (Pair.Value.bDerivedStates)
		{
			Context.Log->Lint(EFigmaLogSeverity::Info, FString::Printf(TEXT("'%s' had no exported variants (library component?), so hover/pressed/disabled were derived. Add its prefix to the exporter's button prefixes."), *Pair.Value.Name));
		}
		if (Pair.Value.LabelProperty.IsNone() && !Pair.Value.LabelComponentProperty.IsEmpty())
		{
			Context.Log->Lint(EFigmaLogSeverity::Warning, FString::Printf(TEXT("'%s': instance labels can't be applied (no text property on the button class). Labels show the component's default text."), *Pair.Value.Name));
		}
	}
}

#undef LOCTEXT_NAMESPACE
