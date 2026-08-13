#include "utils/Logger.h"

#include <ctime>
#include <cstdio>

#include <io.h>
#include <share.h>

namespace ducker {

namespace {
constexpr size_t kMaxLogBytes = 512 * 1024; // rotate at 512 KB

std::string Timestamp() {
    std::time_t t = std::time(nullptr);
    std::tm tm{};
    localtime_s(&tm, &t);
    char buf[16];
    snprintf(buf, sizeof(buf), "%02d:%02d:%02d", tm.tm_hour, tm.tm_min, tm.tm_sec);
    return buf;
}

const char* LevelPrefix(LogLevel l) {
    switch (l) {
        case LogLevel::Warn: return "[W] ";
        case LogLevel::Error: return "[E] ";
        case LogLevel::Debug: return "[D] ";
        default: return "";
    }
}
} // namespace

Logger& Logger::Instance() {
    static Logger instance;
    return instance;
}

void Logger::Init(const std::wstring& path, bool verbose) {
    std::lock_guard<std::mutex> lock(mu_);
    path_ = path;
    verbose_ = verbose;
    if (file_) { fclose(file_); file_ = nullptr; }
    bytes_ = 0;
    if (!path_.empty()) {
        file_ = _wfsopen(path_.c_str(), L"ab", _SH_DENYNO);
        if (file_) {
            long sz = 0;
            if (_fseeki64(file_, 0, SEEK_END) == 0) {
                sz = static_cast<long>(_ftelli64(file_));
                bytes_ = sz > 0 ? static_cast<size_t>(sz) : 0;
            }
        }
        if (bytes_ > kMaxLogBytes) {
            fclose(file_);
            file_ = nullptr;
            std::wstring old = path_ + L".1";
            _wremove(old.c_str());
            _wrename(path_.c_str(), old.c_str());
            file_ = _wfsopen(path_.c_str(), L"ab", _SH_DENYNO);
            bytes_ = 0;
        }
    }
    if (file_) WriteLineRaw("----- Audio Ducker log started -----");
}

void Logger::SetVerbose(bool v) {
    std::lock_guard<std::mutex> lock(mu_);
    verbose_ = v;
}

void Logger::Log(LogLevel level, const std::string& msg) {
    bool isDebug = (level == LogLevel::Debug);
    {
        std::lock_guard<std::mutex> lock(mu_);
        if (isDebug && !verbose_) return;
    }
    std::string line = "[" + Timestamp() + "] " + LevelPrefix(level) + msg;
    WriteLine(line);
    if (echoConsole_) {
        std::lock_guard<std::mutex> lock(mu_);
        HANDLE h = GetStdHandle(STD_OUTPUT_HANDLE);
        if (h && h != INVALID_HANDLE_VALUE) {
            std::string out = line + "\n";
            DWORD written = 0;
            WriteFile(h, out.c_str(), (DWORD)out.size(), &written, nullptr);
        }
    }
}

void Logger::WriteLine(const std::string& line) {
    std::lock_guard<std::mutex> lock(mu_);
    WriteLineRaw(line);
}

void Logger::WriteLineRaw(const std::string& line) {
    if (!file_) return;
    if (bytes_ > kMaxLogBytes) RotateIfNeeded();
    std::string out = line + "\r\n";
    size_t written = fwrite(out.data(), 1, out.size(), file_);
    bytes_ += written;
    fflush(file_);
}

void Logger::RotateIfNeeded() {
    if (bytes_ <= kMaxLogBytes || !file_) return;
    fclose(file_);
    file_ = nullptr;
    std::wstring old = path_ + L".1";
    _wremove(old.c_str());
    _wrename(path_.c_str(), old.c_str());
    file_ = _wfsopen(path_.c_str(), L"ab", _SH_DENYNO);
    bytes_ = 0;
}

} // namespace ducker
