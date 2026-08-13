#pragma once

#include <windows.h>

#include <sstream>
#include <string>
#include <optional>
#include <cstdint>

namespace ducker {

// --- Encoding helpers ----------------------------------------------------
std::wstring Utf8ToWide(const std::string& s);
std::string WideToUtf8(const std::wstring& s);

// --- String helpers ------------------------------------------------------
std::string LowerAscii(std::string s);
std::string Trim(std::string s);
std::string Percent(float v);   // e.g. "73%" (rounded, no decimals)
std::string BaseName(const std::string& path); // keeps the final path component

// Variadic string builder: concatenates the stream output of every argument
// with no separator. e.g. Str("value=", x) -> "value=0.25"
template <typename... Args>
std::string Str(Args&&... args) {
    std::ostringstream oss;
    (oss << ... << std::forward<Args>(args));
    return oss.str();
}

// --- Paths ----------------------------------------------------------------
std::wstring ExePathW();
std::string ExePath();
std::wstring AppDataDirW();
std::wstring GetConfigDirW();          // %APPDATA%\AudioDucker (created on demand)
std::wstring GetConfigFilePathW();     // %APPDATA%\AudioDucker\config.ini
std::wstring GetLogFilePathW();        // %APPDATA%\AudioDucker\ducker.log
std::wstring GetHostManifestDirW();    // %APPDATA%\AudioDucker\nativehost
bool EnsureDir(const std::wstring& path);

// --- Processes -------------------------------------------------------------
std::optional<std::string> ProcessNameByPid(DWORD pid); // lowercase exe file name
bool ProcessExists(DWORD pid);

// --- Registry ---------------------------------------------------------------
bool RegReadString(HKEY root, const wchar_t* subkey, const wchar_t* name, std::wstring& out);
bool RegWriteString(HKEY root, const wchar_t* subkey, const wchar_t* name, const std::wstring& value);
bool RegDeleteValue(HKEY root, const wchar_t* subkey, const wchar_t* name);

// --- Startup (HKCU Run key) --------------------------------------------------
bool SetRunAtStartup(bool enable, const std::wstring& exePath);
bool GetRunAtStartup(bool& enabled, std::wstring& commandOut);

// --- Misc --------------------------------------------------------------------
int64_t NowMs(); // monotonic clock, milliseconds

} // namespace ducker
