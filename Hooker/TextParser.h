#pragma once

#include "constants.h"

enum ParserState
{
	PARSER_READY,
	PARSER_READ_COLOR,
	PARSER_READ_HASHCODE,
	PARSER_TEXT,
	PARSER_FINISHED
};

struct TextParser
{
	// We don't know this until we're done with parsing
	// as it's incrementally added to throughout the loop.
	// However this will be useful for debug.
	std::string OriginalInputString;
	DWORD_PTR PreviousAddress = 0;

	DWORD_PTR buf_current_address = 0;
	DWORD_PTR defer_until_address = 0;

	unsigned int color = 0xffffffff;
	
	int face_id = -1;
	int character_id = -1;

	DWORD_PTR prev_msg_address = 0;
	char prev_rpm_buf[RPM_BUF_SIZE] = {};

	DWORD_PTR msg_start_address = 0;
	char msg_buf[MSG_BUF_SIZE + 1] = {};
	unsigned int msg_offset = 0;

	bool msg_unk_1a = false;

	enum ParserState parser_state = PARSER_READY;

	// A single input string can create multiple messages.
	// Usually this is for multiple lines which we don't care about,
	// but there are some special cases - such as the hiragana
	// superscript explaining the more complex kanji.

	struct Message
	{
		uint16_t EOMType = 0; // the character supplied to specify the end of message
		std::string Data;
		std::vector<char> Flags;
	};

	Message CurrentMessage;
	std::vector<Message> Messages;

	void Parse(const char input[RPM_BUF_SIZE]);
	void Parse_MessageFlag(char flag);
	void Parse_EndOfMessage(uint16_t type);
	void Parse_HashCode(const char input[RPM_BUF_SIZE]);
	void Parse_Text(const char input[RPM_BUF_SIZE]);
	void FinishParsing();
	void CopyToClipboard();

	void parse(const char input[RPM_BUF_SIZE]);
	void parse_color(const char input[RPM_BUF_SIZE]);
	void parse_hashcode(const char input[RPM_BUF_SIZE]);
	void end_of_message();
	void flush_message_buffer();
};
