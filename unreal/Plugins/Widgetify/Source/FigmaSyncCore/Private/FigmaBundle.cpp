#include "FigmaBundle.h"
#include "FigmaDocumentParser.h"
#include "FigmaZipReader.h"
#include "Containers/StringConv.h"
#include "HAL/FileManager.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"

FFigmaBundle::~FFigmaBundle() = default;

bool FFigmaBundle::IsBundlePath(const FString& Path)
{
	if (Path.IsEmpty())
	{
		return false;
	}
	if (IFileManager::Get().DirectoryExists(*Path))
	{
		return FPaths::FileExists(FPaths::Combine(Path, FigmaSync::BundleJsonName));
	}
	const FString Extension = FPaths::GetExtension(Path).ToLower();
	return (Extension == TEXT("zip") || Extension == TEXT("json")) && FPaths::FileExists(Path);
}

TSharedPtr<FFigmaBundle> FFigmaBundle::Open(const FString& InPath, FString& OutError)
{
	FString Path = InPath;
	FPaths::NormalizeFilename(Path);
	while (Path.EndsWith(TEXT("/")) && Path.Len() > 1)
	{
		Path.LeftChopInline(1);
	}

	TSharedPtr<FFigmaBundle> Bundle(new FFigmaBundle());
	Bundle->SourcePath = Path;

	if (IFileManager::Get().DirectoryExists(*Path))
	{
		const FString JsonPath = FPaths::Combine(Path, FigmaSync::BundleJsonName);
		TArray<uint8> JsonBytes;
		if (!FFileHelper::LoadFileToArray(JsonBytes, *JsonPath))
		{
			OutError = FString::Printf(TEXT("No %s in '%s'. Select the folder or .zip the Figma plugin exported."), FigmaSync::BundleJsonName, *Path);
			return nullptr;
		}
		Bundle->FolderRoot = Path;
		return Bundle->ParseJsonBytes(JsonBytes, OutError) ? Bundle : nullptr;
	}

	if (!FPaths::FileExists(Path))
	{
		OutError = FString::Printf(TEXT("'%s' does not exist."), *Path);
		return nullptr;
	}

	const FString Extension = FPaths::GetExtension(Path).ToLower();
	if (Extension == TEXT("json"))
	{
		TArray<uint8> JsonBytes;
		if (!FFileHelper::LoadFileToArray(JsonBytes, *Path))
		{
			OutError = FString::Printf(TEXT("Could not read '%s'."), *Path);
			return nullptr;
		}
		Bundle->FolderRoot = FPaths::GetPath(Path);
		return Bundle->ParseJsonBytes(JsonBytes, OutError) ? Bundle : nullptr;
	}

	TArray<uint8> ZipBytes;
	if (!FFileHelper::LoadFileToArray(ZipBytes, *Path))
	{
		OutError = FString::Printf(TEXT("Could not read '%s'."), *Path);
		return nullptr;
	}
	return Bundle->InitFromZip(MoveTemp(ZipBytes), OutError) ? Bundle : nullptr;
}

TSharedPtr<FFigmaBundle> FFigmaBundle::OpenFromZipBytes(TArray<uint8>&& ZipBytes, const FString& DisplayName, FString& OutError)
{
	TSharedPtr<FFigmaBundle> Bundle(new FFigmaBundle());
	Bundle->SourcePath = DisplayName;
	return Bundle->InitFromZip(MoveTemp(ZipBytes), OutError) ? Bundle : nullptr;
}

bool FFigmaBundle::InitFromZip(TArray<uint8>&& ZipBytes, FString& OutError)
{
	Zip = MakeShared<FFigmaZipReader>();
	if (!Zip->Open(MoveTemp(ZipBytes), OutError))
	{
		return false;
	}

	// The JSON is normally at the root, but a re-zipped folder puts everything under "MainMenu/".
	ArchivePrefix.Reset();
	if (!Zip->Contains(FigmaSync::BundleJsonName))
	{
		const FString Suffix = FString(TEXT("/")) + FigmaSync::BundleJsonName;
		bool bFound = false;
		for (const FString& Name : Zip->GetEntryNames())
		{
			if (Name.EndsWith(Suffix, ESearchCase::IgnoreCase))
			{
				ArchivePrefix = Name.LeftChop(FCString::Strlen(FigmaSync::BundleJsonName));
				bFound = true;
				break;
			}
		}
		if (!bFound)
		{
			OutError = FString::Printf(TEXT("The zip has no %s. Is it a Widgetify bundle?"), FigmaSync::BundleJsonName);
			return false;
		}
	}

	TArray<uint8> JsonBytes;
	if (!Zip->Read(ArchivePrefix + FigmaSync::BundleJsonName, JsonBytes, OutError))
	{
		return false;
	}
	return ParseJsonBytes(JsonBytes, OutError);
}

bool FFigmaBundle::ParseJsonBytes(const TArray<uint8>& JsonBytes, FString& OutError)
{
	int32 Offset = 0;
	if (JsonBytes.Num() >= 3 && JsonBytes[0] == 0xEF && JsonBytes[1] == 0xBB && JsonBytes[2] == 0xBF)
	{
		Offset = 3; // UTF-8 BOM
	}
	const FUTF8ToTCHAR Converted(reinterpret_cast<const ANSICHAR*>(JsonBytes.GetData() + Offset), JsonBytes.Num() - Offset);
	const FString JsonText(Converted.Length(), Converted.Get());
	return FFigmaDocumentParser::Parse(JsonText, Document, OutError);
}

bool FFigmaBundle::ReadFile(const FString& RelativePath, TArray<uint8>& OutBytes, FString& OutError) const
{
	FString Clean = RelativePath.Replace(TEXT("\\"), TEXT("/"));
	TArray<FString> Segments;
	Clean.ParseIntoArray(Segments, TEXT("/"), true);
	if (Segments.Num() == 0 || Segments.Contains(TEXT("..")) || Clean.StartsWith(TEXT("/")) || Clean.Contains(TEXT(":")))
	{
		OutError = FString::Printf(TEXT("Refusing to read '%s': bundle paths must stay inside the bundle."), *RelativePath);
		return false;
	}
	Clean = FString::Join(Segments, TEXT("/"));

	if (Zip.IsValid())
	{
		return Zip->Read(ArchivePrefix + Clean, OutBytes, OutError);
	}

	const FString FullPath = FPaths::Combine(FolderRoot, Clean);
	if (!FFileHelper::LoadFileToArray(OutBytes, *FullPath))
	{
		OutError = FString::Printf(TEXT("'%s' is missing from the bundle folder."), *Clean);
		return false;
	}
	return true;
}
