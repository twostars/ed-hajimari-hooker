#include "stdafx.h"
#include "TextParser.h"

thread_local char oldstr[MSG_BUF_SIZE + 1] = {};
thread_local wchar_t wstr[MSG_BUF_SIZE + 1] = {};
thread_local char utf8str[MSG_BUF_SIZE + 1] = {};

void TextParser::parse(const char* buf)
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
		switch (buf[0])
		{
			case '':
				parser_state = PARSER_READ_COLOR;
				break;

			case '#':
				parser_state = PARSER_READ_HASHCODE;
				parse(buf);
				break;

			default:
				parser_state = PARSER_TEXT;
				parse(buf);
		}
		break;

	case PARSER_READ_COLOR:
		parse_color(buf);
		parser_state = PARSER_READY;
		break;

	case PARSER_READ_HASHCODE:
		parse_hashcode(buf);
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

		switch (buf[0])
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

	case PARSER_FINISHED:
		break;
	}
}

void TextParser::parse_color(const char* buf)
{
	unsigned int offset = 1;
	unsigned int start_offset = offset;
	offset += 4; /* assume color codes are always 4 (hex) digits */

	size_t length = (offset - start_offset);
	std::vector<char> tmpBuffer(length + 1U, 0);
	memcpy(&tmpBuffer[0], buf + start_offset, length);

	color = std::strtoull(&tmpBuffer[0], nullptr, 16);
	defer_until_address = buf_current_address + length + 1;
}

void TextParser::parse_hashcode(const char* buf)
{
	unsigned int offset = 1;
	unsigned int start_offset = offset;
	while(isdigit(buf[offset]))
		++offset;

	size_t length = (offset - start_offset);
	std::vector<char> tmpBuffer(length + 1U, 0);
	memcpy(&tmpBuffer[0], buf + start_offset, length);

	// now i is at the offset of the first non-digit character, so we can see whether we parsed a face or a character id.
	char code = buf[offset];
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

	int wchars_num = MultiByteToWideChar(CP_UTF8, 0, msg_buf, -1, nullptr, 0);
	if (wchars_num > MSG_BUF_SIZE)
		return;

	MultiByteToWideChar(CP_UTF8, 0, msg_buf, -1, wstr, wchars_num);

	int utf8chars_num = WideCharToMultiByte(CP_SHIFT_JIS, 0, wstr, -1, nullptr, 0, nullptr, nullptr);
	if (utf8chars_num > MSG_BUF_SIZE)
		return;

	WideCharToMultiByte(CP_SHIFT_JIS, 0, wstr, wchars_num, utf8str, utf8chars_num, nullptr, nullptr);

	HGLOBAL locale_ptr = GlobalAlloc(GMEM_MOVEABLE, sizeof(DWORD));
	DWORD japanese = LCID_JA_JA;
	memcpy(GlobalLock(locale_ptr), &japanese, sizeof(japanese));
	GlobalUnlock(locale_ptr);
	
	// allocate a buffer to give to the system with the clipboard data.
	HGLOBAL clip_buf = GlobalAlloc(GMEM_MOVEABLE | GMEM_ZEROINIT, utf8chars_num);

	// copy the message buffer into the global buffer
	memcpy(GlobalLock(clip_buf), utf8str, utf8chars_num);
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
