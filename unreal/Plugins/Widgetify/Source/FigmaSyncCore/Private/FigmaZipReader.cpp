#include "FigmaZipReader.h"
#include "Containers/StringConv.h"
#include "Misc/Compression.h"

namespace
{
	constexpr uint32 LocalHeaderSignature = 0x04034b50;
	constexpr uint32 CentralHeaderSignature = 0x02014b50;
	constexpr uint32 EndOfCentralDirSignature = 0x06054b50;
	constexpr int64 EndOfCentralDirSize = 22;
	constexpr int64 CentralHeaderSize = 46;
	constexpr int64 LocalHeaderSize = 30;

	/** Bundles hold a JSON file and PNGs; anything bigger is a corrupt or hostile archive (live push accepts uploads). */
	constexpr uint32 MaxEntryBytes = 256u * 1024u * 1024u;

	struct FCrcTable
	{
		uint32 Entries[256];

		FCrcTable()
		{
			for (uint32 N = 0; N < 256; ++N)
			{
				uint32 C = N;
				for (int32 K = 0; K < 8; ++K)
				{
					C = (C & 1u) ? (0xEDB88320u ^ (C >> 1)) : (C >> 1);
				}
				Entries[N] = C;
			}
		}
	};

	const uint32* GetCrcTable()
	{
		static const FCrcTable Table; // Function-local static: initialized once, thread-safely.
		return Table.Entries;
	}
}

uint32 FFigmaZipReader::Crc32(const uint8* Data, int64 Size)
{
	const uint32* Table = GetCrcTable();
	uint32 Crc = 0xFFFFFFFFu;
	for (int64 Index = 0; Index < Size; ++Index)
	{
		Crc = Table[(Crc ^ Data[Index]) & 0xFFu] ^ (Crc >> 8);
	}
	return Crc ^ 0xFFFFFFFFu;
}

FString FFigmaZipReader::MakeKey(const FString& Path)
{
	FString Key = Path.Replace(TEXT("\\"), TEXT("/"));
	while (Key.StartsWith(TEXT("/")))
	{
		Key.RightChopInline(1);
	}
	return Key.ToLower();
}

uint16 FFigmaZipReader::Read16(int64 Offset) const
{
	return static_cast<uint16>(Bytes[Offset] | (Bytes[Offset + 1] << 8));
}

uint32 FFigmaZipReader::Read32(int64 Offset) const
{
	return static_cast<uint32>(Bytes[Offset]) | (static_cast<uint32>(Bytes[Offset + 1]) << 8) | (static_cast<uint32>(Bytes[Offset + 2]) << 16) |
		(static_cast<uint32>(Bytes[Offset + 3]) << 24);
}

bool FFigmaZipReader::Open(TArray<uint8>&& InBytes, FString& OutError)
{
	Bytes = MoveTemp(InBytes);
	Entries.Reset();
	EntryNames.Reset();
	EntryIndex.Reset();

	const int64 Size = Bytes.Num();
	if (Size < EndOfCentralDirSize)
	{
		OutError = TEXT("File is too small to be a zip archive.");
		return false;
	}

	int64 Eocd = INDEX_NONE;
	const int64 SearchStart = Size - EndOfCentralDirSize;
	const int64 SearchEnd = FMath::Max<int64>(0, SearchStart - 0xFFFF);
	for (int64 Position = SearchStart; Position >= SearchEnd; --Position)
	{
		if (Read32(Position) == EndOfCentralDirSignature)
		{
			Eocd = Position;
			break;
		}
	}
	if (Eocd == INDEX_NONE)
	{
		OutError = TEXT("Not a zip archive (no end-of-central-directory record).");
		return false;
	}

	const uint16 EntryCount = Read16(Eocd + 10);
	const uint32 CentralSize = Read32(Eocd + 12);
	const uint32 CentralOffset = Read32(Eocd + 16);
	if (EntryCount == 0xFFFF || CentralOffset == 0xFFFFFFFFu)
	{
		OutError = TEXT("ZIP64 archives are not supported. Re-export the bundle from Figma.");
		return false;
	}
	if (static_cast<int64>(CentralOffset) + static_cast<int64>(CentralSize) > Eocd)
	{
		OutError = TEXT("Zip central directory is out of range (truncated download?).");
		return false;
	}

	int64 Position = CentralOffset;
	for (int32 Index = 0; Index < EntryCount; ++Index)
	{
		if (Position + CentralHeaderSize > Size || Read32(Position) != CentralHeaderSignature)
		{
			OutError = TEXT("Zip central directory is corrupt.");
			return false;
		}
		const uint16 Flags = Read16(Position + 8);
		FEntry Entry;
		Entry.Method = Read16(Position + 10);
		Entry.Crc = Read32(Position + 16);
		Entry.CompressedSize = Read32(Position + 20);
		Entry.UncompressedSize = Read32(Position + 24);
		const uint16 NameLength = Read16(Position + 28);
		const uint16 ExtraLength = Read16(Position + 30);
		const uint16 CommentLength = Read16(Position + 32);
		Entry.LocalHeaderOffset = Read32(Position + 42);
		if (Position + CentralHeaderSize + NameLength > Size)
		{
			OutError = TEXT("Zip entry name is out of range.");
			return false;
		}

		// Names are UTF-8 when bit 11 is set; plain ASCII names decode the same way either way.
		const FUTF8ToTCHAR Converted(reinterpret_cast<const ANSICHAR*>(Bytes.GetData() + Position + CentralHeaderSize), NameLength);
		Entry.Name = FString(Converted.Length(), Converted.Get()).Replace(TEXT("\\"), TEXT("/"));
		Position += CentralHeaderSize + NameLength + ExtraLength + CommentLength;

		if (Entry.Name.EndsWith(TEXT("/")))
		{
			continue;
		}
		if (Flags & 0x1)
		{
			OutError = FString::Printf(TEXT("Zip entry '%s' is encrypted, which is not supported."), *Entry.Name);
			return false;
		}
		const FString Key = MakeKey(Entry.Name);
		if (!EntryIndex.Contains(Key))
		{
			EntryIndex.Add(Key, Entries.Num());
			EntryNames.Add(Entry.Name);
			Entries.Add(MoveTemp(Entry));
		}
	}
	return true;
}

bool FFigmaZipReader::Contains(const FString& Path) const
{
	return EntryIndex.Contains(MakeKey(Path));
}

bool FFigmaZipReader::Read(const FString& Path, TArray<uint8>& OutData, FString& OutError) const
{
	OutData.Reset();
	const int32* Index = EntryIndex.Find(MakeKey(Path));
	if (!Index)
	{
		OutError = FString::Printf(TEXT("'%s' is missing from the bundle."), *Path);
		return false;
	}
	const FEntry& Entry = Entries[*Index];
	const int64 Local = Entry.LocalHeaderOffset;
	if (Local + LocalHeaderSize > Bytes.Num() || Read32(Local) != LocalHeaderSignature)
	{
		OutError = FString::Printf(TEXT("Zip entry '%s' has a corrupt local header."), *Entry.Name);
		return false;
	}
	const int64 DataStart = Local + LocalHeaderSize + Read16(Local + 26) + Read16(Local + 28);
	if (DataStart + static_cast<int64>(Entry.CompressedSize) > Bytes.Num())
	{
		OutError = FString::Printf(TEXT("Zip entry '%s' is truncated."), *Entry.Name);
		return false;
	}
	// Also keeps the int32 casts below non-negative.
	if (Entry.UncompressedSize > MaxEntryBytes || (Entry.Method == 0 && Entry.CompressedSize != Entry.UncompressedSize))
	{
		OutError = FString::Printf(TEXT("Zip entry '%s' has an invalid size (%u bytes)."), *Entry.Name, Entry.UncompressedSize);
		return false;
	}

	if (Entry.Method == 0)
	{
		OutData.Append(Bytes.GetData() + DataStart, static_cast<int32>(Entry.CompressedSize));
	}
	else if (Entry.Method == 8)
	{
		OutData.SetNumUninitialized(static_cast<int32>(Entry.UncompressedSize));
		// int32 sizes widen to the int64 parameters of UE 5.5+; the literal -15 (a negative zlib window = raw deflate)
		// matches Epic's own DMXZipper and converts cleanly to the uintptr_t CompressionData parameter of 5.5+.
		const bool bInflated = Entry.UncompressedSize == 0 ||
			FCompression::UncompressMemory(NAME_Zlib, OutData.GetData(), static_cast<int32>(Entry.UncompressedSize), Bytes.GetData() + DataStart,
				static_cast<int32>(Entry.CompressedSize), COMPRESS_NoFlags, -15);
		if (!bInflated)
		{
			OutData.Reset();
			OutError = FString::Printf(TEXT("Could not decompress '%s'. Extract the zip and select the folder instead."), *Entry.Name);
			return false;
		}
	}
	else
	{
		OutError = FString::Printf(TEXT("Zip entry '%s' uses compression method %d, which is not supported. Extract the zip and select the folder instead."),
			*Entry.Name, Entry.Method);
		return false;
	}

	if (Crc32(OutData.GetData(), OutData.Num()) != Entry.Crc)
	{
		OutData.Reset();
		OutError = FString::Printf(TEXT("Zip entry '%s' failed its CRC check (corrupt download?)."), *Entry.Name);
		return false;
	}
	return true;
}
