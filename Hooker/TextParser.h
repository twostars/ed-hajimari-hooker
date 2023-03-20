#pragma once

#include "constants.h"

struct TextParser
{
	// We don't know this until we're done with parsing
	// as it's incrementally added to throughout the loop.
	// However this will be useful for debug.
	std::string OriginalInputString;
	char PreviousInputBuffer[RPM_BUF_SIZE] = {};

	enum class ParserState
	{
		Waiting,
		ReadingText,
		ReadingHashCode,
		ReadingBackslashCode,
	};

	DWORD_PTR CurrentAddress = 0;
	DWORD_PTR PreviousAddress = 0;
	ParserState State = ParserState::Waiting;

	enum class SequenceEntryType
	{
		Message,
		HashCode,
		BackslashCode
	};

	// A single input string is essentially a message sequence.
	// This will contain relevant flags (via hash codes / backslash codes)
	// and lines of text.
	// The game supports superscript, which is handled via #R so we can ignore
	// it and just merge its regular lines together.
	struct MessageSequenceEntry
	{
		SequenceEntryType Type;
		std::string Data;

		/* Message specific */
		char EOMType = 0; // the character supplied to specify the end of message
		std::vector<char> Flags;
	};

	MessageSequenceEntry CurrentMessage;
	std::vector<MessageSequenceEntry> MessageSequence;

	void Parse(const char input[RPM_BUF_SIZE]);
	void AddMessageFlag(char flag);
	void HandleEndOfMessage(char type);
	void HandlePreviousInput(const char* previous_input, size_t len);
	void FinishParsing();
	void CopyToClipboard();
};
