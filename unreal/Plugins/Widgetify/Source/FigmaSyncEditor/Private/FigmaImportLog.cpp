#include "FigmaImportLog.h"
#include "FigmaDocument.h"
#include "Logging/MessageLog.h"
#include "Logging/TokenizedMessage.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"

#define LOCTEXT_NAMESPACE "FigmaSync"

namespace
{
	const TCHAR* SeverityLabel(EFigmaLogSeverity Severity)
	{
		switch (Severity)
		{
		case EFigmaLogSeverity::Error: return TEXT("Error");
		case EFigmaLogSeverity::Warning: return TEXT("Warning");
		default: return TEXT("Info");
		}
	}

	const TCHAR* CategoryLabel(EFigmaLogCategory Category)
	{
		switch (Category)
		{
		case EFigmaLogCategory::Lint: return TEXT("Lint");
		case EFigmaLogCategory::Export: return TEXT("Figma");
		default: return TEXT("Import");
		}
	}
}

FString FFigmaLogEntry::ToString() const
{
	return NodePath.IsEmpty()
		? FString::Printf(TEXT("[%s] %s: %s"), CategoryLabel(Category), SeverityLabel(Severity), *Message)
		: FString::Printf(TEXT("[%s] %s: %s: %s"), CategoryLabel(Category), SeverityLabel(Severity), *NodePath, *Message);
}

void FFigmaImportLog::Add(EFigmaLogSeverity Severity, EFigmaLogCategory Category, const FString& Message, const FFigmaNode* Node)
{
	AddForPath(Severity, Category, Message, Node ? Node->Id : FString(), Node ? Node->GetPath() : FString());
}

void FFigmaImportLog::AddForPath(EFigmaLogSeverity Severity, EFigmaLogCategory Category, const FString& Message, const FString& NodeId, const FString& NodePath)
{
	// The same finding for the same layer is only interesting once.
	const FString Key = FString::Printf(TEXT("%d|%s|%s"), static_cast<int32>(Category), *NodeId, *Message);
	if (Seen.Contains(Key))
	{
		return;
	}
	Seen.Add(Key);

	TSharedPtr<FFigmaLogEntry> Entry = MakeShared<FFigmaLogEntry>();
	Entry->Severity = Severity;
	Entry->Category = Category;
	Entry->NodeId = NodeId;
	Entry->NodePath = NodePath;
	Entry->Message = Message;
	Entries.Add(Entry);
}

int32 FFigmaImportLog::Count(EFigmaLogSeverity Severity) const
{
	int32 Count = 0;
	for (const TSharedPtr<FFigmaLogEntry>& Entry : Entries)
	{
		Count += Entry->Severity == Severity ? 1 : 0;
	}
	return Count;
}

int32 FFigmaImportLog::Count(EFigmaLogCategory Category) const
{
	int32 Count = 0;
	for (const TSharedPtr<FFigmaLogEntry>& Entry : Entries)
	{
		Count += Entry->Category == Category ? 1 : 0;
	}
	return Count;
}

void FFigmaImportLog::Reset()
{
	Entries.Reset();
	Seen.Reset();
}

void FFigmaImportLog::FlushToMessageLog(const FText& Title, bool bOpenOnWarnings) const
{
	FMessageLog MessageLog(TEXT("FigmaSync"));
	MessageLog.NewPage(Title);
	for (const TSharedPtr<FFigmaLogEntry>& Entry : Entries)
	{
		const FText Text = FText::FromString(Entry->ToString());
		switch (Entry->Severity)
		{
		case EFigmaLogSeverity::Error: MessageLog.Error(Text); break;
		case EFigmaLogSeverity::Warning: MessageLog.Warning(Text); break;
		default: MessageLog.Info(Text); break;
		}
	}
	if (bOpenOnWarnings && (Count(EFigmaLogSeverity::Error) > 0 || Count(EFigmaLogSeverity::Warning) > 0))
	{
		MessageLog.Open(EMessageSeverity::Warning);
	}
}

bool FFigmaImportLog::SaveToFile(const FString& FilePath, const FString& Header) const
{
	FString Text = Header + LINE_TERMINATOR;
	for (const TSharedPtr<FFigmaLogEntry>& Entry : Entries)
	{
		Text += Entry->ToString() + LINE_TERMINATOR;
	}
	return FFileHelper::SaveStringToFile(Text, *FilePath, FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM);
}

#undef LOCTEXT_NAMESPACE
