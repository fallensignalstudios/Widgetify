#pragma once

#include "CoreMinimal.h"

/**
 * Minimal read-only ZIP reader for figmasync bundles.
 *
 * Supports stored entries (what the Widgetify exporter writes) and deflate entries (in case someone re-zips a
 * bundle folder with their OS). No ZIP64, no encryption, no multi-disk archives.
 */
class FIGMASYNCCORE_API FFigmaZipReader
{
public:
	/** Takes ownership of the archive bytes and indexes the central directory. */
	bool Open(TArray<uint8>&& InBytes, FString& OutError);

	/** Case-insensitive lookup of a forward-slash path inside the archive. */
	bool Contains(const FString& Path) const;

	/** Extracts one entry and verifies its CRC-32. */
	bool Read(const FString& Path, TArray<uint8>& OutData, FString& OutError) const;

	/** All file entries (directories excluded), in archive order. */
	const TArray<FString>& GetEntryNames() const { return EntryNames; }

	/** Standard (zlib/PNG) CRC-32. */
	static uint32 Crc32(const uint8* Data, int64 Size);

private:
	struct FEntry
	{
		FString Name;
		uint16 Method = 0;
		uint32 Crc = 0;
		uint32 CompressedSize = 0;
		uint32 UncompressedSize = 0;
		uint32 LocalHeaderOffset = 0;
	};

	static FString MakeKey(const FString& Path);
	uint16 Read16(int64 Offset) const;
	uint32 Read32(int64 Offset) const;

	TArray<uint8> Bytes;
	TArray<FEntry> Entries;
	TArray<FString> EntryNames;
	TMap<FString, int32> EntryIndex;
};
