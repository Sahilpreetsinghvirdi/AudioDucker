#include "ui/TrayIcon.h"

#include <objidl.h>
#include <propidl.h>
#include <gdiplus.h>

#include <memory>

#include "common/Helpers.h"

namespace ducker {

namespace {
using namespace Gdiplus;

ULONG_PTR g_gdiplusToken = 0;
bool g_gdiplusInit = false;

void EnsureGdiPlus() {
    if (g_gdiplusInit) return;
    GdiplusStartupInput input;
    GdiplusStartup(&g_gdiplusToken, &input, nullptr);
    g_gdiplusInit = true;
}

void ShutdownGdiPlus() {
    if (g_gdiplusInit) {
        GdiplusShutdown(g_gdiplusToken);
        g_gdiplusInit = false;
    }
}

// Draws a small "speaker with sound waves" glyph into a bitmap.
void DrawSpeaker(Bitmap* bmp, bool ducking, bool enabled) {
    Graphics g(bmp);
    g.SetSmoothingMode(SmoothingModeAntiAlias);
    g.Clear(Color(0, 0, 0, 0)); // transparent

    int s = bmp->GetWidth();
    float u = s / 32.0f; // scale factor for 32-unit design

    Color body(255, 82, 109, 62);
    Color accent(255, 46, 140, 66);
    if (!enabled) {
        body = Color(255, 130, 130, 130);
        accent = Color(255, 150, 150, 150);
    }

    // Speaker box.
    RectF box(3 * u, 12 * u, 9 * u, 8 * u);
    SolidBrush boxBrush(body);
    g.FillRectangle(&boxBrush, box);

    // Cone.
    PointF cone[3] = {
        PointF(12 * u, 12 * u),
        PointF(20 * u, 7 * u),
        PointF(20 * u, 25 * u),
    };
    SolidBrush coneBrush(accent);
    g.FillPolygon(&coneBrush, cone, 3);

    // Sound waves.
    Pen wavePen(accent, 2.4f * u);
    wavePen.SetLineJoin(LineJoinRound);
    g.DrawArc(&wavePen, 20 * u, 9 * u, 7 * u, 14 * u, -55, 110);
    g.DrawArc(&wavePen, 23 * u, 5 * u, 9 * u, 22 * u, -55, 110);

    if (ducking) {
        // Red "active" badge in the top-right corner.
        SolidBrush redBrush(Color(255, 214, 40, 40));
        float r = 5.5f * u;
        g.FillEllipse(&redBrush, s - 2 * u - r, 2 * u, r * 2, r * 2);
        SolidBrush whiteBrush(Color(255, 255, 255, 255));
        float lw = 1.6f * u;
        g.FillRectangle(&whiteBrush, s - 2 * u - r + lw, 3.0f * u, r * 2 - 2 * lw, 2.2f * u);
    }
}

HICON MakeIcon(int size, bool ducking, bool enabled) {
    Bitmap bmp(size, size, PixelFormat32bppARGB);
    DrawSpeaker(&bmp, ducking, enabled);
    HICON icon = nullptr;
    if (bmp.GetHICON(&icon) != Ok) return nullptr;
    return icon;
}
} // namespace

TrayIcon::~TrayIcon() { Destroy(); }

bool TrayIcon::Create(HWND hwnd) {
    hwnd_ = hwnd;
    EnsureGdiPlus();
    DrawIcons();

    nid_ = {};
    nid_.cbSize = sizeof(NOTIFYICONDATAW);
    nid_.hWnd = hwnd_;
    nid_.uID = 1;
    nid_.uFlags = NIF_ICON | NIF_MESSAGE | NIF_TIP | NIF_SHOWTIP;
    nid_.uCallbackMessage = kTrayCallbackMessage;
    nid_.hIcon = iconIdle_;
    nid_.uTimeout = 4000;
    wcsncpy_s(nid_.szTip, tooltip_.c_str(), _TRUNCATE);

    Shell_NotifyIconW(NIM_ADD, &nid_);
    nid_.uVersion = NOTIFYICON_VERSION_4;
    Shell_NotifyIconW(NIM_SETVERSION, &nid_);
    created_ = true;
    return true;
}

void TrayIcon::Destroy() {
    if (created_) Shell_NotifyIconW(NIM_DELETE, &nid_);
    created_ = false;
    if (iconIdle_) { DestroyIcon(iconIdle_); iconIdle_ = nullptr; }
    if (iconDucking_) { DestroyIcon(iconDucking_); iconDucking_ = nullptr; }
    if (iconDisabled_) { DestroyIcon(iconDisabled_); iconDisabled_ = nullptr; }
    if (g_gdiplusInit && !created_) ShutdownGdiPlus();
}

void TrayIcon::DrawIcons() {
    if (iconIdle_) { DestroyIcon(iconIdle_); iconIdle_ = nullptr; }
    if (iconDucking_) { DestroyIcon(iconDucking_); iconDucking_ = nullptr; }
    if (iconDisabled_) { DestroyIcon(iconDisabled_); iconDisabled_ = nullptr; }
    int size = GetSystemMetrics(SM_CXSMICON);
    iconIdle_ = MakeIcon(size, false, true);
    iconDucking_ = MakeIcon(size, true, true);
    iconDisabled_ = MakeIcon(size, false, false);
}

void TrayIcon::SetTooltip(const std::wstring& tip) {
    tooltip_ = tip;
    Rebuild();
}

void TrayIcon::SetState(bool ducking, bool enabled) {
    ducking_ = ducking;
    enabled_ = enabled;
    Rebuild();
}

void TrayIcon::Rebuild() {
    if (!created_) return;
    nid_.uFlags = NIF_TIP | NIF_ICON | NIF_SHOWTIP;
    nid_.hIcon = !enabled_ ? iconDisabled_ : (ducking_ ? iconDucking_ : iconIdle_);
    wcsncpy_s(nid_.szTip, tooltip_.c_str(), _TRUNCATE);
    Shell_NotifyIconW(NIM_MODIFY, &nid_);
}

void TrayIcon::Notify(const std::wstring& title, const std::wstring& body) {
    if (!created_) return;
    wcsncpy_s(nid_.szInfoTitle, title.c_str(), _TRUNCATE);
    wcsncpy_s(nid_.szInfo, body.c_str(), _TRUNCATE);
    nid_.uFlags = NIF_INFO;
    nid_.dwInfoFlags = NIIF_USER;
    nid_.hBalloonIcon = iconDucking_;
    Shell_NotifyIconW(NIM_MODIFY, &nid_);
}

} // namespace ducker
