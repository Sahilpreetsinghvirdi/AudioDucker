// AudioDuckerHost.exe - the native messaging host launched by the browser
// extension. It reads Chrome/Edge/Firefox native-messaging messages on stdin
// (4-byte little-endian length + JSON) and forwards the YouTube playback count
// to the running AudioDucker main process via WM_COPYDATA.
//
// If Audio Ducker is not running, the messages are silently ignored - the host
// is just an optional precision helper on top of the built-in audio detection.

#include <windows.h>

#include <cstdint>
#include <string>
#include <vector>

#include "common/JsonMini.h"
#include "detection/NativeProtocol.h"

namespace {

bool ReadAll(HANDLE h, void* buffer, DWORD bytes) {
    auto* p = static_cast<BYTE*>(buffer);
    DWORD total = 0;
    while (total < bytes) {
        DWORD read = 0;
        if (!ReadFile(h, p + total, bytes - total, &read, nullptr)) {
            DWORD err = GetLastError();
            if (err == ERROR_BROKEN_PIPE || err == ERROR_HANDLE_EOF) return false;
            if (read == 0) return false;
        }
        if (read == 0) return false;
        total += read;
    }
    return true;
}

bool WriteAll(HANDLE h, const void* buffer, DWORD bytes) {
    const auto* p = static_cast<const BYTE*>(buffer);
    DWORD total = 0;
    while (total < bytes) {
        DWORD written = 0;
        if (!WriteFile(h, p + total, bytes - total, &written, nullptr) || written == 0)
            return false;
        total += written;
    }
    return true;
}

bool SendMessage(const ducker::native::StateMessage& msg) {
    HWND hwnd = FindWindowW(ducker::native::kWindowClass, nullptr);
    if (!hwnd) return false;
    COPYDATASTRUCT cds;
    cds.dwData = 0;
    cds.cbData = sizeof(msg);
    cds.lpData = const_cast<ducker::native::StateMessage*>(&msg);
    DWORD_PTR result = 0;
    SendMessageTimeoutW(hwnd, WM_COPYDATA, 0, reinterpret_cast<LPARAM>(&cds),
                        SMTO_ABORTIFHUNG, 200, &result);
    return true;
}

} // namespace

int WINAPI wWinMain(HINSTANCE, HINSTANCE, LPWSTR, int) {
    HANDLE stdinHandle = GetStdHandle(STD_INPUT_HANDLE);
    HANDLE stdoutHandle = GetStdHandle(STD_OUTPUT_HANDLE);
    if (stdinHandle == INVALID_HANDLE_VALUE || stdinHandle == nullptr) return 1;

    bool connected = false;
    for (;;) {
        DWORD len = 0;
        if (!ReadAll(stdinHandle, &len, sizeof(len))) break; // pipe closed
        if (len == 0 || len > 1024 * 1024) break;

        std::string payload(len, '\0');
        if (!ReadAll(stdinHandle, payload.data(), len)) break;

        ducker::JsonValue json;
        if (!ducker::ParseJson(payload, json)) continue;

        std::string type = json.AsString("type");
        if (type == "youtube-count") {
            int count = json.AsInt("count", 0);
            ducker::native::StateMessage msg{ducker::native::kMagic, 1, count};
            if (SendMessage(msg)) connected = true;
        } else if (type == "ping") {
            std::string reply = "{\"type\":\"pong\"}\n";
            if (stdoutHandle && stdoutHandle != INVALID_HANDLE_VALUE) {
                DWORD n = static_cast<DWORD>(reply.size());
                WriteAll(stdoutHandle, &n, sizeof(n));
                WriteAll(stdoutHandle, reply.data(), static_cast<DWORD>(reply.size()));
            }
        }
    }

    // Let the main app know this browser is going away (count 0) on clean exit.
    ducker::native::StateMessage bye{ducker::native::kMagic, 1, 0};
    SendMessage(bye);
    return 0;
}
