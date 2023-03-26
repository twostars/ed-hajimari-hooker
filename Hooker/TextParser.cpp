#include "stdafx.h"
#include "TextParser.h"
#include "string_utils.h"

void TextParser::Parse(const char* input)
{
	// If the official parser skipped over anything, we should handle it appropriately.
	if (PreviousAddress != 0)
	{
		size_t len = CurrentAddress - PreviousAddress;
		HandlePreviousInput(PreviousInputBuffer, len);
	}

	char currentChar = input[0];

	PreviousAddress = CurrentAddress;
	if (currentChar != '\0')
		OriginalInputString += currentChar;

	// Note: Redundant switch cases are due to preserving parser order for comparison with the official logic
	switch (currentChar)
	{
		case '\0':
			// finished parsing?
			HandleEndOfMessage(currentChar);
			FinishParsing();
			break;

		case '\1': // newline? handled with \n so probably
		case '\n': // handled with \1
		case '\2':
			HandleEndOfMessage(currentChar);
			break;

		case '\3':
			// seems to generally act as some kind of flag, might be used for something in parsing
			// but unclear at this time
			AddMessageFlag(currentChar); /* may not be specific to the message */
			break;

		case '\6':
			AddMessageFlag(currentChar); /* may not be specific to the message */
			break;

		case '\a':
			HandleEndOfMessage(currentChar);
			break;

		case '\b':
			AddMessageFlag(currentChar); /* may not be specific to the message */
			break;
				
		case '\v':
		case '\f':
		case '\x0f':
			HandleEndOfMessage(currentChar);
			break;

		case '\x10':
			// TODO: this can (when some alternative string is present) atoi() a string length
			// from the input and replace the destination text with the alternative string
			// it will also trigger an end of message.
			HandleEndOfMessage(currentChar);
			break;

		case '\x11':
		case '\x12':  // handled with \x11
			AddMessageFlag(currentChar); /* may not be specific to the message */
			break;

		case '\x13':
		case '\x14':
		case '\x15':
		case '\x16':
			AddMessageFlag(currentChar); /* may not be specific to the message */
			break;

		case '\x17':
			// seemingly an end of message
			HandleEndOfMessage(currentChar);
			break;

		case '\x18':
			AddMessageFlag(currentChar); /* may not be specific to the message */
			break;

		case '\x19':
			// TODO: prior text is atoi()'d and an end of message is triggered.
			HandleEndOfMessage(currentChar);
			break;

		case '\x1a':
			AddMessageFlag(currentChar); /* may not be specific to the message */
			break;

		case '\\':
			State = ParserState::ReadingBackslashCode;
			break;

		default:
			if (currentChar != '#' /* || *(_BYTE *)(v26 + 9202) */)
				State = ParserState::ReadingText;
			else // if (currentChar == '#')
				State = ParserState::ReadingHashCode;
	}
}

void TextParser::AddMessageFlag(char flag)
{
	CurrentMessage.Flags.push_back(flag);
}

void TextParser::HandleEndOfMessage(char type)
{
	if (CurrentMessage.Data.empty())
		return;

	CurrentMessage.EOMType = type;
	CurrentMessage.Type = SequenceEntryType::Message;
	MessageSequence.push_back(CurrentMessage);

	CurrentMessage = {};
}

void TextParser::HandlePreviousInput(const char* previous_input, size_t len)
{
	switch (State)
	{
		case ParserState::ReadingHashCode:
		{
			MessageSequenceEntry entry;
			entry.Type = SequenceEntryType::HashCode;

			// We don't need the # as part of this.
			entry.Data.assign(previous_input + 1, len - 1);
			MessageSequence.push_back(entry);
		} break;

		case ParserState::ReadingText:
			CurrentMessage.Data.append(previous_input, len);
			break;

		case ParserState::ReadingBackslashCode:
		{
			MessageSequenceEntry entry;
			entry.Type = SequenceEntryType::BackslashCode;

			// We don't need the \ as part of this.
			entry.Data.assign(previous_input + 1, len - 1);
			MessageSequence.push_back(entry);
		} break;
	}

	State = ParserState::Waiting;
}

void TextParser::FinishParsing()
{
	if (MessageSequence.empty())
		return;

	CopyToClipboard();
}

void TextParser::CopyToClipboard()
{
	if (MessageSequence.empty())
		return;

	const UINT CP_SHIFT_JIS = 932;
	const DWORD LCID_JA_JA = 1041;

	thread_local std::wstring PreviousMessage;

	// Construct messages for clipboard.
	// For now we just use the first message.
	std::wstring constructedMessage;
	for (auto itr = MessageSequence.begin(); itr != MessageSequence.end(); ++itr)
	{
		auto& msg = *itr;
		if (msg.Type == SequenceEntryType::Message)
			constructedMessage += MultiByteToWideChar(CP_UTF8, msg.Data) + L'\n';
	}

	if (constructedMessage == PreviousMessage
		|| !IsJapaneseText(constructedMessage))
		return;

	auto utf8str = WideCharToMultiByte(CP_SHIFT_JIS, constructedMessage);

	// Write the data to the clipboard.
	if (!OpenClipboard(nullptr))
	{
		printf("Failed to open clipboard. (Error %u)\n", GetLastError());
		return;
	}

	if (!EmptyClipboard())
	{
		printf("Failed to empty clipboard. (Error %u)\n", GetLastError());
	}
	else
	{
		// allocate a buffer to give to the system with the clipboard data.
		HGLOBAL clip_buf = GlobalAlloc(GMEM_MOVEABLE | GMEM_ZEROINIT, utf8str.size() + 1);
		if (clip_buf == nullptr)
		{
			printf("Failed to allocate a buffer for the clipboard data. (Error %u)\n", GetLastError());
		}
		else
		{
			// copy the message buffer into the global buffer
			auto ptr = GlobalLock(clip_buf);
			if (ptr != nullptr)
				memcpy(ptr, utf8str.c_str(), utf8str.size() + 1);

			GlobalUnlock(clip_buf);

			HGLOBAL locale_ptr = GlobalAlloc(GMEM_MOVEABLE, sizeof(DWORD));
			if (locale_ptr == nullptr)
			{
				printf("Failed to allocate a buffer for the clipboard locale. (Error %u)\n", GetLastError());
			}
			else
			{
				DWORD japanese = LCID_JA_JA;
				auto ptr = GlobalLock(locale_ptr);
				if (ptr != nullptr)
					memcpy(ptr, &japanese, sizeof(japanese));

				GlobalUnlock(locale_ptr);
	
				if (!SetClipboardData(CF_TEXT, clip_buf))
				{
					printf("Failed to set clipboard data. (Error %u)\n", GetLastError());
					GlobalFree(clip_buf);
					GlobalFree(locale_ptr);
				}
				else if (!SetClipboardData(CF_LOCALE, locale_ptr))
				{
					printf("Failed to set clipboard locale. (Error %u)\n", GetLastError());
					GlobalFree(locale_ptr);
				}

				PreviousMessage = constructedMessage;
			}
		}
	}

	CloseClipboard();
}
