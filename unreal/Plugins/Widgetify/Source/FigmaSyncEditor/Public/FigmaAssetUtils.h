#pragma once

#include "CoreMinimal.h"
#include "UObject/WeakObjectPtrTemplates.h"

class FFigmaImportLog;
class UBlueprint;
class UPackage;
class UWidgetBlueprint;

/** Asset creation, lookup, compiling and saving helpers shared by the importer and mappers. */
struct FIGMASYNCEDITOR_API FFigmaAssetUtils
{
	/** "/Game/UI" + "WBP_Menu" -> "/Game/UI/WBP_Menu.WBP_Menu" */
	static FString MakeObjectPath(const FString& PackagePath, const FString& AssetName);

	/** Loads an existing asset of the expected class, or returns null (no warnings when it doesn't exist). */
	static UObject* FindAsset(const FString& PackagePath, const FString& AssetName, const UClass* ExpectedClass);
	static bool DoesAssetExist(const FString& PackagePath, const FString& AssetName);
	static FString MakeUniqueAssetName(const FString& PackagePath, const FString& BaseName);

	/** Turns user input ("Game/UI/", "/Game/UI//Menus") into a clean long package path, or empty when invalid. */
	static FString SanitizePackagePath(const FString& InPath);

	/** Creates a Widget Blueprint through the UMG factory. The new blueprint has no root widget yet. */
	static UWidgetBlueprint* CreateWidgetBlueprint(const FString& PackagePath, const FString& AssetName, UClass* ParentClass, FString& OutError);

	/** Creates a plain Blueprint class (e.g. a CommonUI style) deriving from ParentClass. */
	static UBlueprint* CreateBlueprintClass(const FString& PackagePath, const FString& AssetName, UClass* ParentClass, FString& OutError);

	/** Compiles a blueprint; compiler errors and warnings go to Log. Returns false on errors. */
	static bool CompileBlueprint(UBlueprint* Blueprint, FFigmaImportLog* Log);

	static bool SavePackages(const TSet<TWeakObjectPtr<UPackage>>& Packages);
};
