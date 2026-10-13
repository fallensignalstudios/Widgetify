#pragma once

#include "CoreMinimal.h"

struct FFigmaNode;

enum class EFigmaLogSeverity : uint8
{
	Info,
	Warning,
	Error,
};

enum class EFigmaLogCategory : uint8
{
	/** Something was skipped or approximated while building. */
	Import,
	/** A finding from the lint pass (focus, input actions, fonts...). */
	Lint,
	/** A warning the Figma exporter already reported. */
	Export,
};

struct FIGMASYNCEDITOR_API FFigmaLogEntry
{
	EFigmaLogSeverity Severity = EFigmaLogSeverity::Info;
	EFigmaLogCategory Category = EFigmaLogCategory::Import;
	FString NodeId;
	FString NodePath;
	FString Message;

	FString ToString() const;
};

/** The import log: everything skipped or approximated, so nothing fails silently. */
class FIGMASYNCEDITOR_API FFigmaImportLog
{
public:
	void Add(EFigmaLogSeverity Severity, EFigmaLogCategory Category, const FString& Message, const FFigmaNode* Node = nullptr);
	void AddForPath(EFigmaLogSeverity Severity, EFigmaLogCategory Category, const FString& Message, const FString& NodeId, const FString& NodePath);

	void Info(const FString& Message, const FFigmaNode* Node = nullptr) { Add(EFigmaLogSeverity::Info, EFigmaLogCategory::Import, Message, Node); }
	void Warning(const FString& Message, const FFigmaNode* Node = nullptr) { Add(EFigmaLogSeverity::Warning, EFigmaLogCategory::Import, Message, Node); }
	void Error(const FString& Message, const FFigmaNode* Node = nullptr) { Add(EFigmaLogSeverity::Error, EFigmaLogCategory::Import, Message, Node); }
	void Lint(EFigmaLogSeverity Severity, const FString& Message, const FFigmaNode* Node = nullptr) { Add(Severity, EFigmaLogCategory::Lint, Message, Node); }

	const TArray<TSharedPtr<FFigmaLogEntry>>& GetEntries() const { return Entries; }
	int32 Count(EFigmaLogSeverity Severity) const;
	int32 Count(EFigmaLogCategory Category) const;
	bool HasErrors() const { return Count(EFigmaLogSeverity::Error) > 0; }
	void Reset();

	/** Mirrors the log into the "Figma Sync" Message Log page. */
	void FlushToMessageLog(const FText& Title, bool bOpenOnWarnings) const;

	/** Writes a plain-text copy (Saved/FigmaSync/Logs/<name>.log). */
	bool SaveToFile(const FString& FilePath, const FString& Header) const;

private:
	TArray<TSharedPtr<FFigmaLogEntry>> Entries;
	TSet<FString> Seen;
};
