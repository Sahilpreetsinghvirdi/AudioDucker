#include "common/Helpers.h"

#include <shlobj.h>
#include <tchar.h>
#include <tlhelp32.h>

#include <chrono>
#include <cstdio>
#include <cwctype>
#include <algorithm>

namespace ducker {

std::wstring Utf8ToWide(const std::string& s) {
    if (s.empty()) return {};
    int len = MultiByteToWideChar(CP_UTF8, 0, s.c_str(), (int)s.size(), nullptr, 0);
    std::wstring out(len, L'\0');
    if (len > 0) MultiByteToWideChar(CP_UTF8, 0, s.c_str(), (int)s.size(), &out[0], len);
    return out;
}

std::string WideToUtf8(const std::wstring& s) {
    if (s.empty()) return {};
    int len = WideCharToMultiByte(CP_UTF8, 0, s.c_str(), (int)s.size(), nullptr, 0, nullptr, nullptr);
    std::string out(len, '\0');
    if (len > 0) WideCharToMultiByte(CP_UTF8, 0, s.c_str(), (int)s.size(), &out[0], len, nullptr, nullptr);
    return out;
}

std::string LowerAscii(std::string s) {
    std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) {
        return static_cast<char>(std::tolower(c));
    });
    return s;
}

std::string Trim(std::string s) {
    size_t b = s.find_first_not_of(" \t\r\n");
    if (b == std::string::npos) return {};
    size_t e = s.find_last_not_of(" \t\r\n");
    return s.substr(b, e - b + 1);
}

std::string Percent(float v) {
    if (v < 0.0f) v = 0.0f;
    if (v > 1.0f) v = 1.0f;
    int p = static_cast<int>(v * 100.0f + 0.5f);
    return Str(p, "%");
}

std::string BaseName(const std::string& path) {
    size_t pos = path.find_last_of("/\\");
    return pos == std::string::npos ? path : path.substr(pos + 1);
}

std::wstring ExePathW() {
    wchar_t buf[MAX_PATH]{};
    DWORD n = GetModuleFileNameW(nullptr, buf, MAX_PATH);
    return std::wstring(buf, n);
}

std::string ExePath() { return WideToUtf8(ExePathW()); }

std::wstring AppDataDirW() {
    wchar_t buf[MAX_PATH]{};
    if (SHGetFolderPathW(nullptr, CSIDL_APPDATA, nullptr, SHGFP_TYPE_CURRENT, buf) != S_OK)
        return L".";
    return buf;
}

bool EnsureDir(const std::wstring& path) {
    if (path.empty()) return false;
    if (GetFileAttributesW(path.c_str()) == INVALID_FILE_ATTRIBUTES) {
        // Recursively create parents.
        size_t pos = path.find_last_of(L"/\\");
        if (pos != std::string::npos) {
            std::wstring parent = path.substr(0, pos);
            if (!parent.empty() && GetFileAttributesW(parent.c_str()) == INVALID_FILE_ATTRIBUTES)
                EnsureDir(parent);
        }
        return CreateDirectoryW(path.c_str(), nullptr) != FALSE;
    }
    return true;
}

std::wstring GetConfigDirW() {
    std::wstring dir = AppDataDirW() + L"\\AudioDucker";
    EnsureDir(dir);
    return dir;
}

std::wstring GetConfigFilePathW() { return GetConfigDirW() + L"\\config.ini"; }
std::wstring GetLogFilePathW()    { return GetConfigDirW() + L"\\ducker.log"; }
std::wstring GetHostManifestDirW() { return GetConfigDirW() + L"\\nativehost"; }

std::optional<std::string> ProcessNameByPid(DWORD pid) {
    if (pid == 0) return std::nullopt;
    HANDLE h = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid);
    if (!h) return std::nullopt;
    wchar_t buf[MAX_PATH]{};
    DWORD size = MAX_PATH;
    std::optional<std::string> out;
    if (QueryFullProcessImageNameW(h, 0, buf, &size)) {
        std::wstring full(buf, size);
        size_t pos = full.find_last_of(L"/\\");
        std::wstring name = (pos == std::wstring::npos) ? full : full.substr(pos + 1);
        out = LowerAscii(WideToUtf8(name));
    }
    CloseHandle(h);
    return out;
}

bool ProcessExists(DWORD pid) {
    if (pid == 0) return false;
    HANDLE h = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid);
    if (!h) return false;
    DWORD exitCode = 0;
    BOOL alive = GetExitCodeProcess(h, &exitCode) && exitCode == STILL_ACTIVE;
    CloseHandle(h);
    return alive != FALSE;
}

bool RegReadString(HKEY root, const wchar_t* subkey, const wchar_t* name, std::wstring& out) {
    HKEY key = nullptr;
    if (RegOpenKeyExW(root, subkey, 0, KEY_QUERY_VALUE, &key) != ERROR_SUCCESS) return false;
    wchar_t buf[4096]{};
    DWORD size = sizeof(buf);
    DWORD type = 0;
    LONG r = RegQueryValueExW(key, name, nullptr, &type, reinterpret_cast<BYTE*>(buf), &size);
    RegCloseKey(key);
    if (r != ERROR_SUCCESS || type != REG_SZ) return false;
    out = buf;
    return true;
}

bool RegWriteString(HKEY root, const wchar_t* subkey, const wchar_t* name, const std::wstring& value) {
    HKEY key = nullptr;
    if (RegCreateKeyExW(root, subkey, 0, nullptr, 0, KEY_SET_VALUE, nullptr, &key, nullptr) != ERROR_SUCCESS)
        return false;
    LONG r = RegSetValueExW(key, name, 0, REG_SZ,
                            reinterpret_cast<const BYTE*>(value.c_str()),
                            static_cast<DWORD>((value.size() + 1) * sizeof(wchar_t)));
    RegCloseKey(key);
    return r == ERROR_SUCCESS;
}

bool RegDeleteValue(HKEY root, const wchar_t* subkey, const wchar_t* name) {
    HKEY key = nullptr;
    if (RegOpenKeyExW(root, subkey, 0, KEY_SET_VALUE, &key) != ERROR_SUCCESS) return true; // already gone
    LONG r = RegDeleteValueW(key, name);
    RegCloseKey(key);
    return r == ERROR_SUCCESS || r == ERROR_FILE_NOT_FOUND;
}

namespace {
constexpr const wchar_t* kRunKey = L"Software\\Microsoft\\Windows\\CurrentVersion\\Run";
constexpr const wchar_t* kRunValue = L"AudioDucker";
} // namespace

bool SetRunAtStartup(bool enable, const std::wstring& exePath) {
    if (enable) {
        std::wstring cmd = L"\"" + exePath + L"\" --background";
        return RegWriteString(HKEY_CURRENT_USER, kRunKey, kRunValue, cmd);
    }
    return RegDeleteValue(HKEY_CURRENT_USER, kRunKey, kRunValue);
}

bool GetRunAtStartup(bool& enabled, std::wstring& commandOut) {
    if (RegReadString(HKEY_CURRENT_USER, kRunKey, kRunValue, commandOut)) {
        enabled = true;
        return true;
    }
    enabled = false;
    commandOut.clear();
    return true;
}

int64_t NowMs() {
    using namespace std::chrono;
    return duration_cast<milliseconds>(steady_clock::now().time_since_epoch()).count();
}

} // namespace ducker
