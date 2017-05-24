#pragma once

#include <vector>
#include <codecvt>

namespace utils {

static inline std::wstring wstring_convert_from_bytes(const std::vector<uint8_t>& v)
{
	std::string str(v.begin(), v.end());
	std::wstring_convert<std::codecvt_utf8<wchar_t>, wchar_t> converter;
	return converter.from_bytes(str.data());
}
static inline std::wstring wstring_convert_from_bytes(std::string& v)
{
	std::wstring_convert<std::codecvt_utf8<wchar_t>, wchar_t> converter;
	return converter.from_bytes(v.data());
}
static inline std::vector<uint8_t> wstring_convert_to_bytes(const wchar_t* str)
{
	std::wstring_convert<std::codecvt_utf8<wchar_t>, wchar_t> converter;
	std::string string = converter.to_bytes(str);
	return std::vector<uint8_t>(string.begin(), string.end());
}
static inline std::vector<uint8_t> wstring_convert_to_bytes(std::wstring& str)
{
	std::wstring_convert<std::codecvt_utf8<wchar_t>, wchar_t> converter;
	std::string string = converter.to_bytes(str);
	return std::vector<uint8_t>(string.begin(), string.end());
}
static inline std::string to_utf8(std::wstring& str)
{
	std::wstring_convert<std::codecvt_utf8<wchar_t>, wchar_t> converter;
	return converter.to_bytes(str);
}
static inline std::string to_utf8(const wchar_t* str)
{
	std::wstring_convert<std::codecvt_utf8<wchar_t>, wchar_t> converter;
	return converter.to_bytes(str);
}

static inline std::string win32_error_to_ansi(DWORD errorCode)
{
	LPSTR messageBuffer = nullptr;
	size_t size = ::FormatMessageA(FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS
		, NULL
		, errorCode
		, MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT)
		, (LPSTR)&messageBuffer
		, 0
		, NULL);

	std::string message(messageBuffer, size);

	//Free the buffer.
	::LocalFree(messageBuffer);

	return message;
}

} // end of namespace utils