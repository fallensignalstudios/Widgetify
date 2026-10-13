#include "FigmaAssetUtils.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "AssetRegistry/IAssetRegistry.h"
#include "AssetToolsModule.h"
#include "Blueprint/UserWidget.h"
#include "Engine/Blueprint.h"
#include "Engine/BlueprintGeneratedClass.h"
#include "FigmaImportLog.h"
#include "FigmaNaming.h"
#include "FileHelpers.h"
#include "IAssetTools.h"
#include "Kismet2/CompilerResultsLog.h"
#include "Kismet2/KismetEditorUtilities.h"
#include "Logging/TokenizedMessage.h"
#include "Misc/PackageName.h"
#include "Modules/ModuleManager.h"
#include "UObject/Package.h"
#include "WidgetBlueprint.h"
#include "WidgetBlueprintFactory.h"

FString FFigmaAssetUtils::MakeObjectPath(const FString& PackagePath, const FString& AssetName)
{
	return FString::Printf(TEXT("%s/%s.%s"), *PackagePath, *AssetName, *AssetName);
}

UObject* FFigmaAssetUtils::FindAsset(const FString& PackagePath, const FString& AssetName, const UClass* ExpectedClass)
{
	IAssetRegistry& Registry = FModuleManager::LoadModuleChecked<FAssetRegistryModule>(TEXT("AssetRegistry")).Get();
	const FAssetData Data = Registry.GetAssetByObjectPath(FSoftObjectPath(MakeObjectPath(PackagePath, AssetName)));
	if (!Data.IsValid())
	{
		return nullptr;
	}
	UObject* Asset = Data.GetAsset();
	return Asset && (!ExpectedClass || Asset->IsA(ExpectedClass)) ? Asset : nullptr;
}

bool FFigmaAssetUtils::DoesAssetExist(const FString& PackagePath, const FString& AssetName)
{
	IAssetRegistry& Registry = FModuleManager::LoadModuleChecked<FAssetRegistryModule>(TEXT("AssetRegistry")).Get();
	if (Registry.GetAssetByObjectPath(FSoftObjectPath(MakeObjectPath(PackagePath, AssetName))).IsValid())
	{
		return true;
	}
	// A package created earlier in this session but not saved yet is not always in the registry.
	return FindObject<UPackage>(nullptr, *(PackagePath / AssetName)) != nullptr;
}

FString FFigmaAssetUtils::MakeUniqueAssetName(const FString& PackagePath, const FString& BaseName)
{
	return FFigmaNaming::MakeUnique(BaseName, [&PackagePath](const FString& Candidate) { return DoesAssetExist(PackagePath, Candidate); });
}

FString FFigmaAssetUtils::SanitizePackagePath(const FString& InPath)
{
	FString Path = InPath.TrimStartAndEnd().Replace(TEXT("\\"), TEXT("/"));
	while (Path.Contains(TEXT("//")))
	{
		Path.ReplaceInline(TEXT("//"), TEXT("/"));
	}
	Path.RemoveFromEnd(TEXT("/"));
	if (!Path.StartsWith(TEXT("/")))
	{
		Path = TEXT("/") + Path;
	}
	FText Reason;
	if (!FPackageName::IsValidLongPackageName(Path / TEXT("X"), false, &Reason))
	{
		return FString();
	}
	return Path;
}

UWidgetBlueprint* FFigmaAssetUtils::CreateWidgetBlueprint(const FString& PackagePath, const FString& AssetName, UClass* ParentClass, FString& OutError)
{
	UClass* Parent = ParentClass ? ParentClass : UUserWidget::StaticClass();
	if (!Parent->IsChildOf(UUserWidget::StaticClass()) || !FKismetEditorUtilities::CanCreateBlueprintOfClass(Parent))
	{
		OutError = FString::Printf(TEXT("%s can't be the parent of a Widget Blueprint."), *Parent->GetName());
		return nullptr;
	}

	UWidgetBlueprintFactory* Factory = NewObject<UWidgetBlueprintFactory>();
	Factory->ParentClass = Parent;
	IAssetTools& AssetTools = FModuleManager::LoadModuleChecked<FAssetToolsModule>(TEXT("AssetTools")).Get();
	UWidgetBlueprint* Blueprint = Cast<UWidgetBlueprint>(AssetTools.CreateAsset(AssetName, PackagePath, UWidgetBlueprint::StaticClass(), Factory, FName(TEXT("FigmaSync"))));
	if (!Blueprint)
	{
		OutError = FString::Printf(TEXT("Could not create %s/%s."), *PackagePath, *AssetName);
	}
	return Blueprint;
}

UBlueprint* FFigmaAssetUtils::CreateBlueprintClass(const FString& PackagePath, const FString& AssetName, UClass* ParentClass, FString& OutError)
{
	if (!ParentClass || !FKismetEditorUtilities::CanCreateBlueprintOfClass(ParentClass))
	{
		OutError = FString::Printf(TEXT("Can't create a Blueprint of %s."), ParentClass ? *ParentClass->GetName() : TEXT("(null)"));
		return nullptr;
	}
	UPackage* Package = CreatePackage(*(PackagePath / AssetName));
	UBlueprint* Blueprint = FKismetEditorUtilities::CreateBlueprint(ParentClass, Package, FName(*AssetName), BPTYPE_Normal,
		UBlueprint::StaticClass(), UBlueprintGeneratedClass::StaticClass(), FName(TEXT("FigmaSync")));
	if (!Blueprint)
	{
		OutError = FString::Printf(TEXT("Could not create %s/%s."), *PackagePath, *AssetName);
		return nullptr;
	}
	FAssetRegistryModule::AssetCreated(Blueprint);
	Package->MarkPackageDirty();
	return Blueprint;
}

bool FFigmaAssetUtils::CompileBlueprint(UBlueprint* Blueprint, FFigmaImportLog* Log)
{
	if (!Blueprint)
	{
		return false;
	}
	FCompilerResultsLog Results;
	Results.bSilentMode = true;
	FKismetEditorUtilities::CompileBlueprint(Blueprint, EBlueprintCompileOptions::None, &Results);
	if (Log)
	{
		for (const TSharedRef<FTokenizedMessage>& Message : Results.Messages)
		{
			const FString Text = FString::Printf(TEXT("%s: %s"), *Blueprint->GetName(), *Message->ToText().ToString());
			if (Message->GetSeverity() == EMessageSeverity::Error)
			{
				Log->Error(Text);
			}
			else if (Message->GetSeverity() == EMessageSeverity::Warning || Message->GetSeverity() == EMessageSeverity::PerformanceWarning)
			{
				Log->Warning(Text);
			}
		}
	}
	return Results.NumErrors == 0;
}

bool FFigmaAssetUtils::SavePackages(const TSet<TWeakObjectPtr<UPackage>>& Packages)
{
	TArray<UPackage*> ToSave;
	for (const TWeakObjectPtr<UPackage>& Package : Packages)
	{
		if (Package.IsValid())
		{
			ToSave.Add(Package.Get());
		}
	}
	return ToSave.Num() == 0 || UEditorLoadingAndSavingUtils::SavePackages(ToSave, true);
}
