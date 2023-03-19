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

	void parse(const char input[RPM_BUF_SIZE]);
	void parse_color(const char input[RPM_BUF_SIZE]);
	void parse_hashcode(const char input[RPM_BUF_SIZE]);
	void end_of_message();
	void flush_message_buffer();
};
