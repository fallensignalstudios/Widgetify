#include "FigmaTextureImporter.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "EditorFramework/AssetImportData.h"
#include "Engine/Texture2D.h"
#include "FigmaAssetUtils.h"
#include "FigmaImportContext.h"
#include "FigmaNaming.h"
#include "FigmaSyncSettings.h"
#include "FigmaSyncStylePreset.h"
#include "IImageWrapper.h"
#include "IImageWrapperModule.h"
#include "Misc/Paths.h"
#include "Misc/SecureHash.h"
#include "Modules/ModuleManager.h"
#include "UObject/Package.h"

namespace
{
	FMD5Hash HashBytes(const TArray<uint8>& Bytes)
	{
		FMD5 Md5;
		Md5.Update(Bytes.GetData(), Bytes.Num());
		FMD5Hash Hash;
		Hash.Set(Md5);
		return Hash;
	}

	FMD5Hash GetStoredHash(const UTexture2D* Texture)
	{
		if (Texture && Texture->AssetImportData && Texture->AssetImportData->SourceData.SourceFiles.Num() > 0)
		{
			return Texture->AssetImportData->SourceData.SourceFiles[0].FileHash;
		}
		return FMD5Hash();
	}

	void StoreSource(UTexture2D* Texture, const FString& SourceLabel, const FMD5Hash& Hash)
	{
		if (Texture && Texture->AssetImportData)
		{
			Texture->AssetImportData->SourceData.SourceFiles.Reset();
			Texture->AssetImportData->SourceData.SourceFiles.Emplace(SourceLabel, FDateTime::UtcNow(), Hash);
		}
	}
}

FFigmaTextureImporter::FFigmaTextureImporter(FFigmaImportContext& InContext)
	: Context(InContext)
{
}

FName FFigmaTextureImporter::GetPresetForNode(const FFigmaNode& Node, const UFigmaSyncStylePreset* StylePreset)
{
	const FString Tagged = Node.GetTag(TEXT("img"));
	if (!Tagged.IsEmpty())
	{
		return FName(*Tagged);
	}
	const FString Preset = Node.GetTag(TEXT("preset"));
	if (!Preset.IsEmpty())
	{
		return FName(*Preset);
	}
	return StylePreset && !StylePreset->TexturePreset.IsNone() ? StylePreset->TexturePreset : NAME_None;
}

FVector2D FFigmaTextureImporter::GetSlateSize(const FFigmaAsset& Asset)
{
	const float Scale = Asset.Scale > 0.f ? Asset.Scale : 1.f;
	return FVector2D(Asset.Width / Scale, Asset.Height / Scale);
}

UTexture2D* FFigmaTextureImporter::GetOrImport(const FFigmaAsset& Asset, FName PresetName, const FString& InFolder)
{
	const FString Folder = InFolder.IsEmpty() ? Context.TextureFolder : InFolder;
	const FString CacheKey = Folder + TEXT("|") + Asset.Id;
	if (const TWeakObjectPtr<UTexture2D>* Cached = Cache.Find(CacheKey))
	{
		if (Cached->IsValid())
		{
			return Cached->Get();
		}
	}

	TArray<uint8> PngBytes;
	FString Error;
	if (!Context.Bundle.ReadAsset(Asset, PngBytes, Error))
	{
		Context.Log->Error(FString::Printf(TEXT("Image %s: %s"), *Asset.Path, *Error));
		return nullptr;
	}

	const FString BaseName = FFigmaNaming::SanitizeIdentifier(FPaths::GetBaseFilename(Asset.Path), TEXT("Image"));
	FString AssetName = Context.Settings.TexturePrefix + BaseName;

	UTexture2D* Existing = Cast<UTexture2D>(FFigmaAssetUtils::FindAsset(Folder, AssetName, UTexture2D::StaticClass()));
	if (!Existing && FFigmaAssetUtils::DoesAssetExist(Folder, AssetName))
	{
		// Something that isn't a texture already uses the name.
		AssetName = FFigmaAssetUtils::MakeUniqueAssetName(Folder, AssetName);
	}

	const FMD5Hash Hash = HashBytes(PngBytes);
	if (Existing && GetStoredHash(Existing) == Hash)
	{
		Context.Stats.TexturesReused++;
		Cache.Add(CacheKey, Existing);
		return Existing;
	}

	UTexture2D* Texture = ImportPng(PngBytes, Folder, AssetName, PresetName, Existing, Error);
	if (!Texture)
	{
		Context.Log->Error(FString::Printf(TEXT("Image %s could not be imported: %s"), *Asset.Path, *Error));
		return nullptr;
	}
	StoreSource(Texture, FString::Printf(TEXT("%s/%s"), *FPaths::GetCleanFilename(Context.Bundle.GetSourcePath()), *Asset.Path), Hash);
	Context.TouchPackage(Texture->GetPackage());
	Context.Stats.TexturesImported++;
	Cache.Add(CacheKey, Texture);
	return Texture;
}

UTexture2D* FFigmaTextureImporter::ImportPng(const TArray<uint8>& PngBytes, const FString& PackagePath, const FString& AssetName, FName PresetName, UTexture2D* Existing, FString& OutError)
{
	IImageWrapperModule& ImageWrapperModule = FModuleManager::LoadModuleChecked<IImageWrapperModule>(TEXT("ImageWrapper"));
	TSharedPtr<IImageWrapper> Wrapper = ImageWrapperModule.CreateImageWrapper(EImageFormat::PNG);
	if (!Wrapper.IsValid() || !Wrapper->SetCompressed(PngBytes.GetData(), PngBytes.Num()))
	{
		OutError = TEXT("not a valid PNG");
		return nullptr;
	}
	TArray64<uint8> Pixels;
	if (!Wrapper->GetRaw(ERGBFormat::BGRA, 8, Pixels))
	{
		OutError = TEXT("could not decode the PNG");
		return nullptr;
	}
	const int32 Width = static_cast<int32>(Wrapper->GetWidth());
	const int32 Height = static_cast<int32>(Wrapper->GetHeight());
	if (Width <= 0 || Height <= 0 || Pixels.Num() < static_cast<int64>(Width) * Height * 4)
	{
		OutError = TEXT("the PNG has no pixels");
		return nullptr;
	}

	UTexture2D* Texture = Existing;
	const bool bIsNew = Texture == nullptr;
	if (bIsNew)
	{
		UPackage* Package = CreatePackage(*(PackagePath / AssetName));
		Texture = NewObject<UTexture2D>(Package, FName(*AssetName), RF_Public | RF_Standalone | RF_Transactional);
	}
	else
	{
		Texture->Modify();
		Texture->PreEditChange(nullptr);
	}

	Texture->Source.Init(Width, Height, 1, 1, TSF_BGRA8, Pixels.GetData());

	if (bIsNew)
	{
		const FFigmaTexturePreset& Preset = Context.Settings.FindTexturePreset(PresetName);
		Texture->CompressionSettings = Preset.CompressionSettings;
		Texture->SRGB = Preset.bSRGB;
		Texture->LODGroup = Preset.LODGroup;
		Texture->MipGenSettings = Preset.MipGenSettings;
		Texture->Filter = Preset.Filter;
		Texture->NeverStream = Preset.bNeverStream;
		Texture->AddressX = TA_Clamp;
		Texture->AddressY = TA_Clamp;
	}

	Texture->PostEditChange();
	Texture->MarkPackageDirty();
	if (bIsNew)
	{
		FAssetRegistryModule::AssetCreated(Texture);
	}
	return Texture;
}
