#pragma once

#include "common/Helpers.h"

#include <mutex>
#include <string>

namespace ducker {

enum class LogLevel { Info = 0, Warn, Error, Debug };

// Thread-safe file logger writing to %APPDATA%\AudioDucker\ducker.log.
// Output lines follow the shape: [16:42:01] message
class Logger {
public:
    static Logger& Instance();

    void Init(const std::wstring& path, bool verbose);
    void SetVerbose(bool v);
    bool Verbose() const { return verbose_; }
    void SetEchoConsole(bool echo) { echoConsole_ = echo; }

    std::wstring Path() const { return path_; }

    void Log(LogLevel level, const std::string& msg);

    template <typename... Args>
    void Info(Args&&... a) { Log(LogLevel::Info, Str(std::forward<Args>(a)...)); }
    template <typename... Args>
    void Warn(Args&&... a) { Log(LogLevel::Warn, Str(std::forward<Args>(a)...)); }
    template <typename... Args>
    void Error(Args&&... a) { Log(LogLevel::Error, Str(std::forward<Args>(a)...)); }
    template <typename... Args>
    void Debug(Args&&... a) { Log(LogLevel::Debug, Str(std::forward<Args>(a)...)); }

private:
    void WriteLine(const std::string& line);
    void WriteLineRaw(const std::string& line);
    void RotateIfNeeded();

    mutable std::mutex mu_;
    FILE* file_ = nullptr;
    std::wstring path_;
    size_t bytes_ = 0;
    bool verbose_ = false;
    bool echoConsole_ = false;
};

} // namespace ducker
