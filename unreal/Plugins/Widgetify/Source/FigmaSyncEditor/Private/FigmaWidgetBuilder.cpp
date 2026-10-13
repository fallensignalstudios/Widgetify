#include "FigmaWidgetBuilder.h"
#include "Animation/WidgetAnimation.h"
#include "Blueprint/UserWidget.h"
#include "Blueprint/WidgetTree.h"
#include "Components/Border.h"
#include "Components/BorderSlot.h"
#include "Components/Button.h"
#include "Components/ButtonSlot.h"
#include "Components/CanvasPanel.h"
#include "Components/CanvasPanelSlot.h"
#include "Components/ContentWidget.h"
#include "Components/HorizontalBox.h"
#include "Components/HorizontalBoxSlot.h"
#include "Components/Image.h"
#include "Components/Overlay.h"
#include "Components/OverlaySlot.h"
#include "Components/PanelSlot.h"
#include "Components/PanelWidget.h"
#include "Components/SizeBox.h"
#include "Components/SizeBoxSlot.h"
#include "Components/Spacer.h"
#include "Components/TextBlock.h"
#include "Components/VerticalBox.h"
#include "Components/VerticalBoxSlot.h"
#include "Components/WrapBox.h"
#include "Components/WrapBoxSlot.h"
#include "EdGraph/EdGraph.h"
#include "Engine/Texture2D.h"
#include "FigmaButtonStates.h"
#include "FigmaFontResolver.h"
#include "FigmaImportContext.h"
#include "FigmaNaming.h"
#include "FigmaReflection.h"
#include "FigmaStyleConverter.h"
#include "FigmaSyncBlueprintExtension.h"
#include "FigmaSyncSettings.h"
#include "FigmaSyncStylePreset.h"
#include "FigmaTextureImporter.h"
#include "FigmaWidgetMapper.h"
#include "Framework/Text/TextLayout.h"
#include "Kismet2/BlueprintEditorUtils.h"
#include "Misc/EngineVersionComparison.h"
#include "Styling/SlateTypes.h"
#include "UObject/Package.h"
#include "UObject/UnrealType.h"
#include "WidgetBlueprint.h"

namespace FigmaRoles
{
	static FName Size() { static const FName Name(TEXT("Size")); return Name; }
	static FName Layout() { static const FName Name(TEXT("Layout")); return Name; }
	static FName Stack() { static const FName Name(TEXT("Stack")); return Name; }
	static FName Bg() { static const FName Name(TEXT("Bg")); return Name; }
	static FName Flow() { static const FName Name(TEXT("Flow")); return Name; }
	static FName Abs() { static const FName Name(TEXT("Abs")); return Name; }
	static FName Spacer(int32 Index) { return FName(*FString::Printf(TEXT("Spacer%d"), Index)); }
}

namespace
{
	/** Figma thinks in pixels with "auto" line height ~1.2 x font size for most fonts; UMG scales the font's own line height. */
	constexpr float NaturalLineHeightRatio = 1.2f;

	bool AnyAutoChildFillsPrimary(const FFigmaNode& Node)
	{
		if (!Node.IsAutoLayout())
		{
			return false;
		}
		const bool bHorizontal = Node.Layout.Mode == EFigmaLayoutMode::Horizontal;
		for (const TSharedPtr<FFigmaNode>& Child : Node.Children)
		{
			if (!Child.IsValid() || Child->Child.bAbsolute)
			{
				continue;
			}
			const EFigmaSizing Primary = bHorizontal ? Child->Child.SizingH : Child->Child.SizingV;
			if (Primary == EFigmaSizing::Fill || Child->Child.Grow > 0.f)
			{
				return true;
			}
		}
		return false;
	}

	bool HasAbsoluteChildren(const FFigmaNode& Node)
	{
		if (!Node.IsAutoLayout())
		{
			return false;
		}
		for (const TSharedPtr<FFigmaNode>& Child : Node.Children)
		{
			if (Child.IsValid() && Child->Child.bAbsolute)
			{
				return true;
			}
		}
		return false;
	}

	bool BackgroundOverflows(const FFigmaNode& Node)
	{
		const FFigmaRect& B = Node.Background.Bounds;
		constexpr float Tolerance = 0.5f;
		return B.X < -Tolerance || B.Y < -Tolerance || B.X + B.Width > Node.Width + Tolerance || B.Y + B.Height > Node.Height + Tolerance;
	}

	EVerticalAlignment CounterToVAlign(EFigmaAlign Align)
	{
		switch (Align)
		{
		case EFigmaAlign::Center: return VAlign_Center;
		case EFigmaAlign::Max: return VAlign_Bottom;
		case EFigmaAlign::Baseline: return VAlign_Center;
		default: return VAlign_Top;
		}
	}

	EHorizontalAlignment CounterToHAlign(EFigmaAlign Align)
	{
		switch (Align)
		{
		case EFigmaAlign::Center: return HAlign_Center;
		case EFigmaAlign::Max: return HAlign_Right;
		case EFigmaAlign::Baseline: return HAlign_Center;
		default: return HAlign_Left;
		}
	}

	void CollectIds(const FFigmaNode& Node, TSet<FString>& OutIds)
	{
		OutIds.Add(Node.Id);
		for (const TSharedPtr<FFigmaNode>& Child : Node.Children)
		{
			if (Child.IsValid())
			{
				CollectIds(*Child, OutIds);
			}
		}
	}

	int32 DepthOf(const UWidget* Widget)
	{
		// Capped: a hand-edited hierarchy can be transiently cyclic mid-sync.
		constexpr int32 MaxDepth = 1024;
		int32 Depth = 0;
		for (const UWidget* Current = Widget; Current && Current->GetParent() && Depth < MaxDepth; Current = Current->GetParent())
		{
			++Depth;
		}
		return Depth;
	}

	/** Records everything a re-parent touches (the child, its old slot and old parent) so undo restores it. */
	void ModifyForReparent(UWidget* Child)
	{
		if (!Child)
		{
			return;
		}
		Child->Modify();
		if (UPanelSlot* OldSlot = Child->Slot)
		{
			OldSlot->Modify();
			if (UPanelWidget* OldParent = OldSlot->Parent)
			{
				OldParent->Modify();
			}
		}
	}

	FString ClassDisplayName(const UClass* Class)
	{
		if (!Class)
		{
			return TEXT("?");
		}
		FString Name = Class->GetName();
		Name.RemoveFromEnd(TEXT("_C"));
		return Name;
	}
}

FFigmaWidgetBuilder::FFigmaWidgetBuilder(FFigmaImportContext& InContext, UWidgetBlueprint* InBlueprint, UFigmaSyncBlueprintExtension* InState, const FFigmaBuildOptions& InOptions)
	: Context(InContext)
	, Blueprint(InBlueprint)
	, Tree(InBlueprint ? InBlueprint->WidgetTree.Get() : nullptr)
	, State(InState)
	, Options(InOptions)
{
}

// ----------------------------------------------------------------------------------------------------------------
// Classification and planning

EFigmaWidgetKind FFigmaWidgetBuilder::Classify(const FFigmaNode& Node, FFigmaImportContext& InContext, UClass** OutLeafClass, IFigmaWidgetMapper** OutMapper)
{
	if (Node.IsRasterized())
	{
		return EFigmaWidgetKind::Image;
	}
	for (const TSharedRef<IFigmaWidgetMapper>& Mapper : InContext.Mappers)
	{
		if (UClass* LeafClass = Mapper->ChooseLeafClass(Node, InContext))
		{
			if (OutLeafClass)
			{
				*OutLeafClass = LeafClass;
			}
			if (OutMapper)
			{
				*OutMapper = &Mapper.Get();
			}
			return EFigmaWidgetKind::Leaf;
		}
	}
	if (Node.IsText())
	{
		return EFigmaWidgetKind::Text;
	}
	if (InContext.IsButtonNode(Node))
	{
		return EFigmaWidgetKind::Button;
	}
	if (Node.IsContainer())
	{
		return EFigmaWidgetKind::Container;
	}
	return EFigmaWidgetKind::Shape;
}

UClass* FFigmaWidgetBuilder::GetPanelClass(const FFigmaNode& Node)
{
	if (Node.HasTag(TEXT("overlay")))
	{
		return UOverlay::StaticClass();
	}
	if (Node.HasTag(TEXT("canvas")) || !Node.IsAutoLayout())
	{
		return UCanvasPanel::StaticClass();
	}
	if (Node.Layout.bWrap)
	{
		return UWrapBox::StaticClass();
	}
	return Node.Layout.Mode == EFigmaLayoutMode::Horizontal ? UHorizontalBox::StaticClass() : UVerticalBox::StaticClass();
}

FFigmaWidgetBuilder::FPlan FFigmaWidgetBuilder::MakePlan(const FFigmaNode& Node, const FParentInfo& Parent, bool bIsRoot)
{
	FPlan Plan;
	UClass* LeafClass = nullptr;
	IFigmaWidgetMapper* Mapper = nullptr;
	Plan.Kind = Classify(Node, Context, &LeafClass, &Mapper);
	Plan.Mapper = Mapper;

	const bool bComponentRoot = bIsRoot && Options.bComponentContent;
	switch (Plan.Kind)
	{
	case EFigmaWidgetKind::Image:
	case EFigmaWidgetKind::Shape:
		Plan.NamedClass = UImage::StaticClass();
		break;

	case EFigmaWidgetKind::Text:
	{
		Plan.NamedClass = UTextBlock::StaticClass();
		for (const TSharedRef<IFigmaWidgetMapper>& Candidate : Context.Mappers)
		{
			UClass* TextClass = Candidate->ChooseTextClass(Node, Context);
			if (TextClass && TextClass->IsChildOf(UTextBlock::StaticClass()))
			{
				Plan.NamedClass = TextClass;
				Plan.Mapper = &Candidate.Get();
				break;
			}
		}
		break;
	}

	case EFigmaWidgetKind::Leaf:
		Plan.NamedClass = LeafClass;
		break;

	case EFigmaWidgetKind::Button:
		Plan.NamedClass = UButton::StaticClass();
		Plan.PanelClass = GetPanelClass(Node);
		Plan.bFoldPadding = false; // the button style carries the padding
		break;

	case EFigmaWidgetKind::Container:
	{
		Plan.PanelClass = GetPanelClass(Node);
		Plan.bAbsLayer = HasAbsoluteChildren(Node) && !Node.HasTag(TEXT("overlay")) && !Node.HasTag(TEXT("canvas"));
		if (Plan.bAbsLayer)
		{
			Plan.FlowClass = Plan.PanelClass;
			Plan.PanelClass = UOverlay::StaticClass();
		}
		const bool bHasBox = !bComponentRoot && FFigmaStyleConverter::HasVisibleBox(Node);
		const bool bHasRasterBg = !bComponentRoot && Node.Background.IsSet();
		const bool bWrapPadding = Node.IsAutoLayout() && Node.Layout.bWrap && !Node.Layout.Padding.IsZero();
		Plan.bBgOverlay = bHasRasterBg && BackgroundOverflows(Node);
		Plan.bBgBrush = bHasRasterBg && !Plan.bBgOverlay;
		Plan.bBorder = bHasBox || bHasRasterBg || bWrapPadding;
		break;
	}
	}

	ComputeSizing(Node, Parent, bIsRoot, Plan);

	if (Plan.Kind == EFigmaWidgetKind::Container)
	{
		// Centered/right-aligned auto layout needs something to align the hugging box inside the frame.
		EHorizontalAlignment H = HAlign_Fill;
		EVerticalAlignment V = VAlign_Fill;
		GetContentAlignment(Node, H, V);
		const bool bNeedsAligner = H != HAlign_Fill || V != VAlign_Fill;
		if (bNeedsAligner && !Plan.bBorder && !Plan.bSizeBox)
		{
			Plan.bBorder = true;
		}
		Plan.bFoldPadding = Node.IsAutoLayout() && !Plan.bBorder && !Plan.bAbsLayer && !Node.Layout.bWrap && !bComponentRoot;
		Plan.NamedClass = Plan.bBorder ? UBorder::StaticClass() : Plan.PanelClass;
	}
	return Plan;
}

void FFigmaWidgetBuilder::ComputeSizing(const FFigmaNode& Node, const FParentInfo& Parent, bool bIsRoot, FPlan& Plan) const
{
	const bool bIntrinsic = Plan.Kind == EFigmaWidgetKind::Image || Plan.Kind == EFigmaWidgetKind::Shape || Plan.Kind == EFigmaWidgetKind::Text;
	bool bW = false;
	bool bH = false;
	bool bNeedBox = false;

	switch (Parent.Kind)
	{
	case EParentKind::HBox:
	case EParentKind::VBox:
	case EParentKind::Wrap:
		if (Node.bHasChildLayout && !Node.Child.bAbsolute && !bIntrinsic)
		{
			bW = Node.Child.SizingH == EFigmaSizing::Fixed;
			bH = Node.Child.SizingV == EFigmaSizing::Fixed;
		}
		break;
	case EParentKind::Overlay:
		if (!bIntrinsic)
		{
			bW = Node.ConstraintH != EFigmaConstraint::Stretch && Node.ConstraintH != EFigmaConstraint::Scale;
			bH = Node.ConstraintV != EFigmaConstraint::Stretch && Node.ConstraintV != EFigmaConstraint::Scale;
		}
		break;
	case EParentKind::Root:
		if (bIsRoot && !Options.bComponentContent)
		{
			const FIntPoint Design = Context.Settings.DesignResolution;
			const bool bFullScreen = Node.Width >= Design.X * 0.9f && Node.Height >= Design.Y * 0.9f;
			bW = bH = !bFullScreen;
		}
		break;
	default:
		break;
	}

	if (Plan.Kind == EFigmaWidgetKind::Text)
	{
		const FFigmaText& Text = Node.Text;
		const bool bFixedHeight = Text.AutoResize == EFigmaTextAutoResize::None || Text.AutoResize == EFigmaTextAutoResize::Truncate;
		const bool bInBox = Parent.Kind == EParentKind::HBox || Parent.Kind == EParentKind::VBox || Parent.Kind == EParentKind::Wrap;
		if (bInBox && Node.Child.SizingH == EFigmaSizing::Fixed && Text.bTruncate)
		{
			bW = true;
		}
		if (bFixedHeight && Text.AlignV != EFigmaTextAlignV::Top)
		{
			Plan.SizeVAlign = Text.AlignV == EFigmaTextAlignV::Center ? VAlign_Center : VAlign_Bottom;
			if (bInBox && Node.Child.SizingV == EFigmaSizing::Fixed)
			{
				bH = true;
			}
			// In a canvas the slot sizes the box; the SizeBox only aligns the text inside it.
			bNeedBox = bNeedBox || Parent.Kind == EParentKind::Canvas || bH;
		}
	}

	Plan.bSizeW = bW;
	Plan.bSizeH = bH;
	Plan.bSizeBox = bW || bH || bNeedBox || Node.SizeLimits.IsSet();
}

void FFigmaWidgetBuilder::GetContentAlignment(const FFigmaNode& Node, EHorizontalAlignment& OutH, EVerticalAlignment& OutV) const
{
	OutH = HAlign_Fill;
	OutV = VAlign_Fill;
	if (!Node.IsAutoLayout() || Node.Layout.bWrap || AnyAutoChildFillsPrimary(Node))
	{
		return;
	}
	const EFigmaAlign Primary = Node.Layout.PrimaryAlign;
	if (Primary == EFigmaAlign::Min || Primary == EFigmaAlign::SpaceBetween)
	{
		return;
	}
	if (Node.Layout.Mode == EFigmaLayoutMode::Horizontal)
	{
		OutH = Primary == EFigmaAlign::Center ? HAlign_Center : HAlign_Right;
	}
	else
	{
		OutV = Primary == EFigmaAlign::Center ? VAlign_Center : VAlign_Bottom;
	}
}

FFigmaWidgetBuilder::FLocks FFigmaWidgetBuilder::GetLocks(const FFigmaSyncNodeRecord& Record) const
{
	FLocks Locks;
	if (const FFigmaSyncLockRule* Rule = State ? State->FindLock(Record.Widget) : nullptr)
	{
		Locks.bLayout = Rule->bLockLayout;
		Locks.bAppearance = Rule->bLockAppearance;
		Locks.bText = Rule->bLockText;
		Locks.bChildren = Rule->bLockChildren;
	}
	return Locks;
}

FString FFigmaWidgetBuilder::Describe(const FFigmaNode& Node, FFigmaImportContext& InContext)
{
	FFigmaWidgetBuilder Planner(InContext, nullptr, nullptr);
	FParentInfo Parent;
	if (Node.Parent)
	{
		Parent.Node = Node.Parent;
		Parent.Kind = Node.Parent->IsAutoLayout() ? (Node.Parent->Layout.Mode == EFigmaLayoutMode::Horizontal ? EParentKind::HBox : EParentKind::VBox) : EParentKind::Canvas;
	}
	const FPlan Plan = Planner.MakePlan(Node, Parent, Node.Parent == nullptr);

	FString Out;
	switch (Plan.Kind)
	{
	case EFigmaWidgetKind::Image:
		Out = FString::Printf(TEXT("Image (PNG, %s)"), *Node.Image.Reason);
		break;
	case EFigmaWidgetKind::Shape:
		Out = Node.Type == EFigmaNodeType::Ellipse ? TEXT("Image (circle brush)") : TEXT("Image (rounded-box brush)");
		break;
	case EFigmaWidgetKind::Text:
		Out = ClassDisplayName(Plan.NamedClass);
		break;
	case EFigmaWidgetKind::Leaf:
		Out = ClassDisplayName(Plan.NamedClass);
		break;
	case EFigmaWidgetKind::Button:
		Out = Node.Children.Num() > 0 ? FString::Printf(TEXT("Button + %s"), *ClassDisplayName(Plan.PanelClass)) : TEXT("Button");
		break;
	case EFigmaWidgetKind::Container:
		Out = Plan.bBorder ? FString::Printf(TEXT("Border + %s"), *ClassDisplayName(Plan.bAbsLayer ? Plan.FlowClass : Plan.PanelClass))
			: ClassDisplayName(Plan.bAbsLayer ? Plan.FlowClass : Plan.PanelClass);
		if (Plan.bAbsLayer)
		{
			Out += TEXT(" + CanvasPanel");
		}
		if (Plan.bBgBrush || Plan.bBgOverlay)
		{
			Out += TEXT(" (PNG background)");
		}
		break;
	}
	if (Plan.bSizeBox)
	{
		Out = TEXT("SizeBox > ") + Out;
	}
	return Out;
}

// ----------------------------------------------------------------------------------------------------------------
// Build

bool FFigmaWidgetBuilder::Build(const FFigmaNode& Root)
{
	if (!Blueprint || !Tree)
	{
		Context.Log->Error(TEXT("The Widget Blueprint has no widget tree."));
		return false;
	}

	ManagedBefore = State ? State->GetManagedWidgetNames() : TSet<FName>();
	TSet<FString> CurrentIds;
	CollectIds(Root, CurrentIds);
	// BuildNode, BuildContainer and BuildChildren hold FFigmaSyncNodeRecord& into NewRecords while recursing into
	// children, which add records. One slot per node up front means those adds never reallocate under them.
	NewRecords.Reserve(CurrentIds.Num());
	if (State)
	{
		for (const TPair<FString, FFigmaSyncNodeRecord>& Pair : State->Nodes)
		{
			if (!CurrentIds.Contains(Pair.Key))
			{
				RemovedNames.Add(Pair.Value.Widget);
				for (const TPair<FName, FName>& Helper : Pair.Value.Helpers)
				{
					RemovedNames.Add(Helper.Value);
				}
			}
		}
		State->Modify();
	}
	Blueprint->Modify();
	Tree->Modify();

	FParentInfo RootParent;
	UWidget* RootOuter = BuildNode(Root, RootParent, true);
	if (!RootOuter)
	{
		Context.Log->Error(TEXT("Could not build the root widget."), &Root);
		return false;
	}

	UWidget* CurrentRoot = Tree->RootWidget;
	const bool bRootIsOurs = !CurrentRoot || CurrentRoot == RootOuter || IsKnownManaged(CurrentRoot->GetFName());
	if (bRootIsOurs)
	{
		if (CurrentRoot != RootOuter)
		{
			ModifyForReparent(RootOuter);
			RootOuter->RemoveFromParent();
			Tree->RootWidget = RootOuter;
		}
	}
	else if (!RootOuter->GetParent())
	{
		UPanelWidget* UserRoot = Cast<UPanelWidget>(CurrentRoot);
		if (UserRoot && UserRoot->CanAddMoreChildren())
		{
			ModifyForReparent(RootOuter);
			UserRoot->Modify();
			UserRoot->AddChild(RootOuter);
			Context.Log->Info(TEXT("The root widget was replaced by hand, so the Figma screen was added inside it."));
		}
		else
		{
			Context.Log->Warning(TEXT("The root widget was replaced by hand and can't hold the Figma screen. Make the Figma widget the root again to keep syncing."));
		}
	}

	HandleRemovedNodes(CurrentIds);
	DeletePending();
	SyncVariableGuids();

	if (State)
	{
		State->Nodes = MoveTemp(NewRecords);
	}
	FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(Blueprint);
	return true;
}

FFigmaSyncNodeRecord& FFigmaWidgetBuilder::RecordFor(const FFigmaNode& Node)
{
	if (FFigmaSyncNodeRecord* Existing = NewRecords.Find(Node.Id))
	{
		return *Existing;
	}
	FFigmaSyncNodeRecord Record;
	if (State)
	{
		if (const FFigmaSyncNodeRecord* Old = State->Nodes.Find(Node.Id))
		{
			Record = *Old;
		}
	}
	Record.bRemovedInFigma = false;
	Record.LayerName = Node.CleanName;
	Record.ParentId = Node.Parent ? Node.Parent->Id : FString();
	return NewRecords.Add(Node.Id, Record);
}

UWidget* FFigmaWidgetBuilder::BuildNode(const FFigmaNode& Node, const FParentInfo& Parent, bool bIsRoot)
{
	FFigmaSyncNodeRecord& Record = RecordFor(Node);
	const FLocks Locks = GetLocks(Record);
	const FPlan Plan = MakePlan(Node, Parent, bIsRoot);
	if (!Plan.NamedClass)
	{
		Context.Log->Error(TEXT("No widget class for this layer."), &Node);
		return nullptr;
	}

	bool bCreated = false;
	UWidget* Named = EnsureNamedWidget(Node, Record, Plan, bCreated);
	if (!Named)
	{
		Context.Log->Error(FString::Printf(TEXT("Could not create a %s."), *ClassDisplayName(Plan.NamedClass)), &Node);
		return nullptr;
	}
	Record.Widget = Named->GetFName();
	Record.WidgetClass = FSoftClassPath(Named->GetClass());
	Context.BuiltWidgets.Add(Node.Id, Named->GetFName());

	// Never un-tick "Is Variable" on a widget someone exposed by hand.
	if (bCreated)
	{
		Named->bIsVariable = Context.ShouldBeVariable(Node);
	}
	else
	{
		Named->Modify();
		Context.Stats.WidgetsUpdated++;
		if (Context.ShouldBeVariable(Node))
		{
			Named->bIsVariable = true;
		}
	}

	switch (Plan.Kind)
	{
	case EFigmaWidgetKind::Image:
	case EFigmaWidgetKind::Shape:
	case EFigmaWidgetKind::Text:
	case EFigmaWidgetKind::Leaf:
		for (const FName Role : { FigmaRoles::Layout(), FigmaRoles::Stack(), FigmaRoles::Bg(), FigmaRoles::Flow(), FigmaRoles::Abs() })
		{
			RetireHelper(Record, Role);
		}
		for (int32 SpacerIndex = 1; Record.Helpers.Contains(FigmaRoles::Spacer(SpacerIndex)); ++SpacerIndex)
		{
			RetireHelper(Record, FigmaRoles::Spacer(SpacerIndex));
		}
		// Anything that used to be built under this layer is now part of it (an image, a component...).
		ChildlessIds.Add(Node.Id);
		if (Plan.Kind == EFigmaWidgetKind::Image && !Locks.bAppearance)
		{
			ConfigureImage(Named, Node);
		}
		else if (Plan.Kind == EFigmaWidgetKind::Shape && !Locks.bAppearance)
		{
			ConfigureShape(Named, Node);
		}
		else if (Plan.Kind == EFigmaWidgetKind::Text)
		{
			ConfigureText(Named, Node, Parent, Locks);
		}
		if (Plan.Mapper && !Locks.bAppearance)
		{
			Plan.Mapper->ConfigureWidget(Named, Node, Context);
		}
		break;
	case EFigmaWidgetKind::Button:
		BuildButton(Node, Record, Plan, CastChecked<UButton>(Named), Locks);
		break;
	case EFigmaWidgetKind::Container:
		BuildContainer(Node, Record, Plan, Named, Locks);
		break;
	}

	UWidget* Outer = Named;
	if (Plan.bSizeBox)
	{
		UWidget* SizeBox = EnsureHelper(Record, FigmaRoles::Size(), Named->GetName() + TEXT("_Size"), USizeBox::StaticClass());
		SetSingleContent(Cast<UPanelWidget>(SizeBox), Named);
		if (SizeBox && !Locks.bLayout)
		{
			ConfigureSizeBox(SizeBox, Node, Plan);
		}
		Outer = SizeBox ? SizeBox : Named;
	}
	else
	{
		RetireHelper(Record, FigmaRoles::Size());
	}

	ApplyCommon(Outer, Named, Node, Plan, Locks);
	if (Node.HasTag(TEXT("focus")))
	{
		Context.FocusWidgetName = Named->GetFName();
	}
	return Outer;
}

// ----------------------------------------------------------------------------------------------------------------
// Widget lookup, creation, naming

UWidget* FFigmaWidgetBuilder::FindWidgetByName(FName Name) const
{
	// FindObject also finds widgets that are temporarily detached mid-sync (UWidgetTree::FindWidget only walks from the root).
	return Name.IsNone() || !Tree ? nullptr : FindObject<UWidget>(Tree, *Name.ToString());
}

bool FFigmaWidgetBuilder::IsKnownManaged(FName Name) const
{
	return ManagedBefore.Contains(Name) || NamesTaken.Contains(Name);
}

bool FFigmaWidgetBuilder::IsNameFree(FName Name) const
{
	if (Name.IsNone() || ReservedNames.Contains(Name) || !Tree || !Blueprint)
	{
		return false;
	}
	if (FindObject<UObject>(Tree, *Name.ToString()))
	{
		return false;
	}
	if (FBlueprintEditorUtils::FindNewVariableIndex(Blueprint, Name) != INDEX_NONE)
	{
		return false;
	}
	if (const UClass* ParentClass = Blueprint->ParentClass)
	{
		if (FindFProperty<FProperty>(ParentClass, Name) || ParentClass->FindFunctionByName(Name))
		{
			return false;
		}
	}
	for (const UEdGraph* Graph : Blueprint->FunctionGraphs)
	{
		if (Graph && Graph->GetFName() == Name)
		{
			return false;
		}
	}
	for (const UWidgetAnimation* Animation : Blueprint->Animations)
	{
		if (Animation && Animation->GetFName() == Name)
		{
			return false;
		}
	}
	return true;
}

FName FFigmaWidgetBuilder::ClaimName(const FString& Desired)
{
	const FString Base = FFigmaNaming::SanitizeIdentifier(Desired);
	const FString Unique = FFigmaNaming::MakeUnique(Base, [this](const FString& Candidate) { return !IsNameFree(FName(*Candidate)); });
	const FName Name(*Unique);
	ReservedNames.Add(Name);
	return Name;
}

UWidget* FFigmaWidgetBuilder::ConstructWidgetOfClass(UClass* Class, FName Name)
{
	if (!Class || !Tree)
	{
		return nullptr;
	}
	UWidget* Widget = nullptr;
	if (Class->IsChildOf(UUserWidget::StaticClass()))
	{
		// UE 5.4+ picks CreateWidget vs NewObject from the template type, so user widgets must use UUserWidget.
		Widget = Tree->ConstructWidget<UUserWidget>(Class, Name);
	}
	else
	{
		Widget = Tree->ConstructWidget<UWidget>(Class, Name);
	}
	if (Widget)
	{
		Widget->SetFlags(RF_Transactional);
		NamesTaken.Add(Name);
		ReservedNames.Add(Name);
		Context.Stats.WidgetsCreated++;
	}
	return Widget;
}

void FFigmaWidgetBuilder::RenameWidget(UWidget* Widget, FName NewName)
{
	if (!Widget || Widget->GetFName() == NewName)
	{
		return;
	}
	const FName OldName = Widget->GetFName();
	Widget->Modify();
	Widget->Rename(*NewName.ToString(), nullptr, REN_DontCreateRedirectors);
	NamesFreed.Add(OldName);
	NamesTaken.Add(NewName);
	ReservedNames.Add(NewName);
}

void FFigmaWidgetBuilder::MarkForDelete(UWidget* Widget)
{
	if (Widget && !PendingDelete.Contains(Widget))
	{
		PendingDelete.Add(Widget);
	}
}

UWidget* FFigmaWidgetBuilder::TryRelink(const FFigmaNode& Node, FFigmaSyncNodeRecord& Record)
{
	// The recorded widget is gone. If someone renamed it in Unreal, it's still under the parent's panel.
	UClass* RecordedClass = Record.WidgetClass.ResolveClass();
	const FFigmaSyncNodeRecord* ParentRecord = Node.Parent ? NewRecords.Find(Node.Parent->Id) : nullptr;
	if (!RecordedClass || !ParentRecord)
	{
		return nullptr;
	}
	TArray<UPanelWidget*> Panels;
	for (const FName Role : { FigmaRoles::Layout(), FigmaRoles::Flow(), FigmaRoles::Abs() })
	{
		if (UPanelWidget* Panel = Cast<UPanelWidget>(FindWidgetByName(ParentRecord->Helpers.FindRef(Role))))
		{
			Panels.Add(Panel);
		}
	}
	if (UPanelWidget* ParentPanel = Cast<UPanelWidget>(FindWidgetByName(ParentRecord->Widget)))
	{
		Panels.Add(ParentPanel);
	}

	UWidget* Candidate = nullptr;
	int32 Matches = 0;
	for (UPanelWidget* Panel : Panels)
	{
		for (UWidget* Child : Panel->GetAllChildren())
		{
			// Look through our own SizeBox wrappers too.
			UWidget* Inner = Child;
			if (USizeBox* SizeBox = Cast<USizeBox>(Child))
			{
				Inner = SizeBox->GetContent() ? SizeBox->GetContent() : Child;
			}
			if (Inner && Inner->GetClass() == RecordedClass && !IsKnownManaged(Inner->GetFName()))
			{
				Candidate = Inner;
				++Matches;
			}
		}
	}
	if (Matches == 1 && Candidate)
	{
		Context.Log->Info(FString::Printf(TEXT("Relinked to '%s' (the widget was renamed in Unreal from '%s')."), *Candidate->GetName(), *Record.Widget.ToString()), &Node);
		ReservedNames.Add(Candidate->GetFName());
		NamesTaken.Add(Candidate->GetFName());
		return Candidate;
	}
	return nullptr;
}

UWidget* FFigmaWidgetBuilder::EnsureNamedWidget(const FFigmaNode& Node, FFigmaSyncNodeRecord& Record, const FPlan& Plan, bool& bOutCreated)
{
	bOutCreated = false;
	UWidget* Existing = FindWidgetByName(Record.Widget);
	if (!Existing && !Record.Widget.IsNone())
	{
		Existing = TryRelink(Node, Record);
		if (!Existing)
		{
			Context.Log->Info(FString::Printf(TEXT("Widget '%s' was deleted in Unreal; it was recreated from Figma. Add it to the lock list to keep it gone."), *Record.Widget.ToString()), &Node);
		}
	}

	if (Existing && Existing->GetClass() == Plan.NamedClass)
	{
		ReservedNames.Add(Existing->GetFName());
		return Existing;
	}

	FName NameToUse = NAME_None;
	if (Existing)
	{
		const FName OldName = Existing->GetFName();
		UWidget* OldLayout = FindWidgetByName(Record.Helpers.FindRef(FigmaRoles::Layout()));
		if (OldLayout && OldLayout->GetClass() == Plan.NamedClass)
		{
			// The frame lost its Border (e.g. its fill was removed): its layout panel takes over the name.
			RenameWidget(Existing, ClaimName(OldName.ToString() + TEXT("_Replaced")));
			MarkForDelete(Existing);
			RenameWidget(OldLayout, OldName);
			Record.Helpers.Remove(FigmaRoles::Layout());
			ModifyForReparent(OldLayout);
			OldLayout->RemoveFromParent();
			ReservedNames.Add(OldName);
			return OldLayout;
		}

		if (Plan.PanelClass && Existing->GetClass() == Plan.PanelClass && Plan.bBorder && !OldLayout)
		{
			// The frame gained a Border (e.g. a fill was added): the old panel becomes its layout panel.
			const FName LayoutName = ClaimName(OldName.ToString() + TEXT("_Layout"));
			RenameWidget(Existing, LayoutName);
			Record.Helpers.Add(FigmaRoles::Layout(), LayoutName);
		}
		else
		{
			RenameWidget(Existing, ClaimName(OldName.ToString() + TEXT("_Replaced")));
			MarkForDelete(Existing);
			ReplacedWidgets.Add(Node.Id, Existing);
		}
		NameToUse = OldName;
		Context.Log->Warning(FString::Printf(TEXT("'%s' changed from %s to %s. Blueprint references to it now point at the new widget; recompile and check the graph."),
			*OldName.ToString(), *ClassDisplayName(Existing->GetClass()), *ClassDisplayName(Plan.NamedClass)), &Node);
	}

	if (NameToUse.IsNone())
	{
		const bool bComponentRoot = Options.bComponentContent && Node.Parent == nullptr;
		NameToUse = ClaimName(bComponentRoot ? FString(TEXT("Content")) : Node.CleanName);
	}
	else
	{
		ReservedNames.Add(NameToUse);
	}

	UWidget* Created = ConstructWidgetOfClass(Plan.NamedClass, NameToUse);
	bOutCreated = Created != nullptr;
	return Created;
}

UWidget* FFigmaWidgetBuilder::EnsureHelper(FFigmaSyncNodeRecord& Record, FName Role, const FString& DesiredName, UClass* Class)
{
	if (!Class)
	{
		return nullptr;
	}
	const FName OldName = Record.Helpers.FindRef(Role);
	UWidget* Existing = FindWidgetByName(OldName);
	if (Existing && Existing->GetClass() == Class)
	{
		ReservedNames.Add(OldName);
		Existing->Modify();
		return Existing;
	}

	if (Existing)
	{
		// Same role, different class: free the name first so the new helper gets it (not "_Layout_2" forever).
		RenameWidget(Existing, ClaimName(OldName.ToString() + TEXT("_Replaced")));
	}
	UWidget* Created = ConstructWidgetOfClass(Class, ClaimName(DesiredName));
	if (!Created)
	{
		return nullptr;
	}
	Created->bIsVariable = false;
	Created->SetVisibility(Cast<UPanelWidget>(Created) ? ESlateVisibility::SelfHitTestInvisible : ESlateVisibility::HitTestInvisible);

	if (Existing)
	{
		// Same role, different class (e.g. the layout switched from canvas to vertical box): carry the children over.
		UPanelWidget* OldPanel = Cast<UPanelWidget>(Existing);
		UPanelWidget* NewPanel = Cast<UPanelWidget>(Created);
		if (OldPanel && NewPanel)
		{
			for (UWidget* Child : OldPanel->GetAllChildren())
			{
				if (!NewPanel->CanAddMoreChildren())
				{
					break;
				}
				ModifyForReparent(Child);
				NewPanel->AddChild(Child);
			}
		}
		MarkForDelete(Existing);
	}
	Record.Helpers.Add(Role, Created->GetFName());
	return Created;
}

void FFigmaWidgetBuilder::RetireHelper(FFigmaSyncNodeRecord& Record, FName Role)
{
	const FName Name = Record.Helpers.FindRef(Role);
	if (Name.IsNone())
	{
		return;
	}
	if (UWidget* Widget = FindWidgetByName(Name))
	{
		MarkForDelete(Widget);
	}
	Record.Helpers.Remove(Role);
}

void FFigmaWidgetBuilder::TransferLeftovers(const FFigmaNode& Node, UPanelWidget* Target)
{
	const TWeakObjectPtr<UWidget>* Replaced = ReplacedWidgets.Find(Node.Id);
	UPanelWidget* OldPanel = Replaced ? Cast<UPanelWidget>(Replaced->Get()) : nullptr;
	if (!OldPanel || !Target)
	{
		return;
	}
	for (UWidget* Child : OldPanel->GetAllChildren())
	{
		if (Child && !IsKnownManaged(Child->GetFName()) && Target->CanAddMoreChildren())
		{
			ModifyForReparent(Child);
			Target->Modify();
			Target->AddChild(Child);
			Context.Log->Info(FString::Printf(TEXT("Moved hand-added widget '%s' into the new %s."), *Child->GetName(), *ClassDisplayName(Target->GetClass())), &Node);
		}
	}
}

// ----------------------------------------------------------------------------------------------------------------
// Structure

void FFigmaWidgetBuilder::SetSingleContent(UPanelWidget* Container, UWidget* Child)
{
	if (!Container)
	{
		return;
	}
	UContentWidget* ContentWidget = Cast<UContentWidget>(Container);
	if (!ContentWidget)
	{
		if (Child && Child->GetParent() != Container)
		{
			ModifyForReparent(Child);
			Container->Modify();
			Container->AddChild(Child);
		}
		return;
	}

	UWidget* Current = ContentWidget->GetContent();
	if (Current == Child)
	{
		return;
	}
	ModifyForReparent(Child);
	ModifyForReparent(Current);
	ContentWidget->Modify();
	ContentWidget->SetContent(Child);

	// Something placed here by hand would be dropped; keep it inside our new content panel instead.
	if (Current && !IsKnownManaged(Current->GetFName()) && !PendingDelete.Contains(Current))
	{
		UPanelWidget* ChildPanel = Cast<UPanelWidget>(Child);
		if (ChildPanel && ChildPanel->CanAddMoreChildren())
		{
			ChildPanel->AddChild(Current);
			Context.Log->Info(FString::Printf(TEXT("Moved hand-added widget '%s' into '%s'."), *Current->GetName(), *Child->GetName()));
		}
		else
		{
			Context.Log->Warning(FString::Printf(TEXT("Hand-added widget '%s' was detached from '%s' because Figma content replaced it."), *Current->GetName(), *Container->GetName()));
		}
	}
}

void FFigmaWidgetBuilder::PlaceChild(UPanelWidget* Panel, UWidget* Child, int32 Index)
{
	if (!Panel || !Child)
	{
		return;
	}
	if (Child->GetParent() != Panel)
	{
		ModifyForReparent(Child);
		Panel->Modify();
		Panel->AddChild(Child);
	}
	const int32 Current = Panel->GetChildIndex(Child);
	if (Current != INDEX_NONE && Current != Index)
	{
		Panel->ShiftChild(Index, Child);
	}
}

void FFigmaWidgetBuilder::ReconcileChildren(UPanelWidget* Panel, const TArray<UWidget*>& Desired)
{
	if (!Panel)
	{
		return;
	}
	Panel->Modify();

	// Widgets added by hand (and kept widgets of layers deleted in Figma) stay, after the Figma children.
	TArray<UWidget*> Keep;
	for (UWidget* Child : Panel->GetAllChildren())
	{
		if (!Child || Desired.Contains(Child) || PendingDelete.Contains(Child))
		{
			continue;
		}
		const FName Name = Child->GetFName();
		const bool bHandAdded = !IsKnownManaged(Name);
		const bool bKeptRemoved = RemovedNames.Contains(Name) && Context.Settings.bKeepRemovedWidgets;
		if (bHandAdded || bKeptRemoved)
		{
			Keep.Add(Child);
		}
	}

	int32 Index = 0;
	for (UWidget* Child : Desired)
	{
		PlaceChild(Panel, Child, Index++);
	}
	for (UWidget* Child : Keep)
	{
		PlaceChild(Panel, Child, Index++);
	}
}

void FFigmaWidgetBuilder::BuildContainer(const FFigmaNode& Node, FFigmaSyncNodeRecord& Record, const FPlan& Plan, UWidget* Named, const FLocks& Locks)
{
	const FString Base = Named->GetName();
	const bool bComponentRoot = Options.bComponentContent && Node.Parent == nullptr;
	const FMargin FramePadding = Node.IsAutoLayout() && !bComponentRoot ? FFigmaStyleConverter::ToMargin(Node.Layout.Padding) : FMargin(0.f);
	EHorizontalAlignment ContentH = HAlign_Fill;
	EVerticalAlignment ContentV = VAlign_Fill;
	GetContentAlignment(Node, ContentH, ContentV);
	const bool bNeedsLayout = Node.Children.Num() > 0 || Node.IsAutoLayout();

	UPanelWidget* Layout = nullptr;
	if (Plan.bBorder)
	{
		UBorder* Border = CastChecked<UBorder>(Named);
		UWidget* BorderContent = nullptr;
		if (Plan.bBgOverlay)
		{
			UOverlay* Stack = Cast<UOverlay>(EnsureHelper(Record, FigmaRoles::Stack(), Base + TEXT("_Stack"), UOverlay::StaticClass()));
			UImage* Bg = Cast<UImage>(EnsureHelper(Record, FigmaRoles::Bg(), Base + TEXT("_Bg"), UImage::StaticClass()));
			TArray<UWidget*> StackChildren;
			StackChildren.Add(Bg);
			if (bNeedsLayout)
			{
				Layout = Cast<UPanelWidget>(EnsureHelper(Record, FigmaRoles::Layout(), Base + TEXT("_Layout"), Plan.PanelClass));
				StackChildren.Add(Layout);
			}
			else
			{
				RetireHelper(Record, FigmaRoles::Layout());
			}
			ReconcileChildren(Stack, StackChildren);

			if (UOverlaySlot* BgSlot = Bg ? Cast<UOverlaySlot>(Bg->Slot) : nullptr)
			{
				// Negative padding lets the PNG (and its shadow) spill past the frame.
				const FFigmaRect& B = Node.Background.Bounds;
				BgSlot->SetPadding(FMargin(B.X, B.Y, Node.Width - (B.X + B.Width), Node.Height - (B.Y + B.Height)));
				BgSlot->SetHorizontalAlignment(HAlign_Fill);
				BgSlot->SetVerticalAlignment(VAlign_Fill);
			}
			if (Bg && !Locks.bAppearance)
			{
				bool bClipped = false;
				Bg->SetBrush(MakeBackgroundBrush(Node, bClipped));
			}
			if (UOverlaySlot* LayoutSlot = Layout ? Cast<UOverlaySlot>(Layout->Slot) : nullptr)
			{
				LayoutSlot->SetPadding(Plan.bAbsLayer ? FMargin(0.f) : FramePadding);
				LayoutSlot->SetHorizontalAlignment(Plan.bAbsLayer ? HAlign_Fill : ContentH);
				LayoutSlot->SetVerticalAlignment(Plan.bAbsLayer ? VAlign_Fill : ContentV);
			}
			BorderContent = Stack;
		}
		else
		{
			RetireHelper(Record, FigmaRoles::Stack());
			RetireHelper(Record, FigmaRoles::Bg());
			if (bNeedsLayout)
			{
				Layout = Cast<UPanelWidget>(EnsureHelper(Record, FigmaRoles::Layout(), Base + TEXT("_Layout"), Plan.PanelClass));
				BorderContent = Layout;
			}
			else
			{
				RetireHelper(Record, FigmaRoles::Layout());
			}
		}

		SetSingleContent(Border, BorderContent);
		if (!Locks.bAppearance)
		{
			bool bClipped = false;
			FSlateBrush Brush;
			if (Plan.bBgBrush)
			{
				Brush = MakeBackgroundBrush(Node, bClipped);
			}
			else if (!Plan.bBgOverlay && !bComponentRoot && FFigmaStyleConverter::HasVisibleBox(Node))
			{
				Brush = FFigmaStyleConverter::MakeShapeBrush(Node, Context.Preset);
				if (Node.Style.StrokeAlign != EFigmaStrokeAlign::Inside && Node.Style.HasVisibleStroke())
				{
					Context.Log->Info(TEXT("Outside/center stroke is drawn inside the box in UMG."), &Node);
				}
			}
			else
			{
				Brush.DrawAs = ESlateBrushDrawType::NoDrawType;
			}
			Border->SetBrush(Brush);
			Border->SetBrushColor(FLinearColor::White);
			if (Node.bHasStyle && !Node.Style.bSimple && !Node.Background.IsSet() && !bComponentRoot)
			{
				Context.Log->Info(TEXT("Gradient/effects background approximated with a flat color (rasterizing was off in the exporter)."), &Node);
			}
		}
		if (UBorderSlot* ContentSlot = BorderContent ? Cast<UBorderSlot>(BorderContent->Slot) : nullptr)
		{
			const bool bPadHere = !Plan.bBgOverlay && !Plan.bAbsLayer;
			ContentSlot->SetPadding(bPadHere ? FramePadding : FMargin(0.f));
			ContentSlot->SetHorizontalAlignment(bPadHere ? ContentH : HAlign_Fill);
			ContentSlot->SetVerticalAlignment(bPadHere ? ContentV : VAlign_Fill);
		}
	}
	else
	{
		RetireHelper(Record, FigmaRoles::Stack());
		RetireHelper(Record, FigmaRoles::Bg());
		RetireHelper(Record, FigmaRoles::Layout());
		Layout = Cast<UPanelWidget>(Named);
	}

	UPanelWidget* FlowPanel = Layout;
	UCanvasPanel* AbsPanel = nullptr;
	if (Plan.bAbsLayer && Layout)
	{
		FlowPanel = Cast<UPanelWidget>(EnsureHelper(Record, FigmaRoles::Flow(), Base + TEXT("_Flow"), Plan.FlowClass));
		AbsPanel = Cast<UCanvasPanel>(EnsureHelper(Record, FigmaRoles::Abs(), Base + TEXT("_Abs"), UCanvasPanel::StaticClass()));
		TArray<UWidget*> LayerChildren;
		LayerChildren.Add(FlowPanel);
		LayerChildren.Add(AbsPanel);
		ReconcileChildren(Layout, LayerChildren);
		if (UOverlaySlot* FlowSlot = FlowPanel ? Cast<UOverlaySlot>(FlowPanel->Slot) : nullptr)
		{
			FlowSlot->SetPadding(FramePadding);
			FlowSlot->SetHorizontalAlignment(ContentH);
			FlowSlot->SetVerticalAlignment(ContentV);
		}
		if (UOverlaySlot* AbsSlot = AbsPanel ? Cast<UOverlaySlot>(AbsPanel->Slot) : nullptr)
		{
			AbsSlot->SetPadding(FMargin(0.f));
			AbsSlot->SetHorizontalAlignment(HAlign_Fill);
			AbsSlot->SetVerticalAlignment(VAlign_Fill);
		}
	}
	else
	{
		RetireHelper(Record, FigmaRoles::Flow());
		RetireHelper(Record, FigmaRoles::Abs());
	}

	if (FlowPanel)
	{
		ApplyPanelSettings(FlowPanel, Node);
	}
	BuildChildren(Node, Record, Plan, FlowPanel, AbsPanel, Locks);
	TransferLeftovers(Node, FlowPanel);
}

void FFigmaWidgetBuilder::BuildButton(const FFigmaNode& Node, FFigmaSyncNodeRecord& Record, const FPlan& Plan, UButton* Button, const FLocks& Locks)
{
	for (const FName Role : { FigmaRoles::Stack(), FigmaRoles::Bg(), FigmaRoles::Flow(), FigmaRoles::Abs() })
	{
		RetireHelper(Record, Role);
	}

	UPanelWidget* Layout = nullptr;
	if (Node.Children.Num() > 0)
	{
		Layout = Cast<UPanelWidget>(EnsureHelper(Record, FigmaRoles::Layout(), Button->GetName() + TEXT("_Layout"), Plan.PanelClass));
		SetSingleContent(Button, Layout);
		if (UButtonSlot* ContentSlot = Layout ? Cast<UButtonSlot>(Layout->Slot) : nullptr)
		{
			EHorizontalAlignment H = HAlign_Fill;
			EVerticalAlignment V = VAlign_Fill;
			GetContentAlignment(Node, H, V);
			ContentSlot->SetPadding(FMargin(0.f));
			ContentSlot->SetHorizontalAlignment(H);
			ContentSlot->SetVerticalAlignment(V);
		}
		if (Layout)
		{
			ApplyPanelSettings(Layout, Node);
		}
	}
	else
	{
		RetireHelper(Record, FigmaRoles::Layout());
	}

	if (!Locks.bAppearance)
	{
		Button->SetStyle(MakeButtonStyle(Node, Button->GetStyle()));
		Button->SetColorAndOpacity(FLinearColor::White);
		Button->SetBackgroundColor(FLinearColor::White);

		if (const FFigmaComponentSet* Set = Context.Document.FindComponentSet(Node.Component.ComponentSetId))
		{
			const FString StateProperty = FFigmaButtonStates::FindStateProperty(*Set);
			const FString* StateValue = StateProperty.IsEmpty() ? nullptr : Node.Component.Variant.Find(StateProperty);
			if (StateValue && FFigmaButtonStates::Classify(*StateValue) == EFigmaButtonState::Disabled)
			{
				Button->SetIsEnabled(false);
				Context.Log->Info(TEXT("Uses the Disabled variant, so the button starts disabled (enable it from your Blueprint)."), &Node);
			}
		}
	}

	if (HasAbsoluteChildren(Node))
	{
		Context.Log->Info(TEXT("Absolutely positioned layers inside a button are laid out with the others."), &Node);
	}
	BuildChildren(Node, Record, Plan, Layout, nullptr, Locks);
	TransferLeftovers(Node, Layout);
}

FFigmaWidgetBuilder::EParentKind FFigmaWidgetBuilder::KindOfPanel(const UPanelWidget* Panel)
{
	if (Cast<UCanvasPanel>(Panel)) return EParentKind::Canvas;
	if (Cast<UHorizontalBox>(Panel)) return EParentKind::HBox;
	if (Cast<UVerticalBox>(Panel)) return EParentKind::VBox;
	if (Cast<UWrapBox>(Panel)) return EParentKind::Wrap;
	if (Cast<UOverlay>(Panel)) return EParentKind::Overlay;
	return EParentKind::Content;
}

void FFigmaWidgetBuilder::BuildChildren(const FFigmaNode& Node, FFigmaSyncNodeRecord& Record, const FPlan& Plan, UPanelWidget* FlowPanel, UCanvasPanel* AbsPanel, const FLocks& Locks)
{
	if (Locks.bChildren)
	{
		// Their records are carried over untouched (see HandleRemovedNodes), so nothing under here looks deleted.
		for (const TSharedPtr<FFigmaNode>& Child : Node.Children)
		{
			if (Child.IsValid())
			{
				CollectIds(*Child, LockedIds);
			}
		}
		Context.Log->Info(TEXT("Children are locked; they were not synced."), &Node);
		return;
	}
	if (!FlowPanel)
	{
		if (Node.Children.Num() > 0)
		{
			Context.Log->Warning(TEXT("This layer has children but no panel to hold them."), &Node);
		}
		return;
	}

	TArray<const FFigmaNode*> FlowChildren;
	TArray<const FFigmaNode*> AbsChildren;
	for (const TSharedPtr<FFigmaNode>& Child : Node.Children)
	{
		if (!Child.IsValid())
		{
			continue;
		}
		if (AbsPanel && Child->Child.bAbsolute)
		{
			AbsChildren.Add(Child.Get());
		}
		else
		{
			FlowChildren.Add(Child.Get());
		}
	}

	const EParentKind FlowKind = KindOfPanel(FlowPanel);
	const bool bSpaceBetween = Node.IsAutoLayout() && !Node.Layout.bWrap && Node.Layout.PrimaryAlign == EFigmaAlign::SpaceBetween && FlowChildren.Num() > 1 &&
		(FlowKind == EParentKind::HBox || FlowKind == EParentKind::VBox);
	if (Node.IsAutoLayout() && Node.Layout.ItemSpacing < 0.f && Node.Layout.bReverseZIndex)
	{
		Context.Log->Info(TEXT("Overlapping items with 'first on top' stacking draw last-on-top in UMG."), &Node);
	}
	if (Node.IsAutoLayout() && Node.Layout.CounterAlign == EFigmaAlign::Baseline)
	{
		Context.Log->Info(TEXT("Baseline alignment is approximated as center alignment."), &Node);
	}

	struct FPlaced
	{
		UWidget* Widget;
		const FFigmaNode* Node;
		FParentInfo Info;
	};
	TArray<FPlaced> Placed;
	TArray<UWidget*> Desired;
	TArray<UWidget*> Spacers;
	int32 SpacerCount = 0;

	for (int32 Index = 0; Index < FlowChildren.Num(); ++Index)
	{
		FParentInfo Info;
		Info.Kind = FlowKind;
		Info.Node = &Node;
		Info.bFoldPadding = Plan.bFoldPadding;
		Info.Index = Index;
		Info.Count = FlowChildren.Num();
		UWidget* ChildOuter = BuildNode(*FlowChildren[Index], Info, false);
		if (!ChildOuter)
		{
			continue;
		}
		if (bSpaceBetween && Desired.Num() > 0)
		{
			++SpacerCount;
			const FString SpacerBase = Record.Widget.IsNone() ? Node.CleanName : Record.Widget.ToString();
			UWidget* Spacer = EnsureHelper(Record, FigmaRoles::Spacer(SpacerCount), SpacerBase + FString::Printf(TEXT("_Spacer%d"), SpacerCount), USpacer::StaticClass());
			if (Spacer)
			{
				Desired.Add(Spacer);
				Spacers.Add(Spacer);
			}
		}
		Desired.Add(ChildOuter);
		Placed.Add({ ChildOuter, FlowChildren[Index], Info });
	}

	// Retire spacers that are no longer needed.
	for (int32 Index = SpacerCount + 1; Record.Helpers.Contains(FigmaRoles::Spacer(Index)); ++Index)
	{
		RetireHelper(Record, FigmaRoles::Spacer(Index));
	}

	ReconcileChildren(FlowPanel, Desired);
	for (const FPlaced& Item : Placed)
	{
		ConfigureChildSlot(Item.Widget, *Item.Node, Item.Info);
	}
	for (UWidget* Spacer : Spacers)
	{
		if (UHorizontalBoxSlot* HSlot = Cast<UHorizontalBoxSlot>(Spacer->Slot))
		{
			FSlateChildSize Fill(ESlateSizeRule::Fill);
			HSlot->SetSize(Fill);
			HSlot->SetPadding(FMargin(0.f));
		}
		else if (UVerticalBoxSlot* VSlot = Cast<UVerticalBoxSlot>(Spacer->Slot))
		{
			FSlateChildSize Fill(ESlateSizeRule::Fill);
			VSlot->SetSize(Fill);
			VSlot->SetPadding(FMargin(0.f));
		}
	}

	if (AbsPanel)
	{
		TArray<UWidget*> AbsDesired;
		TArray<FPlaced> AbsPlaced;
		for (const FFigmaNode* Child : AbsChildren)
		{
			FParentInfo Info;
			Info.Kind = EParentKind::Canvas;
			Info.Node = &Node;
			if (UWidget* ChildOuter = BuildNode(*Child, Info, false))
			{
				AbsDesired.Add(ChildOuter);
				AbsPlaced.Add({ ChildOuter, Child, Info });
			}
		}
		ReconcileChildren(AbsPanel, AbsDesired);
		for (const FPlaced& Item : AbsPlaced)
		{
			ConfigureChildSlot(Item.Widget, *Item.Node, Item.Info);
		}
	}
}

void FFigmaWidgetBuilder::ApplyPanelSettings(UPanelWidget* Panel, const FFigmaNode& Node)
{
	if (UWrapBox* Wrap = Cast<UWrapBox>(Panel))
	{
		Wrap->SetInnerSlotPadding(FVector2D(Node.Layout.ItemSpacing, Node.Layout.CounterAxisSpacing));
		switch (Node.Layout.PrimaryAlign)
		{
		case EFigmaAlign::Center: Wrap->SetHorizontalAlignment(HAlign_Center); break;
		case EFigmaAlign::Max: Wrap->SetHorizontalAlignment(HAlign_Right); break;
		default: Wrap->SetHorizontalAlignment(HAlign_Left); break;
		}
	}
}

// ----------------------------------------------------------------------------------------------------------------
// Slots

FFigmaRect FFigmaWidgetBuilder::GetPlacementBox(const FFigmaNode& Node)
{
	FFigmaRect Box;
	Box.X = Node.X;
	Box.Y = Node.Y;
	Box.Width = Node.Width;
	Box.Height = Node.Height;
	if (Node.IsRasterized())
	{
		Box.X += Node.Image.Bounds.X;
		Box.Y += Node.Image.Bounds.Y;
		Box.Width = Node.Image.Bounds.Width;
		Box.Height = Node.Image.Bounds.Height;
	}
	else if (Node.Type == EFigmaNodeType::Line)
	{
		// Lines have zero height; the stroke is centered on them.
		const float Weight = FMath::Max(Node.bHasStyle ? Node.Style.StrokeWeight : 1.f, 1.f);
		Box.Y += Box.Height * 0.5f - Weight * 0.5f;
		Box.Height = Weight;
	}
	return Box;
}

void FFigmaWidgetBuilder::ConfigureChildSlot(UWidget* Child, const FFigmaNode& Node, const FParentInfo& Parent)
{
	if (!Child || !Child->Slot || !Parent.Node)
	{
		return;
	}
	if (const FFigmaSyncNodeRecord* Record = NewRecords.Find(Node.Id))
	{
		if (GetLocks(*Record).bLayout)
		{
			return;
		}
	}
	Child->Slot->Modify();
	const FFigmaRect Box = GetPlacementBox(Node);
	const float ParentW = Parent.Node->Width;
	const float ParentH = Parent.Node->Height;

	if (UCanvasPanelSlot* CanvasSlot = Cast<UCanvasPanelSlot>(Child->Slot))
	{
		auto Axis = [](EFigmaConstraint Constraint, float Pos, float Size, float ParentSize, double& AnchorMin, double& AnchorMax, float& OffsetMin, float& OffsetMax)
		{
			switch (Constraint)
			{
			case EFigmaConstraint::Max:
				AnchorMin = AnchorMax = 1.0;
				OffsetMin = Pos - ParentSize;
				OffsetMax = Size;
				break;
			case EFigmaConstraint::Center:
				AnchorMin = AnchorMax = 0.5;
				OffsetMin = Pos - ParentSize * 0.5f;
				OffsetMax = Size;
				break;
			case EFigmaConstraint::Stretch:
				AnchorMin = 0.0;
				AnchorMax = 1.0;
				OffsetMin = Pos;
				OffsetMax = ParentSize - (Pos + Size);
				break;
			case EFigmaConstraint::Scale:
				AnchorMin = ParentSize > 0.f ? Pos / ParentSize : 0.0;
				AnchorMax = ParentSize > 0.f ? (Pos + Size) / ParentSize : 1.0;
				OffsetMin = 0.f;
				OffsetMax = 0.f;
				break;
			default:
				AnchorMin = AnchorMax = 0.0;
				OffsetMin = Pos;
				OffsetMax = Size;
				break;
			}
		};

		const EFigmaConstraint ConstraintH = Node.bHasConstraints ? Node.ConstraintH : EFigmaConstraint::Min;
		const EFigmaConstraint ConstraintV = Node.bHasConstraints ? Node.ConstraintV : EFigmaConstraint::Min;
		FAnchors Anchors;
		FMargin Offsets;
		Axis(ConstraintH, Box.X, Box.Width, ParentW, Anchors.Minimum.X, Anchors.Maximum.X, Offsets.Left, Offsets.Right);
		Axis(ConstraintV, Box.Y, Box.Height, ParentH, Anchors.Minimum.Y, Anchors.Maximum.Y, Offsets.Top, Offsets.Bottom);

		FVector2D Alignment(0.0, 0.0);
		bool bAutoSize = false;
		const bool bStretchy = ConstraintH == EFigmaConstraint::Stretch || ConstraintH == EFigmaConstraint::Scale ||
			ConstraintV == EFigmaConstraint::Stretch || ConstraintV == EFigmaConstraint::Scale;
		if (Node.IsText() && Node.Text.AutoResize == EFigmaTextAutoResize::WidthAndHeight && !bStretchy && Child == FindWidgetByName(NewRecords.FindRef(Node.Id).Widget))
		{
			// Hugging text: let Slate measure it (UE fonts differ slightly), anchored where Figma's text box was.
			bAutoSize = true;
			Alignment.X = Node.Text.AlignH == EFigmaTextAlignH::Center ? 0.5 : (Node.Text.AlignH == EFigmaTextAlignH::Right ? 1.0 : 0.0);
			Offsets.Left += Box.Width * static_cast<float>(Alignment.X);
		}
		CanvasSlot->SetAnchors(Anchors);
		CanvasSlot->SetOffsets(Offsets);
		CanvasSlot->SetAlignment(Alignment);
		CanvasSlot->SetAutoSize(bAutoSize);
		CanvasSlot->SetZOrder(0);
		return;
	}

	const bool bHorizontalBox = Cast<UHorizontalBoxSlot>(Child->Slot) != nullptr;
	const bool bVerticalBox = Cast<UVerticalBoxSlot>(Child->Slot) != nullptr;
	if (bHorizontalBox || bVerticalBox)
	{
		const FFigmaAutoLayout& Layout = Parent.Node->Layout;
		const bool bFirst = Parent.Index == 0;
		const bool bLast = Parent.Index == Parent.Count - 1;
		const float Spacing = Layout.PrimaryAlign == EFigmaAlign::SpaceBetween ? 0.f : Layout.ItemSpacing;
		FMargin Padding(0.f);
		if (bHorizontalBox)
		{
			Padding.Left = bFirst ? 0.f : Spacing;
		}
		else
		{
			Padding.Top = bFirst ? 0.f : Spacing;
		}
		if (Parent.bFoldPadding)
		{
			const FFigmaPadding& P = Layout.Padding;
			if (bHorizontalBox)
			{
				Padding.Left += bFirst ? P.Left : 0.f;
				Padding.Right += bLast ? P.Right : 0.f;
				Padding.Top += P.Top;
				Padding.Bottom += P.Bottom;
			}
			else
			{
				Padding.Top += bFirst ? P.Top : 0.f;
				Padding.Bottom += bLast ? P.Bottom : 0.f;
				Padding.Left += P.Left;
				Padding.Right += P.Right;
			}
		}
		if (Node.IsRasterized())
		{
			// Let the PNG's overflow (shadows, outside strokes) spill out without changing the layout size.
			const FFigmaRect& B = Node.Image.Bounds;
			Padding.Left += B.X;
			Padding.Top += B.Y;
			Padding.Right -= (B.X + B.Width) - Node.Width;
			Padding.Bottom -= (B.Y + B.Height) - Node.Height;
		}

		const EFigmaSizing Primary = bHorizontalBox ? Node.Child.SizingH : Node.Child.SizingV;
		const EFigmaSizing Cross = bHorizontalBox ? Node.Child.SizingV : Node.Child.SizingH;
		FSlateChildSize Size(ESlateSizeRule::Automatic);
		if (Primary == EFigmaSizing::Fill || Node.Child.Grow > 0.f)
		{
			Size.SizeRule = ESlateSizeRule::Fill;
			Size.Value = FMath::Max(Node.Child.Grow, 1.f);
		}
		const bool bCrossFill = Cross == EFigmaSizing::Fill || Node.Child.bStretch;

		if (UHorizontalBoxSlot* HSlot = Cast<UHorizontalBoxSlot>(Child->Slot))
		{
			HSlot->SetPadding(Padding);
			HSlot->SetSize(Size);
			HSlot->SetHorizontalAlignment(HAlign_Fill);
			HSlot->SetVerticalAlignment(bCrossFill ? VAlign_Fill : CounterToVAlign(Layout.CounterAlign));
		}
		else if (UVerticalBoxSlot* VSlot = Cast<UVerticalBoxSlot>(Child->Slot))
		{
			VSlot->SetPadding(Padding);
			VSlot->SetSize(Size);
			VSlot->SetVerticalAlignment(VAlign_Fill);
			VSlot->SetHorizontalAlignment(bCrossFill ? HAlign_Fill : CounterToHAlign(Layout.CounterAlign));
		}
		return;
	}

	if (UWrapBoxSlot* WrapSlot = Cast<UWrapBoxSlot>(Child->Slot))
	{
		WrapSlot->SetPadding(FMargin(0.f));
		WrapSlot->SetFillEmptySpace(Node.Child.SizingH == EFigmaSizing::Fill);
		WrapSlot->SetHorizontalAlignment(HAlign_Fill);
		WrapSlot->SetVerticalAlignment(Node.Child.SizingV == EFigmaSizing::Fill ? VAlign_Fill : CounterToVAlign(Parent.Node->Layout.CounterAlign));
		return;
	}

	if (UOverlaySlot* OverlaySlot = Cast<UOverlaySlot>(Child->Slot))
	{
		FMargin Padding(0.f);
		EHorizontalAlignment H = HAlign_Fill;
		EVerticalAlignment V = VAlign_Fill;
		switch (Node.ConstraintH)
		{
		case EFigmaConstraint::Min: H = HAlign_Left; Padding.Left = Box.X; break;
		case EFigmaConstraint::Max: H = HAlign_Right; Padding.Right = ParentW - (Box.X + Box.Width); break;
		case EFigmaConstraint::Center:
		{
			H = HAlign_Center;
			const float Offset = (Box.X + Box.Width * 0.5f) - ParentW * 0.5f;
			(Offset > 0.f ? Padding.Left : Padding.Right) = FMath::Abs(Offset) * 2.f;
			break;
		}
		default: Padding.Left = Box.X; Padding.Right = ParentW - (Box.X + Box.Width); break;
		}
		switch (Node.ConstraintV)
		{
		case EFigmaConstraint::Min: V = VAlign_Top; Padding.Top = Box.Y; break;
		case EFigmaConstraint::Max: V = VAlign_Bottom; Padding.Bottom = ParentH - (Box.Y + Box.Height); break;
		case EFigmaConstraint::Center:
		{
			V = VAlign_Center;
			const float Offset = (Box.Y + Box.Height * 0.5f) - ParentH * 0.5f;
			(Offset > 0.f ? Padding.Top : Padding.Bottom) = FMath::Abs(Offset) * 2.f;
			break;
		}
		default: Padding.Top = Box.Y; Padding.Bottom = ParentH - (Box.Y + Box.Height); break;
		}
		OverlaySlot->SetPadding(Padding);
		OverlaySlot->SetHorizontalAlignment(H);
		OverlaySlot->SetVerticalAlignment(V);
	}
}

void FFigmaWidgetBuilder::ConfigureSizeBox(UWidget* SizeWidget, const FFigmaNode& Node, const FPlan& Plan)
{
	USizeBox* SizeBox = Cast<USizeBox>(SizeWidget);
	if (!SizeBox)
	{
		return;
	}
	if (Plan.bSizeW)
	{
		SizeBox->SetWidthOverride(Node.Width);
	}
	else
	{
		SizeBox->ClearWidthOverride();
	}
	if (Plan.bSizeH)
	{
		SizeBox->SetHeightOverride(Node.Height);
	}
	else
	{
		SizeBox->ClearHeightOverride();
	}
	if (Node.SizeLimits.MinWidth.IsSet())
	{
		SizeBox->SetMinDesiredWidth(Node.SizeLimits.MinWidth.GetValue());
	}
	if (Node.SizeLimits.MaxWidth.IsSet())
	{
		SizeBox->SetMaxDesiredWidth(Node.SizeLimits.MaxWidth.GetValue());
	}
	if (Node.SizeLimits.MinHeight.IsSet())
	{
		SizeBox->SetMinDesiredHeight(Node.SizeLimits.MinHeight.GetValue());
	}
	if (Node.SizeLimits.MaxHeight.IsSet())
	{
		SizeBox->SetMaxDesiredHeight(Node.SizeLimits.MaxHeight.GetValue());
	}

	UWidget* Content = SizeBox->GetContent();
	if (USizeBoxSlot* ContentSlot = Content ? Cast<USizeBoxSlot>(Content->Slot) : nullptr)
	{
		EHorizontalAlignment H = Plan.SizeHAlign;
		EVerticalAlignment V = Plan.SizeVAlign;
		if (Plan.Kind == EFigmaWidgetKind::Container && !Plan.bBorder)
		{
			// No Border: the SizeBox aligns the hugging panel (centered/right-aligned auto layout).
			GetContentAlignment(Node, H, V);
		}
		ContentSlot->SetPadding(FMargin(0.f));
		ContentSlot->SetHorizontalAlignment(H);
		ContentSlot->SetVerticalAlignment(V);
	}
}

// ----------------------------------------------------------------------------------------------------------------
// Look

FName FFigmaWidgetBuilder::GetTexturePreset(const FFigmaNode& Node) const
{
	return FFigmaTextureImporter::GetPresetForNode(Node, Context.Preset);
}

FSlateBrush FFigmaWidgetBuilder::MakeNodeBackgroundBrush(const FFigmaNode& Node, FFigmaImportContext& InContext, const FString& TextureFolder, bool* bOutOverflowClipped)
{
	if (bOutOverflowClipped)
	{
		*bOutOverflowClipped = false;
	}
	if (Node.Background.IsSet())
	{
		const FFigmaAsset* Asset = InContext.Document.FindAsset(Node.Background.AssetId);
		UTexture2D* Texture = Asset ? InContext.Textures->GetOrImport(*Asset, FFigmaTextureImporter::GetPresetForNode(Node, InContext.Preset), TextureFolder) : nullptr;
		if (Texture)
		{
			if (bOutOverflowClipped)
			{
				*bOutOverflowClipped = BackgroundOverflows(Node);
			}
			const float Slice = FCString::Atof(*Node.GetTag(TEXT("slice"), TEXT("0")));
			return FFigmaStyleConverter::MakeImageBrush(Texture, FVector2D(Node.Background.Bounds.Width, Node.Background.Bounds.Height), Slice);
		}
	}
	if (Node.bHasStyle)
	{
		return FFigmaStyleConverter::MakeShapeBrush(Node, InContext.Preset);
	}
	FSlateBrush None;
	None.DrawAs = ESlateBrushDrawType::NoDrawType;
	return None;
}

FSlateBrush FFigmaWidgetBuilder::MakeBackgroundBrush(const FFigmaNode& Node, bool& bOutOverflowClipped)
{
	return MakeNodeBackgroundBrush(Node, Context, Options.TextureFolder, &bOutOverflowClipped);
}

FButtonStyle FFigmaWidgetBuilder::MakeButtonStyle(const FFigmaNode& Node, const FButtonStyle& Base)
{
	if (Context.Preset)
	{
		const FString Key = Node.Component.IsSet() ? Node.Component.GetFamilyName() : Node.CleanName;
		const FFigmaButtonStyleOverride* Override = Context.Preset->FindButtonStyle(Key);
		if (!Override)
		{
			Override = Context.Preset->FindButtonStyle(Node.CleanName);
		}
		if (Override && Override->bOverrideButtonStyle)
		{
			return Override->ButtonStyle;
		}
	}

	const float HoverBrightness = Context.Preset ? Context.Preset->HoverBrightness : 1.15f;
	const float PressedBrightness = Context.Preset ? Context.Preset->PressedBrightness : 0.85f;
	const float DisabledOpacity = Context.Preset ? Context.Preset->DisabledOpacity : 0.45f;

	bool bClipped = false;
	FSlateBrush Normal = MakeBackgroundBrush(Node, bClipped);
	FSlateBrush Hovered = FFigmaStyleConverter::ScaleBrush(Normal, HoverBrightness);
	FSlateBrush Pressed = FFigmaStyleConverter::ScaleBrush(Normal, PressedBrightness);
	FSlateBrush Disabled = FFigmaStyleConverter::ScaleBrush(Normal, 1.f, DisabledOpacity);
	if (bClipped)
	{
		Context.Log->Info(TEXT("Button shadows are drawn inside the button box (button brushes can't overflow)."), &Node);
	}

	// Figma variants win over derived states.
	if (const FFigmaComponentSet* Set = Context.Document.FindComponentSet(Node.Component.ComponentSetId))
	{
		const FString StateProperty = FFigmaButtonStates::FindStateProperty(*Set);
		const FString Group = FFigmaButtonStates::GroupKey(Node.Component.Variant, StateProperty);
		auto FromVariant = [&](EFigmaButtonState ButtonState, FSlateBrush& OutBrush)
		{
			const FFigmaVariant* Variant = FFigmaButtonStates::FindVariant(*Set, StateProperty, Group, ButtonState);
			if (Variant && Variant->Node.IsValid())
			{
				bool bVariantClipped = false;
				OutBrush = MakeBackgroundBrush(*Variant->Node, bVariantClipped);
			}
		};
		FromVariant(EFigmaButtonState::Normal, Normal);
		FromVariant(EFigmaButtonState::Hovered, Hovered);
		FromVariant(EFigmaButtonState::Pressed, Pressed);
		FromVariant(EFigmaButtonState::Disabled, Disabled);
	}

	FButtonStyle Style = Base;
	Style.SetNormal(Normal);
	Style.SetHovered(Hovered);
	Style.SetPressed(Pressed);
	Style.SetDisabled(Disabled);
	const FMargin Padding = Node.IsAutoLayout() ? FFigmaStyleConverter::ToMargin(Node.Layout.Padding) : FMargin(0.f);
	Style.SetNormalPadding(Padding);
	Style.SetPressedPadding(Padding);
	return Style;
}

void FFigmaWidgetBuilder::ConfigureImage(UWidget* Widget, const FFigmaNode& Node)
{
	UImage* Image = Cast<UImage>(Widget);
	if (!Image)
	{
		return;
	}
	const FFigmaAsset* Asset = Context.Document.FindAsset(Node.Image.AssetId);
	UTexture2D* Texture = Asset ? Context.Textures->GetOrImport(*Asset, GetTexturePreset(Node), Options.TextureFolder) : nullptr;
	if (!Texture)
	{
		Context.Log->Warning(TEXT("Image missing from the bundle; drawn as a placeholder box."), &Node);
		Image->SetBrush(FFigmaStyleConverter::MakeShapeBrush(Node, Context.Preset));
		return;
	}
	const float Slice = FCString::Atof(*Node.GetTag(TEXT("slice"), TEXT("0")));
	Image->SetBrush(FFigmaStyleConverter::MakeImageBrush(Texture, FVector2D(Node.Image.Bounds.Width, Node.Image.Bounds.Height), Slice));
	Image->SetColorAndOpacity(FLinearColor::White);
}

void FFigmaWidgetBuilder::ConfigureShape(UWidget* Widget, const FFigmaNode& Node)
{
	UImage* Image = Cast<UImage>(Widget);
	if (!Image)
	{
		return;
	}
	if (Node.Type == EFigmaNodeType::Line)
	{
		Image->SetBrush(FFigmaStyleConverter::MakeLineBrush(Node));
	}
	else
	{
		Image->SetBrush(FFigmaStyleConverter::MakeShapeBrush(Node, Context.Preset));
		if (Node.bHasStyle && !Node.Style.bSimple)
		{
			Context.Log->Info(TEXT("Gradient/effects approximated with a flat color (rasterizing was off in the exporter)."), &Node);
		}
		if (!Node.bHasStyle && Node.Type != EFigmaNodeType::Rectangle && Node.Type != EFigmaNodeType::Ellipse)
		{
			Context.Log->Warning(TEXT("Vector layer without an exported image; tag it [img] in Figma."), &Node);
		}
	}
	Image->SetColorAndOpacity(FLinearColor::White);
}

void FFigmaWidgetBuilder::ConfigureText(UWidget* Widget, const FFigmaNode& Node, const FParentInfo& Parent, const FLocks& Locks)
{
	UTextBlock* Text = Cast<UTextBlock>(Widget);
	if (!Text)
	{
		return;
	}
	const FFigmaText& Info = Node.Text;
	if (!Locks.bText)
	{
		Text->SetText(FText::FromString(Info.Characters));
	}
	if (Locks.bAppearance)
	{
		return;
	}

	FSlateFontInfo Font = Context.Fonts->MakeFontInfo(Info);
	FLinearColor Color = Info.Color.ToLinear();
	if (Context.Preset)
	{
		if (const FFigmaTextStyleOverride* Override = Context.Preset->FindTextStyle(Info.TextStyle))
		{
			if (Override->Font.FontObject)
			{
				Font = Override->Font;
			}
			if (Override->bOverrideColor)
			{
				Color = Override->Color;
			}
		}
	}
	Text->SetFont(Font);
	Text->SetColorAndOpacity(FSlateColor(Color));

	switch (Info.AlignH)
	{
	case EFigmaTextAlignH::Center: Text->SetJustification(ETextJustify::Center); break;
	case EFigmaTextAlignH::Right: Text->SetJustification(ETextJustify::Right); break;
	case EFigmaTextAlignH::Justified:
		Text->SetJustification(ETextJustify::Left);
		Context.Log->Info(TEXT("Justified text is left-aligned in UMG."), &Node);
		break;
	default: Text->SetJustification(ETextJustify::Left); break;
	}

	Text->SetShadowOffset(Info.bHasShadow ? Info.ShadowOffset : FVector2D::ZeroVector);
	Text->SetShadowColorAndOpacity(Info.bHasShadow ? Info.ShadowColor.ToLinear() : FLinearColor::Transparent);
	if (Info.bHasShadow && Info.ShadowRadius > 0.f)
	{
		Context.Log->Info(TEXT("Text shadow blur is not supported by UMG; the shadow is sharp."), &Node);
	}

	switch (Info.TextCase)
	{
	case EFigmaTextCase::Upper: Text->SetTextTransformPolicy(ETextTransformPolicy::ToUpper); break;
	case EFigmaTextCase::Lower: Text->SetTextTransformPolicy(ETextTransformPolicy::ToLower); break;
	case EFigmaTextCase::Title:
	case EFigmaTextCase::SmallCaps:
		Text->SetTextTransformPolicy(ETextTransformPolicy::None);
		Context.Log->Info(TEXT("Title case / small caps are not supported by UMG; the raw text is shown."), &Node);
		break;
	default: Text->SetTextTransformPolicy(ETextTransformPolicy::None); break;
	}
	Text->SetTextOverflowPolicy(Info.bTruncate ? ETextOverflowPolicy::Ellipsis : ETextOverflowPolicy::Clip);

	if (Info.Decoration != EFigmaTextDecoration::None)
	{
		Context.Log->Info(TEXT("Underline/strikethrough are not applied; use a Rich Text Block if you need them."), &Node);
	}

	if (Info.LineHeightUnit != EFigmaUnit::Auto && Info.FontSize > 0.f)
	{
		const float Multiplier = Info.LineHeightUnit == EFigmaUnit::Percent ? Info.LineHeight / 100.f : Info.LineHeight / Info.FontSize;
		FFigmaReflection::SetFloat(Text, TEXT("LineHeightPercentage"), FMath::Clamp(Multiplier / NaturalLineHeightRatio, 0.1f, 10.f));
	}

	// Wrapping: fixed-width text wraps at its Figma width; hugging text never wraps.
	const bool bWraps = Info.AutoResize == EFigmaTextAutoResize::Height || Info.AutoResize == EFigmaTextAutoResize::None;
	const bool bInBox = Parent.Kind == EParentKind::HBox || Parent.Kind == EParentKind::VBox || Parent.Kind == EParentKind::Wrap;
	float MinWidth = 0.f;
	float WrapAt = 0.f;
	bool bAutoWrap = false;
	if (bWraps)
	{
		if (bInBox && Node.Child.SizingH == EFigmaSizing::Fixed && !Info.bTruncate)
		{
			MinWidth = Node.Width;
			WrapAt = Node.Width;
		}
		else
		{
			bAutoWrap = true;
		}
	}
	Text->SetAutoWrapText(bAutoWrap);
	Text->SetMinDesiredWidth(MinWidth);
	FFigmaReflection::SetFloat(Text, TEXT("WrapTextAt"), WrapAt);

	if (Info.bMixed)
	{
		Context.Log->Info(TEXT("Mixed text styles: the dominant style is used (a Rich Text Block can show every run)."), &Node);
	}
}

bool FFigmaWidgetBuilder::NeedsClipping(const FFigmaNode& Node) const
{
	if (!Node.bClipsContent)
	{
		return false;
	}
	constexpr float Tolerance = 0.5f;
	for (const TSharedPtr<FFigmaNode>& Child : Node.Children)
	{
		if (!Child.IsValid())
		{
			continue;
		}
		const FFigmaRect Box = GetPlacementBox(*Child);
		// Rotated children: grow the box by the rotation's bounding radius.
		float Grow = 0.f;
		if (!FMath::IsNearlyZero(Child->Rotation))
		{
			Grow = 0.5f * (FMath::Sqrt(Box.Width * Box.Width + Box.Height * Box.Height) - FMath::Min(Box.Width, Box.Height));
		}
		if (Box.X - Grow < -Tolerance || Box.Y - Grow < -Tolerance || Box.X + Box.Width + Grow > Node.Width + Tolerance || Box.Y + Box.Height + Grow > Node.Height + Tolerance)
		{
			return true;
		}
	}
	return false;
}

void FFigmaWidgetBuilder::ApplyCommon(UWidget* Outer, UWidget* Named, const FFigmaNode& Node, const FPlan& Plan, const FLocks& Locks)
{
	if (Locks.bAppearance)
	{
		return;
	}

	ESlateVisibility Visible = ESlateVisibility::SelfHitTestInvisible;
	switch (Plan.Kind)
	{
	case EFigmaWidgetKind::Button:
	case EFigmaWidgetKind::Leaf:
		Visible = ESlateVisibility::Visible;
		break;
	case EFigmaWidgetKind::Container:
		Visible = ESlateVisibility::SelfHitTestInvisible;
		break;
	default:
		Visible = Context.Settings.bMakeDecorationsHitTestInvisible ? ESlateVisibility::HitTestInvisible : ESlateVisibility::Visible;
		break;
	}
	Named->SetVisibility(Visible);
	if (Outer != Named)
	{
		Outer->SetVisibility(Node.bVisible ? ESlateVisibility::SelfHitTestInvisible : ESlateVisibility::Collapsed);
	}
	else if (!Node.bVisible)
	{
		Named->SetVisibility(ESlateVisibility::Collapsed);
	}

	// Rasterized images already carry the layer's opacity in their pixels.
	Named->SetRenderOpacity(Plan.Kind == EFigmaWidgetKind::Image ? 1.f : Node.Opacity);
	Outer->SetRenderTransformAngle(Node.Rotation);

	if (Plan.Kind == EFigmaWidgetKind::Container || Plan.Kind == EFigmaWidgetKind::Button)
	{
		Named->SetClipping(NeedsClipping(Node) ? EWidgetClipping::ClipToBounds : EWidgetClipping::Inherit);
	}
}

// ----------------------------------------------------------------------------------------------------------------
// Removal and cleanup

bool FFigmaWidgetBuilder::IsAbsorbed(const FString& NodeId, const TSet<FString>& FigmaIds) const
{
	// Still in the design but not built: its parent is now a single widget (a component leaf).
	if (FigmaIds.Contains(NodeId))
	{
		return true;
	}
	// Gone from the bundle because an ancestor is now rasterized ([img] exports no children).
	const FFigmaSyncNodeRecord* Record = State ? State->Nodes.Find(NodeId) : nullptr;
	for (int32 Guard = 0; Record && !Record->ParentId.IsEmpty() && Guard < 1024; ++Guard)
	{
		if (ChildlessIds.Contains(Record->ParentId))
		{
			return true;
		}
		if (NewRecords.Contains(Record->ParentId))
		{
			return false; // The parent was rebuilt with its children: this layer really was deleted.
		}
		Record = State->Nodes.Find(Record->ParentId);
	}
	return false;
}

void FFigmaWidgetBuilder::HandleRemovedNodes(const TSet<FString>& FigmaIds)
{
	if (!State)
	{
		return;
	}
	for (const TPair<FString, FFigmaSyncNodeRecord>& Pair : State->Nodes)
	{
		if (NewRecords.Contains(Pair.Key))
		{
			continue;
		}
		if (LockedIds.Contains(Pair.Key))
		{
			NewRecords.Add(Pair.Key, Pair.Value); // Under a children lock: left exactly as it was.
			continue;
		}
		const FFigmaSyncNodeRecord& Old = Pair.Value;
		UWidget* Widget = FindWidgetByName(Old.Widget);
		if (!Widget || PendingDelete.Contains(Widget))
		{
			continue;
		}
		if (IsAbsorbed(Pair.Key, FigmaIds))
		{
			MarkForDelete(Widget);
			for (const TPair<FName, FName>& Helper : Old.Helpers)
			{
				MarkForDelete(FindWidgetByName(Helper.Value));
			}
			Context.Log->AddForPath(EFigmaLogSeverity::Info, EFigmaLogCategory::Import,
				FString::Printf(TEXT("Layer is now drawn by its parent (an image or component); widget '%s' was removed."), *Old.Widget.ToString()), Pair.Key, Old.LayerName);
			continue;
		}
		if (Context.Settings.bKeepRemovedWidgets)
		{
			FFigmaSyncNodeRecord Kept = Old;
			Kept.bRemovedInFigma = true;
			NewRecords.Add(Pair.Key, Kept);
			Context.Stats.WidgetsKept++;
			Context.Log->AddForPath(EFigmaLogSeverity::Warning, EFigmaLogCategory::Lint,
				FString::Printf(TEXT("Layer was deleted in Figma; widget '%s' was kept. Delete it in Unreal when you no longer need it."), *Old.Widget.ToString()),
				Pair.Key, Old.LayerName);
		}
		else
		{
			MarkForDelete(Widget);
			for (const TPair<FName, FName>& Helper : Old.Helpers)
			{
				MarkForDelete(FindWidgetByName(Helper.Value));
			}
			Context.Stats.WidgetsRemoved++;
			Context.Log->AddForPath(EFigmaLogSeverity::Info, EFigmaLogCategory::Import,
				FString::Printf(TEXT("Layer was deleted in Figma; widget '%s' was removed."), *Old.Widget.ToString()), Pair.Key, Old.LayerName);
		}
	}
}

void FFigmaWidgetBuilder::DeletePending()
{
	TSet<UWidget*> Doomed;
	for (const TWeakObjectPtr<UWidget>& Weak : PendingDelete)
	{
		if (UWidget* Widget = Weak.Get())
		{
			Doomed.Add(Widget);
		}
	}

	// Kept widgets of deleted layers inside a doomed panel can't survive without it: doom them too.
	bool bGrew = true;
	while (bGrew)
	{
		bGrew = false;
		for (UWidget* Widget : Doomed.Array())
		{
			if (UPanelWidget* Panel = Cast<UPanelWidget>(Widget))
			{
				for (UWidget* Child : Panel->GetAllChildren())
				{
					if (Child && !Doomed.Contains(Child) && IsKnownManaged(Child->GetFName()) && RemovedNames.Contains(Child->GetFName()))
					{
						Doomed.Add(Child);
						bGrew = true;
					}
				}
			}
		}
	}

	TArray<UWidget*> Ordered = Doomed.Array();
	Ordered.Sort([](const UWidget& A, const UWidget& B) { return DepthOf(&A) > DepthOf(&B); });
	for (UWidget* Widget : Ordered)
	{
		if (UPanelWidget* Panel = Cast<UPanelWidget>(Widget))
		{
			bool bHasSurvivors = false;
			for (UWidget* Child : Panel->GetAllChildren())
			{
				if (Child && !Doomed.Contains(Child))
				{
					bHasSurvivors = true;
					break;
				}
			}
			if (bHasSurvivors)
			{
				// It stays, so its doomed parents must stay too (removing them would orphan it).
				Doomed.Remove(Widget);
				Context.Log->Warning(FString::Printf(TEXT("Kept '%s' because it still holds widgets added by hand. Move them out and delete it."), *Widget->GetName()));
				continue;
			}
		}
		const FName Name = Widget->GetFName();
		ModifyForReparent(Widget);
		Tree->RemoveWidget(Widget);
		Widget->Rename(nullptr, GetTransientPackage(), REN_DontCreateRedirectors);
		NamesFreed.Add(Name);
	}
	PendingDelete.Reset();
}

void FFigmaWidgetBuilder::SyncVariableGuids()
{
#if !UE_VERSION_OLDER_THAN(5, 6, 0)
	// UE 5.6 tracks widget variables by GUID; keep the map in step with what we created, renamed and deleted.
	for (const FName& Name : NamesTaken)
	{
		if (FindWidgetByName(Name) && !Blueprint->WidgetVariableNameToGuidMap.Contains(Name))
		{
			Blueprint->OnVariableAdded(Name);
		}
	}
	for (const FName& Name : NamesFreed)
	{
		if (!FindWidgetByName(Name) && Blueprint->WidgetVariableNameToGuidMap.Contains(Name))
		{
			Blueprint->OnVariableRemoved(Name);
		}
	}
#endif
}
