#include "stdafx.h"
#include "string_utils.h"

std::wstring MultiByteToWideChar(UINT code_page, const char* input, size_t input_size)
{
	int requiredSize = MultiByteToWideChar(code_page, 0, input, -1, 0, 0);
	if (requiredSize <= 1) // includes null-terminator, so 1 would still be empty
		return std::wstring();

	wchar_t* wideString = new wchar_t[requiredSize]; // includes null-terminator
	memset(wideString, 0, requiredSize);
	MultiByteToWideChar(code_page, 0, input, -1, wideString, requiredSize);
	std::wstring ret(wideString, (size_t) requiredSize - 1);
	delete[] wideString;
	return ret;
}

std::wstring MultiByteToWideChar(UINT code_page, const std::string& input)
{
	return MultiByteToWideChar(code_page, input.c_str(), input.size());
}

std::string WideCharToMultiByte(UINT code_page, const wchar_t* input, size_t input_size)
{
	int sizeNeeded = WideCharToMultiByte(code_page, 0, input, (int) input_size, nullptr, 0, nullptr, nullptr);
	if (sizeNeeded == 0)
		return std::string();

	std::string encoded(sizeNeeded, 0);
	WideCharToMultiByte(code_page, 0, input, (int) input_size, &encoded[0], sizeNeeded, nullptr, nullptr);
	return encoded;
}

std::string WideCharToMultiByte(UINT code_page, const std::wstring& input)
{
	return WideCharToMultiByte(code_page, input.c_str(), input.size());
}

size_t GetUtf8CharacterSize(const char utf8Char)
{
	// U+0000 to U+007F
	if ((utf8Char & 0x80) == 0x00)
		return 1;

	// U+0080 to U+07FF 
	if ((utf8Char & 0xE0) == 0xC0)
		return 2;

	// U+0800 to U+FFFF 
	if ((utf8Char & 0xF0) == 0xE0)
		return 3;

	if (utf8Char == 0xF0)
		return 4;

	return 0; /* invalid */
}

bool IsJapaneseText(const std::wstring& input)
{
	for (auto itr = input.begin(); itr != input.end(); ++itr)
	{
		auto& c = *itr;

		// \u3040-\u30ff
		if ((c >= L'\u3040' && c <= L'\u30ff')
			// \u3400-\u4dbf
			|| (c >= L'\u3400' && c <= L'\u4dbf')
			// \u4e00-\u9fff
			|| (c >= L'\u4e00' && c <= L'\u9fff')
			// \uf900-\ufaff
			|| (c >= L'\uf900' && c <= L'\ufaff')
			// \uff66-\uff9f
			|| (c >= L'\uff66' && c <= L'\uff9f')
			// \u3131-\ud79d
			|| (c >= L'\u3131' && c <= L'\ud79d'))
			return true;
	}

	return false;
}
