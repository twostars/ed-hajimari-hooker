#include "stdafx.h"
#include "TextParser.h"
#include "string_utils.h"

thread_local char oldstr[MSG_BUF_SIZE + 1] = {};
thread_local std::wstring wstr;
thread_local std::string utf8str;

void TextParser::Parse(const char input[RPM_BUF_SIZE])
{
	// If the official parser skipped over anything, we should add it to our string
	if (PreviousAddress != 0)
	{
		size_t len = buf_current_address - PreviousAddress;
		if (len > 1)
		{
			for (size_t i = 1; i < len; i++)
				OriginalInputString += prev_rpm_buf[i];
		}
	}

	char currentChar = input[0];

	PreviousAddress = buf_current_address;
	if (currentChar != '\0')
		OriginalInputString += currentChar;

	if (currentChar < ' ')
	{
		// Note: Redundant switch cases are due to preserving parser order for comparison with the official logic
		switch (currentChar)
		{
			case '\0':
				// finished parsing?
				Parse_EndOfMessage(currentChar);
				FinishParsing();
				break;

			case '\1': // newline? handled with \n so probably
			case '\n': // handled with \1
			case '\2':
				Parse_EndOfMessage(currentChar);
				break;

			case '\3':
				// seems to generally act as some kind of flag, might be used for something in parsing
				// but unclear at this time
				Parse_MessageFlag(currentChar); /* may not be specific to the message */
				break;

			case '\6':
				Parse_MessageFlag(currentChar); /* may not be specific to the message */
				break;

			case '\a':
				Parse_EndOfMessage(currentChar);
				break;

			case '\b':
				Parse_MessageFlag(currentChar); /* may not be specific to the message */
				break;
				
			case '\v':
			case '\f':
			case '\x0f':
				Parse_EndOfMessage(currentChar);
				break;

			case '\x10':
				// TODO: this can (when some alternative string is present) atoi() a string length
				// from the input and replace the destination text with the alternative string
				// it will also trigger an end of message.
				Parse_EndOfMessage(currentChar);
				break;

			case '\x11':
			case '\x12':  // handled with \x11
				Parse_MessageFlag(currentChar); /* may not be specific to the message */
				break;

			case '\x13':
			case '\x14':
			case '\x15':
			case '\x16':
				Parse_MessageFlag(currentChar); /* may not be specific to the message */
				break;

			case '\x17':
				// seemingly an end of message
				Parse_EndOfMessage(currentChar);
				break;

			case '\x18':
				Parse_MessageFlag(currentChar); /* may not be specific to the message */
				break;

			case '\x19':
				// TODO: prior text is atoi()'d and an end of message is triggered.
				Parse_EndOfMessage(currentChar);
				break;

			case '\x1a':
				// flag of some kind
				break;
		}
	}
	else
	{
		if (currentChar == '\\')
		{
			if (input[1] == 'n')
			{
				// TODO
			}
			else
			{
				// TODO
			}
		}
		else if (currentChar != '#' /* || *(_BYTE *)(v26 + 9202) */)
		{
			Parse_Text(input);
		}
		else // if (currentChar == '#')
		{
			Parse_HashCode(input);
		}
	}

	// old parser logic
	parse(input);
}

void TextParser::Parse_MessageFlag(char flag)
{
	CurrentMessage.Flags.push_back(flag);
}

void TextParser::Parse_EndOfMessage(uint16_t type)
{
	if (CurrentMessage.Data.empty())
		return;

	CurrentMessage.EOMType = type;
	Messages.push_back(CurrentMessage);
	CurrentMessage = {};
}

void TextParser::Parse_HashCode(const char input[RPM_BUF_SIZE])
{
	std::string tmp;
	for (size_t i = 1; i < RPM_BUF_SIZE; i++)
	{
		// if (((uint8_t)input[i] - 66) <= 35u)
		if (input[i] > 'A')
		{
			// TODO: [, _, .... EeMBH
		}
		else
		{
			switch (input[i])
			{
				case 'V':
					/* skipped */
					return;

				case 'I':
				case 'i':
				case 'P':
				case 'T':
				case 'W':
				case 'w':
				case 'K':
				case 'k':
				case 'F':
				case 'S':
				case 's':
				case 'C':
				case 'c':
				case 'Z':
				case 'R':
				case 'x':
				case 'y':
				case 'X':
				case 'Y':
				case 'G':
				case 'U':
				case 'D':
				case 'g':
					CurrentMessage.Data = tmp;
					Parse_EndOfMessage(MAKEWORD(input[0], input[i]));
					return;

				default:
					tmp += input[i];
			}
		}
	}
}

void TextParser::Parse_Text(const char input[RPM_BUF_SIZE])
{
	// Append each character or set of characters to the current message.
	// Officially this checks the input argument to determine what type of string we're dealing with,
	// for example: in the build I am working with, it's always 3 to indicate UTF-8 encoded strings.
	// For now, while I'm implementing this, I will assume it's always the case, but I should probably change this after
	// to simply consider this as text for it to append in the next loop iteration based on how far the official loop has progressed.
	size_t utf8CharacterSize = GetUtf8CharacterSize(input[0]);
	for (size_t i = 0; i < utf8CharacterSize; i++)
		CurrentMessage.Data += input[i];
}

void TextParser::FinishParsing()
{
	// TODO: construct messages for clipboard
	CopyToClipboard();
}

void TextParser::CopyToClipboard()
{
}

void TextParser::parse(const char input[RPM_BUF_SIZE])
{
	if (defer_until_address != 0
		&& buf_current_address < defer_until_address)
		return;

	// This parser needs to mimic the official behaviour better
	// Official behaviour iterates over the string and handles it character by character,
	// parsing further depending on where it's at - but even when parsing out the message,
	// it will still do that only 3 bytes at a time for appropriate characters.
	// Its states may well be unnecessary.
	// Note that each call of this method is triggered by each loop iteration
	// or each main character it should process.
	// Any that it has "skipped" will have been handled by its previous processing.
	switch (parser_state)
	{
	case PARSER_READY:
		switch (input[0])
		{
			case '': // \15
				parser_state = PARSER_READ_COLOR;
				break;

			case '#':
				parser_state = PARSER_READ_HASHCODE;
				parse(input);
				break;

			default:
				parser_state = PARSER_TEXT;
				parse(input);
		}
		break;

	case PARSER_READ_COLOR:
		parse_color(input);
		parser_state = PARSER_READY;
		break;

	case PARSER_READ_HASHCODE:
		parse_hashcode(input);
		parser_state = PARSER_READY;
		break;

	case PARSER_TEXT:
		if (msg_start_address == 0)
			msg_start_address = buf_current_address;

		if (prev_msg_address != 0)
		{
			size_t len = buf_current_address - prev_msg_address;
			if (msg_offset + len > MSG_BUF_SIZE)
			{
				printf("Parse failure: message buffer overflow.\n");
			}
			else
			{
				memcpy(msg_buf + msg_offset, prev_rpm_buf, len);
				msg_offset += len;
			}
		}

		prev_msg_address = buf_current_address;

		switch (input[0])
		{
			case 0x1A:
				msg_unk_1a = true;
				end_of_message();
				break;

			case 0:
			case 1:
			case 2:
			case 3:
				end_of_message();
				break;
		}
		break;
	}
}

void TextParser::parse_color(const char input[RPM_BUF_SIZE])
{
	unsigned int offset = 1;
	unsigned int start_offset = offset;
	offset += 4; /* assume color codes are always 4 (hex) digits */

	size_t length = (offset - start_offset);
	std::vector<char> tmpBuffer(length + 1U, 0);
	memcpy(&tmpBuffer[0], input + start_offset, length);

	color = std::strtoull(&tmpBuffer[0], nullptr, 16);
	defer_until_address = buf_current_address + length + 1;
}

void TextParser::parse_hashcode(const char input[RPM_BUF_SIZE])
{
	unsigned int offset = 1;
	unsigned int start_offset = offset;
	while(isdigit(input[offset]))
		++offset;

	size_t length = (offset - start_offset);
	std::vector<char> tmpBuffer(length + 1U, 0);
	memcpy(&tmpBuffer[0], input + start_offset, length);

	// now i is at the offset of the first non-digit character, so we can see whether we parsed a face or a character id.
	char code = input[offset];
	switch (code)
	{
		case 'P':
			character_id = atoi(&tmpBuffer[0]);
			break;

		case 'F':
			face_id = atoi(&tmpBuffer[0]);
			break;

		default:
			printf("Parse failure: unknown hashcode %c, value %u\n", code, (unsigned int)atoi(&tmpBuffer[0]));
	}

	defer_until_address = buf_current_address + length + 2;
}

void TextParser::end_of_message()
{
	flush_message_buffer();

	prev_msg_address = 0;
	parser_state = PARSER_FINISHED;
}

void TextParser::flush_message_buffer()
{
	// this case can occur at the very beginning of a message right after parsing a face ID but before any text has been parsed
	if (msg_offset == 0)
		return; // nothing to do !
	
	// null-terminate the buffer
	msg_buf[msg_offset] = '\0';

	// No change, don't bother updating
	if (memcmp(msg_buf, oldstr, msg_offset) == 0)
		return;

	memcpy(oldstr, msg_buf, msg_offset);

	const UINT CP_SHIFT_JIS = 932;
	const DWORD LCID_JA_JA = 1041;

	wstr = MultiByteToWideChar(CP_UTF8, msg_buf, msg_offset);
	utf8str = WideCharToMultiByte(CP_SHIFT_JIS, wstr);

	HGLOBAL locale_ptr = GlobalAlloc(GMEM_MOVEABLE, sizeof(DWORD));
	DWORD japanese = LCID_JA_JA;
	memcpy(GlobalLock(locale_ptr), &japanese, sizeof(japanese));
	GlobalUnlock(locale_ptr);
	
	// allocate a buffer to give to the system with the clipboard data.
	HGLOBAL clip_buf = GlobalAlloc(GMEM_MOVEABLE | GMEM_ZEROINIT, utf8str.size() + 1);

	// copy the message buffer into the global buffer
	memcpy(GlobalLock(clip_buf), utf8str.c_str(), utf8str.size() + 1);
	GlobalUnlock(clip_buf);

	// Write the data to the clipboard.
	if (!OpenClipboard(nullptr))
	{
		printf("Failed to open clipboard. (Error %d.)\n", GetLastError());
		GlobalFree(locale_ptr);
		GlobalFree(clip_buf);
		goto cleanup;
	}

	if (!EmptyClipboard())
	{
		printf("Failed to empty clipboard. (Error %d.)\n", GetLastError());
		GlobalFree(locale_ptr);
		GlobalFree(clip_buf);
		goto cleanup;
	}

	if (!SetClipboardData(CF_TEXT, clip_buf))
	{
		printf("Failed to set clipboard data. (Error %d.)\n", GetLastError());
		GlobalFree(locale_ptr);
		goto cleanup;
	}

	if(!SetClipboardData(CF_LOCALE, locale_ptr))
	{
		printf("Failed to set clipboard locale. (Error %d.)\n", GetLastError());
		GlobalFree(locale_ptr);
		goto cleanup;
	}

cleanup:
	CloseClipboard();
}
