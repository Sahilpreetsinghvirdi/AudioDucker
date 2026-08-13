#pragma once

#include <windows.h>

#include <map>
#include <string>
#include <vector>
#include <utility>

namespace ducker {

// Thin, dependency-free INI store backed by the Windows INI APIs.
// All stored values in Audio Ducker are ASCII (numbers, exe names), so the
// platform profile functions round-trip them reliably.
class IniFile {
public:
    explicit IniFile(std::wstring path);

    bool Load();
    bool Ok() const { return ok_; }

    std::vector<std::wstring> Sections() const;
    std::vector<std::pair<std::wstring, std::wstring>> Keys(const std::wstring& section) const;

    std::wstring Get(const std::wstring& section, const std::wstring& key,
                     const std::wstring& def = L"") const;
    void Set(const std::wstring& section, const std::wstring& key, const std::wstring& value);
    void RemoveKey(const std::wstring& section, const std::wstring& key);

    // The INI APIs write through to the file immediately; this exists for symmetry.
    void Save() const {}

    const std::wstring& Path() const { return path_; }

private:
    std::wstring path_;
    bool ok_ = false;
    std::map<std::wstring, std::map<std::wstring, std::wstring>> data_;
};

} // namespace ducker
