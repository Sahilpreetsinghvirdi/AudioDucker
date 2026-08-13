#pragma once

#include <windows.h>
#include <shellapi.h>

#include <string>

namespace ducker {

// Message the tray sends to the main window (lParam: mouse message).
inline constexpr UINT kTrayCallbackMessage = WM_APP + 1;

// System-tray icon with generated (GDI+) glyphs: a speaker that changes to show
// ducking activity and a disabled look.
class TrayIcon {
public:
    ~TrayIcon();
    TrayIcon() = default;
    TrayIcon(const TrayIcon&) = delete;
    TrayIcon& operator=(const TrayIcon&) = delete;

    bool Create(HWND hwnd);
    void Destroy();
    void SetTooltip(const std::wstring& tip);
    void SetState(bool ducking, bool enabled);
    void Notify(const std::wstring& title, const std::wstring& body);
    bool Created() const { return created_; }

private:
    void Rebuild();
    void DrawIcons();

    bool created_ = false;
    HWND hwnd_ = nullptr;
    NOTIFYICONDATAW nid_{};
    HICON iconIdle_ = nullptr;
    HICON iconDucking_ = nullptr;
    HICON iconDisabled_ = nullptr;
    bool ducking_ = false;
    bool enabled_ = true;
    std::wstring tooltip_ = L"Audio Ducker";
};

} // namespace ducker
