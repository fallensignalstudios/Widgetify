#pragma once

#include "CoreMinimal.h"
#include "FigmaDocument.h"
#include "FigmaSyncBlueprintExtension.h" // NewRecords holds FFigmaSyncNodeRecord by value: it must be complete here.
#include "Layout/Margin.h"
#include "Types/SlateEnums.h"
#include "UObject/WeakObjectPtrTemplates.h"

class FFigmaImportContext;
class IFigmaWidgetMapper;
class UButton;
class UCanvasPanel;
class UFigmaSyncBlueprintExtension;
class UPanelWidget;
class UWidget;
class UWidgetBlueprint;
class UWidgetTree;
struct FButtonStyle;
struct FSlateBrush;

/** How a Figma node turns into widgets. */
enum class EFigmaWidgetKind : uint8
{
	/** Rasterized PNG -> UImage. */
	Image,
	/** Rectangle, ellipse or line with a simple style -> UImage with a rounded-box brush. */
	Shape,
	/** UTextBlock (or a mapper's text class). */
	Text,
	/** UButton: the frame's look becomes the button style, its children the content. */
	Button,
	/** A mapper-chosen widget class (e.g. a generated CommonUI button); children are not built. */
	Leaf,
	/** Canvas / horizontal / vertical / wrap / overlay panel, inside a Border when it has a look of its own. */
	Container,
};

struct FIGMASYNCEDITOR_API FFigmaBuildOptions
{
	/** Building a reusable component (e.g. a CommonUI button's content): the root's background and padding belong to the parent class. */
	bool bComponentContent = false;

	/** Package folder for textures this build imports. Empty = the screen's texture folder. */
	FString TextureFolder;
};

/**
 * Builds or updates a Widget Blueprint's widget tree from a Figma node tree.
 *
 * Re-sync rules:
 *  - Widgets are matched by Figma node id (stored in UFigmaSyncBlueprintExtension), never by position.
 *  - Look properties are updated in place; graphs, bindings and animations are never touched.
 *  - Widgets added by hand stay where they are (after the Figma children of their panel).
 *  - Layers deleted in Figma keep their widget (flagged in the lint report) unless the project setting says otherwise.
 *  - The lock list skips chosen widgets or properties.
 */
class FIGMASYNCEDITOR_API FFigmaWidgetBuilder
{
public:
	FFigmaWidgetBuilder(FFigmaImportContext& InContext, UWidgetBlueprint* InBlueprint, UFigmaSyncBlueprintExtension* InState, const FFigmaBuildOptions& InOptions = FFigmaBuildOptions());

	/** Builds or updates the tree from Root. Returns false on fatal errors (details in the context log). */
	bool Build(const FFigmaNode& Root);

	/** What a node becomes, e.g. "Border + VerticalBox", for the preview tree. */
	static FString Describe(const FFigmaNode& Node, FFigmaImportContext& InContext);

	/** Kind of widget for a node; OutLeafClass/OutMapper are set for mapper-chosen leaves. */
	static EFigmaWidgetKind Classify(const FFigmaNode& Node, FFigmaImportContext& InContext, UClass** OutLeafClass = nullptr, IFigmaWidgetMapper** OutMapper = nullptr);

	/** Panel class for a container's children (Canvas, HorizontalBox, VerticalBox, WrapBox, Overlay). */
	static UClass* GetPanelClass(const FFigmaNode& Node);

	/** Background brush for a node: its rasterized background PNG when one was exported, else its rounded-box brush. */
	static FSlateBrush MakeNodeBackgroundBrush(const FFigmaNode& Node, FFigmaImportContext& InContext, const FString& TextureFolder, bool* bOutOverflowClipped = nullptr);

private:
	enum class EParentKind : uint8 { Root, Canvas, HBox, VBox, Wrap, Overlay, Content };

	struct FParentInfo
	{
		EParentKind Kind = EParentKind::Root;
		const FFigmaNode* Node = nullptr;
		bool bFoldPadding = false;
		int32 Index = 0;
		int32 Count = 1;
	};

	struct FPlan
	{
		EFigmaWidgetKind Kind = EFigmaWidgetKind::Container;
		UClass* NamedClass = nullptr;
		IFigmaWidgetMapper* Mapper = nullptr;
		/** Panel that holds the children (the Layout helper, or the named widget when there is no Border). */
		UClass* PanelClass = nullptr;
		/** For auto-layout frames with absolutely positioned children: the flow box inside the Layout overlay. */
		UClass* FlowClass = nullptr;
		bool bBorder = false;
		bool bBgBrush = false;
		bool bBgOverlay = false;
		bool bAbsLayer = false;
		bool bFoldPadding = false;
		bool bSizeBox = false;
		bool bSizeW = false;
		bool bSizeH = false;
		EHorizontalAlignment SizeHAlign = HAlign_Fill;
		EVerticalAlignment SizeVAlign = VAlign_Fill;
	};

	struct FLocks
	{
		bool bLayout = false;
		bool bAppearance = false;
		bool bText = false;
		bool bChildren = false;
	};

	UWidget* BuildNode(const FFigmaNode& Node, const FParentInfo& Parent, bool bIsRoot);
	FPlan MakePlan(const FFigmaNode& Node, const FParentInfo& Parent, bool bIsRoot);
	void ComputeSizing(const FFigmaNode& Node, const FParentInfo& Parent, bool bIsRoot, FPlan& Plan) const;
	FLocks GetLocks(const FFigmaSyncNodeRecord& Record) const;

	UWidget* EnsureNamedWidget(const FFigmaNode& Node, FFigmaSyncNodeRecord& Record, const FPlan& Plan, bool& bOutCreated);
	UWidget* EnsureHelper(FFigmaSyncNodeRecord& Record, FName Role, const FString& DesiredName, UClass* Class);
	void RetireHelper(FFigmaSyncNodeRecord& Record, FName Role);
	UWidget* TryRelink(const FFigmaNode& Node, FFigmaSyncNodeRecord& Record);

	void BuildContainer(const FFigmaNode& Node, FFigmaSyncNodeRecord& Record, const FPlan& Plan, UWidget* Named, const FLocks& Locks);
	void BuildButton(const FFigmaNode& Node, FFigmaSyncNodeRecord& Record, const FPlan& Plan, UButton* Button, const FLocks& Locks);
	void BuildChildren(const FFigmaNode& Node, FFigmaSyncNodeRecord& Record, const FPlan& Plan, UPanelWidget* FlowPanel, UCanvasPanel* AbsPanel, const FLocks& Locks);

	void ConfigureImage(UWidget* Widget, const FFigmaNode& Node);
	void ConfigureShape(UWidget* Widget, const FFigmaNode& Node);
	void ConfigureText(UWidget* Widget, const FFigmaNode& Node, const FParentInfo& Parent, const FLocks& Locks);
	void ConfigureSizeBox(UWidget* SizeBox, const FFigmaNode& Node, const FPlan& Plan);
	void ConfigureChildSlot(UWidget* Child, const FFigmaNode& Node, const FParentInfo& Parent);
	void ApplyCommon(UWidget* Outer, UWidget* Named, const FFigmaNode& Node, const FPlan& Plan, const FLocks& Locks);
	void ApplyPanelSettings(UPanelWidget* Panel, const FFigmaNode& Node);

	FSlateBrush MakeBackgroundBrush(const FFigmaNode& Node, bool& bOutOverflowClipped);
	FButtonStyle MakeButtonStyle(const FFigmaNode& Node, const FButtonStyle& Base);
	FName GetTexturePreset(const FFigmaNode& Node) const;
	bool NeedsClipping(const FFigmaNode& Node) const;
	void GetContentAlignment(const FFigmaNode& Node, EHorizontalAlignment& OutH, EVerticalAlignment& OutV) const;
	static EParentKind KindOfPanel(const UPanelWidget* Panel);

	void ReconcileChildren(UPanelWidget* Panel, const TArray<UWidget*>& Desired);
	void PlaceChild(UPanelWidget* Panel, UWidget* Child, int32 Index);
	void SetSingleContent(UPanelWidget* ContentWidget, UWidget* Child);

	UWidget* FindWidgetByName(FName Name) const;
	UWidget* ConstructWidgetOfClass(UClass* Class, FName Name);
	FName ClaimName(const FString& Desired);
	bool IsNameFree(FName Name) const;
	void RenameWidget(UWidget* Widget, FName NewName);
	void MarkForDelete(UWidget* Widget);
	void DeletePending();
	/** FigmaIds: every node id in the bundle (built or not). */
	void HandleRemovedNodes(const TSet<FString>& FigmaIds);
	/** True when a layer wasn't built because its parent became a single widget (image, component leaf). */
	bool IsAbsorbed(const FString& NodeId, const TSet<FString>& FigmaIds) const;
	void SyncVariableGuids();
	bool IsKnownManaged(FName Name) const;

	FFigmaSyncNodeRecord& RecordFor(const FFigmaNode& Node);
	static FFigmaRect GetPlacementBox(const FFigmaNode& Node);
	void TransferLeftovers(const FFigmaNode& Node, UPanelWidget* Target);

	FFigmaImportContext& Context;
	UWidgetBlueprint* Blueprint = nullptr;
	UWidgetTree* Tree = nullptr;
	UFigmaSyncBlueprintExtension* State = nullptr;
	FFigmaBuildOptions Options;

	TMap<FString, FFigmaSyncNodeRecord> NewRecords;
	TSet<FName> ManagedBefore;
	TSet<FName> ReservedNames;
	TSet<FName> NamesTaken;
	TSet<FName> NamesFreed;
	TSet<FName> RemovedNames;
	TArray<TWeakObjectPtr<UWidget>> PendingDelete;
	/** Nodes under a children lock: not built, their records carried over as they were. */
	TSet<FString> LockedIds;
	/** Nodes built as a single widget (image, shape, text, leaf): nothing is built under them. */
	TSet<FString> ChildlessIds;
	/** Named widgets replaced because their class changed, keyed by node id; leftover hand-added children move to the new panel. */
	TMap<FString, TWeakObjectPtr<UWidget>> ReplacedWidgets;
};
