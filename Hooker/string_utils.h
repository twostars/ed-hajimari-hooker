#pragma once

#include <string>

std::wstring MultiByteToWideChar(UINT code_page, const char* input, size_t input_size);
std::wstring MultiByteToWideChar(UINT code_page, const std::string& input);
std::string WideCharToMultiByte(UINT code_page, const wchar_t* input, size_t input_size);
std::string WideCharToMultiByte(UINT code_page, const std::wstring& input);

size_t GetUtf8CharacterSize(const char utf8Char);
bool IsJapaneseText(const std::wstring& input);
