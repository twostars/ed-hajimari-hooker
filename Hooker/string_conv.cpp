#include "stdafx.h"
#include "string_conv.h"

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
