// Automation tests for FigmaSyncCore. Run them from Tools > Session Frontend > Automation, filter "FigmaSync".

#include "FigmaBundle.h"
#include "FigmaDocument.h"
#include "FigmaDocumentParser.h"
#include "FigmaNaming.h"
#include "FigmaZipReader.h"
#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

namespace FigmaSyncTests
{
	// Written by the Widgetify Figma plugin's zip writer (stored entries): figmasync.json + images/a.png = {1,2,3}.
	const uint8 StoredZip[] = {
	0x50, 0x4B, 0x03, 0x04, 0x14, 0x00, 0x00, 0x08, 0x00, 0x00, 0x00, 0x60, 0x4D, 0x5D, 0x32, 0x70, 0x35, 0xD4, 0x18, 0x01,
	0x00, 0x00, 0x18, 0x01, 0x00, 0x00, 0x0E, 0x00, 0x00, 0x00, 0x66, 0x69, 0x67, 0x6D, 0x61, 0x73, 0x79, 0x6E, 0x63, 0x2E,
	0x6A, 0x73, 0x6F, 0x6E, 0x7B, 0x22, 0x66, 0x6F, 0x72, 0x6D, 0x61, 0x74, 0x22, 0x3A, 0x22, 0x66, 0x69, 0x67, 0x6D, 0x61,
	0x73, 0x79, 0x6E, 0x63, 0x22, 0x2C, 0x22, 0x76, 0x65, 0x72, 0x73, 0x69, 0x6F, 0x6E, 0x22, 0x3A, 0x31, 0x2C, 0x22, 0x73,
	0x6F, 0x75, 0x72, 0x63, 0x65, 0x22, 0x3A, 0x7B, 0x22, 0x66, 0x72, 0x61, 0x6D, 0x65, 0x4E, 0x61, 0x6D, 0x65, 0x22, 0x3A,
	0x22, 0x52, 0x6F, 0x6F, 0x74, 0x22, 0x7D, 0x2C, 0x22, 0x72, 0x6F, 0x6F, 0x74, 0x22, 0x3A, 0x7B, 0x22, 0x69, 0x64, 0x22,
	0x3A, 0x22, 0x31, 0x3A, 0x31, 0x22, 0x2C, 0x22, 0x6E, 0x61, 0x6D, 0x65, 0x22, 0x3A, 0x22, 0x52, 0x6F, 0x6F, 0x74, 0x22,
	0x2C, 0x22, 0x63, 0x6C, 0x65, 0x61, 0x6E, 0x4E, 0x61, 0x6D, 0x65, 0x22, 0x3A, 0x22, 0x52, 0x6F, 0x6F, 0x74, 0x22, 0x2C,
	0x22, 0x74, 0x79, 0x70, 0x65, 0x22, 0x3A, 0x22, 0x46, 0x52, 0x41, 0x4D, 0x45, 0x22, 0x2C, 0x22, 0x78, 0x22, 0x3A, 0x30,
	0x2C, 0x22, 0x79, 0x22, 0x3A, 0x30, 0x2C, 0x22, 0x77, 0x69, 0x64, 0x74, 0x68, 0x22, 0x3A, 0x31, 0x30, 0x2C, 0x22, 0x68,
	0x65, 0x69, 0x67, 0x68, 0x74, 0x22, 0x3A, 0x31, 0x30, 0x2C, 0x22, 0x63, 0x68, 0x69, 0x6C, 0x64, 0x72, 0x65, 0x6E, 0x22,
	0x3A, 0x5B, 0x5D, 0x7D, 0x2C, 0x22, 0x61, 0x73, 0x73, 0x65, 0x74, 0x73, 0x22, 0x3A, 0x5B, 0x7B, 0x22, 0x69, 0x64, 0x22,
	0x3A, 0x22, 0x69, 0x6D, 0x67, 0x31, 0x22, 0x2C, 0x22, 0x70, 0x61, 0x74, 0x68, 0x22, 0x3A, 0x22, 0x69, 0x6D, 0x61, 0x67,
	0x65, 0x73, 0x2F, 0x61, 0x2E, 0x70, 0x6E, 0x67, 0x22, 0x2C, 0x22, 0x77, 0x69, 0x64, 0x74, 0x68, 0x22, 0x3A, 0x31, 0x2C,
	0x22, 0x68, 0x65, 0x69, 0x67, 0x68, 0x74, 0x22, 0x3A, 0x31, 0x2C, 0x22, 0x73, 0x63, 0x61, 0x6C, 0x65, 0x22, 0x3A, 0x31,
	0x2C, 0x22, 0x68, 0x61, 0x73, 0x68, 0x22, 0x3A, 0x22, 0x78, 0x22, 0x2C, 0x22, 0x62, 0x79, 0x74, 0x65, 0x73, 0x22, 0x3A,
	0x33, 0x7D, 0x5D, 0x7D, 0x50, 0x4B, 0x03, 0x04, 0x14, 0x00, 0x00, 0x08, 0x00, 0x00, 0x00, 0x60, 0x4D, 0x5D, 0x1D, 0x80,
	0xBC, 0x55, 0x03, 0x00, 0x00, 0x00, 0x03, 0x00, 0x00, 0x00, 0x0C, 0x00, 0x00, 0x00, 0x69, 0x6D, 0x61, 0x67, 0x65, 0x73,
	0x2F, 0x61, 0x2E, 0x70, 0x6E, 0x67, 0x01, 0x02, 0x03, 0x50, 0x4B, 0x01, 0x02, 0x14, 0x00, 0x14, 0x00, 0x00, 0x08, 0x00,
	0x00, 0x00, 0x60, 0x4D, 0x5D, 0x32, 0x70, 0x35, 0xD4, 0x18, 0x01, 0x00, 0x00, 0x18, 0x01, 0x00, 0x00, 0x0E, 0x00, 0x00,
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x66, 0x69, 0x67, 0x6D, 0x61,
	0x73, 0x79, 0x6E, 0x63, 0x2E, 0x6A, 0x73, 0x6F, 0x6E, 0x50, 0x4B, 0x01, 0x02, 0x14, 0x00, 0x14, 0x00, 0x00, 0x08, 0x00,
	0x00, 0x00, 0x60, 0x4D, 0x5D, 0x1D, 0x80, 0xBC, 0x55, 0x03, 0x00, 0x00, 0x00, 0x03, 0x00, 0x00, 0x00, 0x0C, 0x00, 0x00,
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x44, 0x01, 0x00, 0x00, 0x69, 0x6D, 0x61, 0x67, 0x65,
	0x73, 0x2F, 0x61, 0x2E, 0x70, 0x6E, 0x67, 0x50, 0x4B, 0x05, 0x06, 0x00, 0x00, 0x00, 0x00, 0x02, 0x00, 0x02, 0x00, 0x76,
	0x00, 0x00, 0x00, 0x71, 0x01, 0x00, 0x00, 0x00, 0x00,
	};

	// Written by Python's zipfile with ZIP_DEFLATED, wrapping the bundle in a "Bundle/" folder.
	const uint8 DeflatedZip[] = {
	0x50, 0x4B, 0x03, 0x04, 0x14, 0x00, 0x00, 0x00, 0x08, 0x00, 0x00, 0x60, 0x4D, 0x5D, 0xE9, 0xD2, 0x50, 0xA6, 0x7B, 0x00,
	0x00, 0x00, 0x9D, 0x00, 0x00, 0x00, 0x15, 0x00, 0x00, 0x00, 0x42, 0x75, 0x6E, 0x64, 0x6C, 0x65, 0x2F, 0x66, 0x69, 0x67,
	0x6D, 0x61, 0x73, 0x79, 0x6E, 0x63, 0x2E, 0x6A, 0x73, 0x6F, 0x6E, 0x65, 0x8D, 0xBD, 0x0A, 0x02, 0x31, 0x10, 0x84, 0xDF,
	0x65, 0xEA, 0x14, 0x0A, 0x36, 0x97, 0x4E, 0x50, 0x3B, 0x2D, 0x6C, 0xC5, 0x22, 0x24, 0x9B, 0xCB, 0x42, 0x7E, 0x24, 0x17,
	0x4E, 0xC3, 0x71, 0xEF, 0xEE, 0x7A, 0xAD, 0xCD, 0x30, 0xF3, 0xCD, 0xC0, 0x2C, 0xF0, 0xA5, 0x26, 0xD3, 0xA0, 0xE1, 0x79,
	0x4C, 0x66, 0xEA, 0xD9, 0x42, 0x61, 0xA6, 0x3A, 0x71, 0xC9, 0xD0, 0x7B, 0x85, 0x5A, 0x8A, 0xD4, 0x0B, 0xD8, 0xC9, 0x68,
	0xD0, 0x83, 0xD4, 0xD9, 0x24, 0x92, 0x70, 0x22, 0x1F, 0x4D, 0x23, 0x27, 0xC4, 0x46, 0x32, 0xF9, 0xF6, 0x87, 0x5B, 0x7F,
	0xFD, 0xC8, 0xE5, 0x7E, 0xBC, 0x9E, 0x25, 0x7E, 0xA0, 0x77, 0x0A, 0x7D, 0xD3, 0x37, 0xBB, 0x16, 0xA0, 0x0F, 0x0A, 0x81,
	0x78, 0x0C, 0x6D, 0xB3, 0x36, 0x70, 0x74, 0x95, 0xE4, 0xF8, 0xF1, 0x5C, 0xD7, 0x2F, 0x50, 0x4B, 0x01, 0x02, 0x14, 0x03,
	0x14, 0x00, 0x00, 0x00, 0x08, 0x00, 0x00, 0x60, 0x4D, 0x5D, 0xE9, 0xD2, 0x50, 0xA6, 0x7B, 0x00, 0x00, 0x00, 0x9D, 0x00,
	0x00, 0x00, 0x15, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x80, 0x01, 0x00, 0x00, 0x00, 0x00,
	0x42, 0x75, 0x6E, 0x64, 0x6C, 0x65, 0x2F, 0x66, 0x69, 0x67, 0x6D, 0x61, 0x73, 0x79, 0x6E, 0x63, 0x2E, 0x6A, 0x73, 0x6F,
	0x6E, 0x50, 0x4B, 0x05, 0x06, 0x00, 0x00, 0x00, 0x00, 0x01, 0x00, 0x01, 0x00, 0x43, 0x00, 0x00, 0x00, 0xAE, 0x00, 0x00,
	0x00, 0x00, 0x00,
	};

	TArray<uint8> ToArray(const uint8* Data, int32 Num)
	{
		return TArray<uint8>(Data, Num);
	}

	FString FullDocument()
	{
		return FString(UTF8_TO_TCHAR(R"JSON({
			"format": "figmasync",
			"version": 1,
			"generator": { "name": "widgetify-figma", "version": "0.1.0" },
			"source": { "fileName": "UI", "pageName": "Menus", "frameId": "1:1", "frameName": "MainMenu" },
			"options": { "scale": 2, "includeHidden": true, "rasterizeUnsupported": true, "variantPrefixes": ["Btn/"] },
			"root": {
				"id": "1:1", "name": "MainMenu", "cleanName": "MainMenu", "tags": {}, "type": "FRAME",
				"visible": true, "opacity": 1, "rotation": 0, "x": 0, "y": 0, "width": 1920, "height": 1080,
				"style": { "fills": [{ "type": "SOLID", "visible": true, "opacity": 1, "blendMode": "NORMAL", "color": { "r": 1, "g": 0.5, "b": 0, "a": 1 } }],
				           "strokes": [], "strokeWeight": 0, "strokeAlign": "INSIDE", "dashed": false, "cornerRadius": [1, 2, 3, 4],
				           "cornerSmoothing": 0, "effects": [], "blendMode": "PASS_THROUGH", "simple": true },
				"children": [
					{
						"id": "2:1", "name": "Pnl_Menu", "cleanName": "Pnl_Menu", "tags": {}, "type": "FRAME", "visible": true,
						"opacity": 0.5, "rotation": -15, "x": 760, "y": 420, "width": 400, "height": 368,
						"constraints": { "horizontal": "CENTER", "vertical": "STRETCH" },
						"layout": { "mode": "VERTICAL", "wrap": false, "padding": { "left": 1, "right": 2, "top": 3, "bottom": 4 },
						            "itemSpacing": 16, "counterAxisSpacing": 0, "primaryAlign": "SPACE_BETWEEN", "counterAlign": "CENTER", "reverseZIndex": false },
						"background": { "asset": "img1", "bounds": { "x": -24, "y": -16, "width": 448, "height": 416 }, "reason": "effects" },
						"children": [
							{
								"id": "I2:2;1:5", "name": "[focus] Btn_Play", "cleanName": "Btn_Play", "tags": { "focus": "" }, "type": "INSTANCE",
								"visible": true, "opacity": 1, "rotation": 0, "x": 32, "y": 32, "width": 336, "height": 64,
								"child": { "positioning": "AUTO", "sizingH": "FILL", "sizingV": "FIXED", "grow": 0, "alignSelf": "INHERIT" },
								"component": { "kind": "INSTANCE", "componentId": "1:5", "componentName": "State=Default", "componentSetId": "1:4",
								               "componentSetName": "Btn/Primary", "variant": { "State": "Default" },
								               "properties": { "Label": { "type": "TEXT", "value": "Play" }, "Icon": { "type": "BOOLEAN", "value": true } } },
								"children": [
									{
										"id": "I2:2;1:6", "name": "Label", "cleanName": "Label", "tags": {}, "type": "TEXT", "visible": true, "opacity": 1,
										"rotation": 0, "x": 0, "y": 0, "width": 120, "height": 32, "propRefs": { "characters": "Label" },
										"text": { "characters": "Play", "fontFamily": "Cinzel", "fontStyle": "SemiBold", "fontWeight": 600, "italic": false,
										          "fontSize": 24, "color": { "r": 0.9, "g": 0.8, "b": 0.7, "a": 1 }, "alignH": "CENTER", "alignV": "CENTER",
										          "autoResize": "WIDTH_AND_HEIGHT", "truncate": false, "maxLines": 0,
										          "letterSpacing": { "value": 10, "unit": "PERCENT" }, "lineHeight": { "value": 0, "unit": "AUTO" },
										          "textCase": "UPPER", "decoration": "NONE", "mixed": false }
									}
								]
							}
						]
					}
				]
			},
			"components": {
				"1:4": { "id": "1:4", "name": "Btn/Primary", "remote": false, "propertyNames": ["State"],
				         "variants": [ { "id": "1:5", "name": "State=Default", "properties": { "State": "Default" },
				                         "node": { "id": "1:5", "name": "State=Default", "cleanName": "State=Default", "type": "COMPONENT",
				                                   "x": 0, "y": 0, "width": 400, "height": 64, "children": [] } } ] }
			},
			"assets": [ { "id": "img1", "path": "images/Pnl_Menu_Bg.png", "width": 896, "height": 832, "scale": 2, "hash": "abc", "bytes": 1234 } ],
			"warnings": [ { "nodeId": "2:1", "nodePath": "MainMenu/Pnl_Menu", "message": "hello" } ]
		})JSON"));
	}

	/** Index of the last occurrence of {1,2,3} (the image payload), or INDEX_NONE. */
	int32 FindPayload(const TArray<uint8>& Bytes)
	{
		for (int32 Index = Bytes.Num() - 3; Index >= 0; --Index)
		{
			if (Bytes[Index] == 1 && Bytes[Index + 1] == 2 && Bytes[Index + 2] == 3)
			{
				return Index;
			}
		}
		return INDEX_NONE;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFigmaSyncZipStoredTest, "FigmaSync.Core.Zip.Stored", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FFigmaSyncZipStoredTest::RunTest(const FString& Parameters)
{
	using namespace FigmaSyncTests;
	FFigmaZipReader Reader;
	FString Error;
	TestTrue(TEXT("Opens"), Reader.Open(ToArray(StoredZip, UE_ARRAY_COUNT(StoredZip)), Error));
	TestEqual(TEXT("Two entries"), Reader.GetEntryNames().Num(), 2);
	TestTrue(TEXT("Case-insensitive lookup"), Reader.Contains(TEXT("IMAGES/A.PNG")));

	TArray<uint8> Image;
	TestTrue(TEXT("Reads image"), Reader.Read(TEXT("images/a.png"), Image, Error));
	TestTrue(TEXT("Image bytes"), Image == TArray<uint8>({ 1, 2, 3 }));

	TSharedPtr<FFigmaBundle> Bundle = FFigmaBundle::OpenFromZipBytes(ToArray(StoredZip, UE_ARRAY_COUNT(StoredZip)), TEXT("stored"), Error);
	if (!TestTrue(FString::Printf(TEXT("Bundle opens (%s)"), *Error), Bundle.IsValid()))
	{
		return false;
	}
	TestEqual(TEXT("Root parsed"), Bundle->GetDocument().Root->CleanName, FString(TEXT("Root")));
	TestTrue(TEXT("Asset readable through the bundle"), Bundle->ReadAsset(Bundle->GetDocument().Assets[0], Image, Error));
	TestFalse(TEXT("Path traversal rejected"), Bundle->ReadFile(TEXT("../secret.txt"), Image, Error));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFigmaSyncZipDeflatedTest, "FigmaSync.Core.Zip.DeflatedAndNested", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FFigmaSyncZipDeflatedTest::RunTest(const FString& Parameters)
{
	using namespace FigmaSyncTests;
	FString Error;
	TSharedPtr<FFigmaBundle> Bundle = FFigmaBundle::OpenFromZipBytes(ToArray(DeflatedZip, UE_ARRAY_COUNT(DeflatedZip)), TEXT("deflated"), Error);
	if (!TestTrue(FString::Printf(TEXT("Deflated, folder-wrapped bundle opens (%s)"), *Error), Bundle.IsValid()))
	{
		return false;
	}
	TestEqual(TEXT("Root id"), Bundle->GetDocument().Root->Id, FString(TEXT("9:9")));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFigmaSyncZipCorruptTest, "FigmaSync.Core.Zip.Corrupt", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FFigmaSyncZipCorruptTest::RunTest(const FString& Parameters)
{
	using namespace FigmaSyncTests;
	FString Error;
	FFigmaZipReader Garbage;
	TestFalse(TEXT("Garbage is rejected"), Garbage.Open(TArray<uint8>({ 1, 2, 3, 4, 5 }), Error));

	TArray<uint8> Bytes = ToArray(StoredZip, UE_ARRAY_COUNT(StoredZip));
	// Corrupt the image payload ({1,2,3}); its CRC must no longer match.
	const int32 PayloadIndex = FindPayload(Bytes);
	if (TestTrue(TEXT("Found payload"), PayloadIndex != INDEX_NONE))
	{
		Bytes[PayloadIndex + 2] = 9;
	}
	FFigmaZipReader Reader;
	TestTrue(TEXT("Still opens"), Reader.Open(MoveTemp(Bytes), Error));
	TArray<uint8> Image;
	TestFalse(TEXT("CRC mismatch is reported"), Reader.Read(TEXT("images/a.png"), Image, Error));
	TestTrue(TEXT("Standard CRC-32 check value"), FFigmaZipReader::Crc32(reinterpret_cast<const uint8*>("123456789"), 9) == 0xCBF43926u);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFigmaSyncParserTest, "FigmaSync.Core.Parser", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FFigmaSyncParserTest::RunTest(const FString& Parameters)
{
	FFigmaDocument Doc;
	FString Error;
	if (!TestTrue(TEXT("Parses"), FFigmaDocumentParser::Parse(FigmaSyncTests::FullDocument(), Doc, Error)))
	{
		AddError(Error);
		return false;
	}
	TestEqual(TEXT("Frame name"), Doc.FrameName, FString(TEXT("MainMenu")));
	TestEqual(TEXT("Node count"), Doc.CountNodes(), 4);
	TestTrue(TEXT("Corner radii order"), Doc.Root->Style.CornerRadius == FVector4(1, 2, 3, 4));
	TestTrue(TEXT("Simple style"), Doc.Root->Style.bSimple);

	const FFigmaNode* Menu = Doc.FindNodeById(TEXT("2:1"));
	if (!TestNotNull(TEXT("Menu"), Menu))
	{
		return false;
	}
	TestTrue(TEXT("Auto layout"), Menu->IsAutoLayout());
	TestTrue(TEXT("Vertical"), Menu->Layout.Mode == EFigmaLayoutMode::Vertical);
	TestTrue(TEXT("Space between"), Menu->Layout.PrimaryAlign == EFigmaAlign::SpaceBetween);
	TestEqual(TEXT("Padding bottom"), Menu->Layout.Padding.Bottom, 4.f);
	TestTrue(TEXT("Constraint V"), Menu->ConstraintV == EFigmaConstraint::Stretch);
	TestEqual(TEXT("Rotation"), Menu->Rotation, -15.f);
	TestEqual(TEXT("Background bounds"), Menu->Background.Bounds.X, -24.f);

	const FFigmaNode* Play = Doc.FindNodeById(TEXT("I2:2;1:5"));
	if (!TestNotNull(TEXT("Play"), Play))
	{
		return false;
	}
	TestTrue(TEXT("Focus tag"), Play->HasTag(TEXT("focus")));
	TestTrue(TEXT("Fill width"), Play->Child.SizingH == EFigmaSizing::Fill);
	TestEqual(TEXT("Component family"), Play->Component.GetFamilyName(), FString(TEXT("Btn/Primary")));
	TestEqual(TEXT("Label property"), Play->Component.TextProperties.FindRef(TEXT("Label")), FString(TEXT("Play")));
	TestTrue(TEXT("Bool property"), Play->Component.BoolProperties.FindRef(TEXT("Icon")));
	TestTrue(TEXT("Parent pointer"), Play->Parent == Menu);
	TestEqual(TEXT("Path"), Play->GetPath(), FString(TEXT("MainMenu/Pnl_Menu/Btn_Play")));

	const FFigmaNode& Label = *Play->Children[0];
	TestEqual(TEXT("Text"), Label.Text.Characters, FString(TEXT("Play")));
	TestTrue(TEXT("Letter spacing unit"), Label.Text.LetterSpacingUnit == EFigmaUnit::Percent);
	TestTrue(TEXT("Text case"), Label.Text.TextCase == EFigmaTextCase::Upper);
	TestEqual(TEXT("Prop ref"), Label.PropRefs.FindRef(TEXT("characters")), FString(TEXT("Label")));

	const FFigmaComponentSet* Set = Doc.FindComponentSet(TEXT("1:4"));
	TestTrue(TEXT("Component set with one variant"), Set && Set->Variants.Num() == 1);
	TestEqual(TEXT("Asset scale"), Doc.FindAsset(TEXT("img1")) ? Doc.FindAsset(TEXT("img1"))->Scale : 0.f, 2.f);
	TestEqual(TEXT("Warnings"), Doc.Warnings.Num(), 1);

	FFigmaDocument Rejected;
	TestFalse(TEXT("Rejects other JSON"), FFigmaDocumentParser::Parse(TEXT("{\"hello\": 1}"), Rejected, Error));
	TestFalse(TEXT("Rejects newer versions"), FFigmaDocumentParser::Parse(TEXT("{\"format\":\"figmasync\",\"version\":99,\"root\":{}}"), Rejected, Error));
	TestFalse(TEXT("Rejects broken JSON"), FFigmaDocumentParser::Parse(TEXT("{nope"), Rejected, Error));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFigmaSyncColorTest, "FigmaSync.Core.Color", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FFigmaSyncColorTest::RunTest(const FString& Parameters)
{
	FFigmaColor Gray;
	Gray.R = Gray.G = Gray.B = 0.5f;
	const FLinearColor Linear = Gray.ToLinear(0.5f);
	TestTrue(TEXT("sRGB 0.5 -> linear 0.214"), FMath::IsNearlyEqual(Linear.R, 0.214f, 0.001f));
	TestEqual(TEXT("Opacity multiplies alpha"), Linear.A, 0.5f);
	FFigmaColor White;
	White.R = White.G = White.B = 1.f;
	TestEqual(TEXT("White stays white"), White.ToLinear().R, 1.f);
	TestEqual(TEXT("Hex"), White.ToHex(), FString(TEXT("#FFFFFF")));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFigmaSyncNamingTest, "FigmaSync.Core.Naming", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FFigmaSyncNamingTest::RunTest(const FString& Parameters)
{
	TestEqual(TEXT("Keeps valid names"), FFigmaNaming::SanitizeIdentifier(TEXT("Btn_Play")), FString(TEXT("Btn_Play")));
	TestEqual(TEXT("Replaces spaces and symbols"), FFigmaNaming::SanitizeIdentifier(TEXT("Main Menu / Title!")), FString(TEXT("Main_Menu_Title")));
	TestEqual(TEXT("Keeps designer underscores"), FFigmaNaming::SanitizeIdentifier(TEXT("Txt__Name")), FString(TEXT("Txt__Name")));
	TestEqual(TEXT("No leading digit"), FFigmaNaming::SanitizeIdentifier(TEXT("9Slice")), FString(TEXT("W_9Slice")));
	TestEqual(TEXT("Fallback"), FFigmaNaming::SanitizeIdentifier(TEXT("!!!"), TEXT("Image")), FString(TEXT("Image")));
	TestEqual(TEXT("Not None"), FFigmaNaming::SanitizeIdentifier(TEXT("None")), FString(TEXT("None_")));

	TSet<FString> Taken = { TEXT("Ico_Star"), TEXT("Ico_Star_2") };
	TestEqual(TEXT("Unique suffix"), FFigmaNaming::MakeUnique(TEXT("Ico_Star"), [&Taken](const FString& Name) { return Taken.Contains(Name); }), FString(TEXT("Ico_Star_3")));
	TestTrue(TEXT("Prefix match is case-insensitive"), FFigmaNaming::StartsWithAny(TEXT("btn_play"), { TEXT("Btn_") }));
	TestEqual(TEXT("Component path"), FFigmaNaming::PathToIdentifier(TEXT("Btn/Primary")), FString(TEXT("Btn_Primary")));
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
