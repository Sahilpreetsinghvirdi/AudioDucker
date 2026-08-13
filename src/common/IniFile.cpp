#include "common/IniFile.h"

#include "common/Helpers.h"

namespace ducker {

IniFile::IniFile(std::wstring path) : path_(std::move(path)) {}

bool IniFile::Load() {
    data_.clear();

    wchar_t sectionsBuf[16384]{};
    DWORD n = GetPrivateProfileSectionNamesW(sectionsBuf, sizeof(sectionsBuf) / sizeof(wchar_t), path_.c_str());
    if (n == 0) { ok_ = true; return true; } // empty file is valid

    std::vector<std::wstring> sections;
    const wchar_t* p = sectionsBuf;
    while (*p) {
        sections.emplace_back(p);
        p += wcslen(p) + 1;
    }

    for (const auto& section : sections) {
        wchar_t kvBuf[32768]{};
        DWORD m = GetPrivateProfileSectionW(section.c_str(), kvBuf, sizeof(kvBuf) / sizeof(wchar_t), path_.c_str());
        const wchar_t* q = kvBuf;
        while (*q) {
            std::wstring line(q);
            q += wcslen(q) + 1;
            size_t eq = line.find(L'=');
            std::wstring key = (eq == std::wstring::npos) ? line : line.substr(0, eq);
            std::wstring value = (eq == std::wstring::npos) ? L"" : line.substr(eq + 1);
            data_[section][key] = value;
        }
        if (m == 0) data_[section]; // section present but empty
    }

    ok_ = true;
    return true;
}

std::vector<std::wstring> IniFile::Sections() const {
    std::vector<std::wstring> out;
    for (const auto& [s, _] : data_) out.push_back(s);
    return out;
}

std::vector<std::pair<std::wstring, std::wstring>> IniFile::Keys(const std::wstring& section) const {
    std::vector<std::pair<std::wstring, std::wstring>> out;
    auto it = data_.find(section);
    if (it == data_.end()) return out;
    for (const auto& [k, v] : it->second) out.emplace_back(k, v);
    return out;
}

std::wstring IniFile::Get(const std::wstring& section, const std::wstring& key,
                          const std::wstring& def) const {
    auto it = data_.find(section);
    if (it == data_.end()) return def;
    auto kit = it->second.find(key);
    if (kit == it->second.end()) return def;
    return kit->second;
}

void IniFile::Set(const std::wstring& section, const std::wstring& key, const std::wstring& value) {
    data_[section][key] = value;
    WritePrivateProfileStringW(section.c_str(), key.c_str(), value.c_str(), path_.c_str());
}

void IniFile::RemoveKey(const std::wstring& section, const std::wstring& key) {
    auto it = data_.find(section);
    if (it != data_.end()) it->second.erase(key);
    WritePrivateProfileStringW(section.c_str(), key.c_str(), nullptr, path_.c_str());
}

} // namespace ducker
