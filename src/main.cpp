#include <windows.h>
#include <commctrl.h>
#include <shellapi.h>

#include <atomic>
#include <string>
#include <thread>

#include "audio/AudioSessionManager.h"
#include "common/Helpers.h"
#include "common/Version.h"
#include "config/ConfigManager.h"
#include "detection/NativeProtocol.h"
#include "ducking/DuckingManager.h"
#include "ui/SettingsWindow.h"
#include "ui/TrayIcon.h"
#include "utils/Logger.h"

namespace {

using namespace ducker;

// ---------------------------------------------------------------------------
// messages / ids
// ---------------------------------------------------------------------------
constexpr UINT WM_AUDIO_NOTIFY = WM_APP + 2;   // audio thread -> balloon (wParam 1|2)
constexpr UINT WM_OPEN_SETTINGS = WM_APP + 3;  // tray menu -> settings dialog
constexpr UINT WM_APP_EXIT = WM_APP + 4;       // request clean exit
UINT g_taskbarCreatedMsg = 0;

enum MenuId {
    IDM_TOGGLE_ENABLE = 4001,
    IDM_DUCK_NOW,
    IDM_RESTORE_NOW,
    IDM_SETTINGS,
    IDM_OPEN_LOG,
    IDM_ABOUT,
    IDM_EXIT,
};

struct AppContext {
    TrayIcon tray;
    AudioSessionManager audio;
    DuckingManager* ducking = nullptr;
    ConfigManager* config = nullptr;
};

AppContext* g_ctx = nullptr;

std::wstring BuildTooltip(const AppContext& ctx) {
    std::wstring tip = L"Audio Ducker";
    if (!ctx.ducking->IsEnabled()) {
        tip += L" - disabled";
    } else if (ctx.ducking->IsDucking()) {
        tip += L" - ducking background audio";
        int n = ctx.ducking->ActiveSourceCount();
        if (n > 0) tip += L" (" + std::to_wstring(n) + L" source(s))";
    } else {
        tip += L" - running";
    }
    return tip;
}

void UpdateTray(AppContext& ctx) {
    ctx.tray.SetState(ctx.ducking->IsDucking(), ctx.ducking->IsEnabled());
    ctx.tray.SetTooltip(BuildTooltip(ctx));
}

HMENU BuildMenu(AppContext& ctx) {
    HMENU menu = CreatePopupMenu();
    AppendMenuW(menu, MF_STRING | MF_ENABLED, IDM_TOGGLE_ENABLE,
                ctx.ducking->IsEnabled() ? L"Disable" : L"Enable");
    AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(menu, MF_STRING | MF_ENABLED, IDM_DUCK_NOW, L"Duck now");
    AppendMenuW(menu, MF_STRING | MF_ENABLED, IDM_RESTORE_NOW, L"Restore volumes");
    AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(menu, MF_STRING | MF_ENABLED, IDM_SETTINGS, L"Settings...");
    AppendMenuW(menu, MF_STRING | MF_ENABLED, IDM_OPEN_LOG, L"Open log file");
    AppendMenuW(menu, MF_STRING | MF_ENABLED, IDM_ABOUT, L"About");
    AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(menu, MF_STRING | MF_ENABLED, IDM_EXIT, L"Exit");
    return menu;
}

void OpenSettings(AppContext& ctx, HWND hwnd) {
    AppSettings current = ctx.config->Get();
    ShowSettingsWindow(hwnd, current, SettingsCallbacks{
        [&ctx](const AppSettings& s) {
            ctx.config->Set(s);
            ctx.ducking->Configure(s);
            Logger::Instance().SetVerbose(s.verboseLogging);
        },
        [&ctx]() -> std::vector<DiscoveredApp> {
            auto pairs = ctx.audio.QueryActiveAppNames();
            std::vector<DiscoveredApp> apps;
            for (const auto& [display, proc] : pairs)
                apps.push_back(DiscoveredApp{LowerAscii(proc), display});
            return apps;
        },
    });
}

void ShowAbout(HWND hwnd) {
    std::wstring text =
        L"Audio Ducker " AUDIO_DUCKER_VERSION_STRING_W L"\n\n"
        L"Automatically lowers the volume of background media applications "
        L"while YouTube (or other browser audio) is playing, then restores "
        L"them smoothly.\n\n"
        L"Works at the Windows per-application audio-session level.";
    MessageBoxW(hwnd, text.c_str(), L"About Audio Ducker", MB_OK | MB_ICONINFORMATION);
}

// ---------------------------------------------------------------------------
// window procedure
// ---------------------------------------------------------------------------
LRESULT CALLBACK MainWndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    AppContext* ctx = g_ctx;
    if (!ctx && msg != WM_NCCREATE) return DefWindowProcW(hwnd, msg, wParam, lParam);

    switch (msg) {
        case WM_TIMER:
            if (ctx) UpdateTray(*ctx);
            return 0;

        case kTrayCallbackMessage: {
            // NOTIFYICON_VERSION_4 packs the event in the low word of lParam.
            UINT event = LOWORD(lParam);
            if (event == WM_RBUTTONUP || event == WM_CONTEXTMENU) {
                HMENU menu = BuildMenu(*ctx);
                POINT pt{};
                GetCursorPos(&pt);
                SetForegroundWindow(hwnd);
                UINT cmd = TrackPopupMenu(menu, TPM_RIGHTBUTTON | TPM_RETURNCMD, pt.x, pt.y, 0, hwnd,
                                          nullptr);
                DestroyMenu(menu);
                PostMessageW(hwnd, WM_NULL, 0, 0); // dismiss the menu cleanly
                if (cmd != 0) PostMessageW(hwnd, WM_COMMAND, cmd, 0);
                return 0;
            }
            if (event == WM_LBUTTONUP) {
                // Single left click: open the settings window.
                PostMessageW(hwnd, WM_OPEN_SETTINGS, 0, 0);
                return 0;
            }
            return 0;
        }

        case WM_COMMAND: {
            switch (LOWORD(wParam)) {
                case IDM_TOGGLE_ENABLE:
                    ctx->ducking->SetEnabled(!ctx->ducking->IsEnabled());
                    UpdateTray(*ctx);
                    break;
                case IDM_DUCK_NOW:
                    ctx->ducking->ForceDuck();
                    break;
                case IDM_RESTORE_NOW:
                    ctx->ducking->ForceRestore();
                    break;
                case IDM_SETTINGS:
                    PostMessageW(hwnd, WM_OPEN_SETTINGS, 0, 0);
                    break;
                case IDM_OPEN_LOG: {
                    std::wstring logPath = GetLogFilePathW();
                    // Ensure the file exists so the viewer opens cleanly.
                    Logger::Instance().Log(LogLevel::Info, "---");
                    ShellExecuteW(hwnd, L"open", logPath.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
                    break;
                }
                case IDM_ABOUT:
                    ShowAbout(hwnd);
                    break;
                case IDM_EXIT:
                    PostMessageW(hwnd, WM_APP_EXIT, 0, 0);
                    break;
                default:
                    break;
            }
            return 0;
        }

        case WM_OPEN_SETTINGS:
            OpenSettings(*ctx, hwnd);
            UpdateTray(*ctx);
            return 0;

        case WM_AUDIO_NOTIFY:
            if (wParam == 1) {
                ctx->tray.Notify(L"Audio Ducker",
                                 L"Playback detected - background audio lowered.");
            } else if (wParam == 2) {
                ctx->tray.Notify(L"Audio Ducker",
                                 L"Playback stopped - background audio restored.");
            }
            return 0;

        case WM_COPYDATA: {
            auto* cds = reinterpret_cast<COPYDATASTRUCT*>(lParam);
            if (cds && cds->cbData == sizeof(native::StateMessage)) {
                auto* nativeMsg = reinterpret_cast<const native::StateMessage*>(cds->lpData);
                if (nativeMsg->magic == native::kMagic) {
                    DWORD senderPid = 0;
                    GetWindowThreadProcessId(reinterpret_cast<HWND>(wParam), &senderPid);
                    ctx->ducking->SetExtensionCount(senderPid, nativeMsg->count);
                }
            }
            return 0;
        }

        case WM_QUERYENDSESSION:
        case WM_ENDSESSION:
            // Never leave volumes reduced if the user is logging off.
            ctx->ducking->ForceRestore();
            Sleep(800);
            return msg == WM_QUERYENDSESSION ? TRUE : 0;

        case WM_APP_EXIT:
            DestroyWindow(hwnd);
            return 0;

        case WM_DESTROY:
            PostQuitMessage(0);
            return 0;

        default:
            break;
    }

    if (msg == g_taskbarCreatedMsg) {
        // Explorer restarted: recreate the tray icon.
        if (ctx) {
            ctx->tray.Destroy();
            ctx->tray.Create(hwnd);
            UpdateTray(*ctx);
        }
        return 0;
    }

    return DefWindowProcW(hwnd, msg, wParam, lParam);
}

bool EnsureSingleInstance() {
    HANDLE mutex = CreateMutexW(nullptr, FALSE, L"Local\\AudioDucker_SingletonMutex");
    (void)mutex;
    if (GetLastError() == ERROR_ALREADY_EXISTS) {
        HWND existing = FindWindowW(native::kWindowClass, nullptr);
        if (existing) {
            ShowWindow(existing, SW_SHOW);
            PostMessageW(existing, WM_OPEN_SETTINGS, 0, 0);
        }
        return false;
    }
    return true;
}

} // namespace

int WINAPI wWinMain(_In_ HINSTANCE hInstance, _In_opt_ HINSTANCE hPrevInstance,
                    _In_ LPWSTR lpCmdLine, _In_ int nCmdShow) {
    std::wstring cmdline = lpCmdLine ? lpCmdLine : L"";

    bool consoleMode = cmdline.find(L"--console") != std::wstring::npos;
    bool debugMode = cmdline.find(L"--debug") != std::wstring::npos;

    if (!EnsureSingleInstance()) return 0;

    if (consoleMode) {
        AllocConsole();
        FILE* f = nullptr;
        freopen_s(&f, "CONOUT$", "w", stdout);
        Logger::Instance().SetEchoConsole(true);
    }

    ConfigManager config(GetConfigFilePathW());
    config.Load();
    AppSettings settings = config.Get();
    if (debugMode) settings.verboseLogging = true;

    Logger::Instance().Init(GetLogFilePathW(), settings.verboseLogging);
    Logger::Instance().Info("Audio Ducker v", AUDIO_DUCKER_VERSION_STRING, " starting (pid ",
                            GetCurrentProcessId(), ")");

    // Apply the startup-run setting so the registry stays consistent.
    config.Save();

    INITCOMMONCONTROLSEX icc{sizeof(icc), ICC_LISTVIEW_CLASSES};
    InitCommonControlsEx(&icc);

    WNDCLASSW wc{};
    wc.lpfnWndProc = MainWndProc;
    wc.hInstance = hInstance;
    wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    wc.lpszClassName = native::kWindowClass;
    wc.hIcon = LoadIconW(hInstance, MAKEINTRESOURCE(1));
    RegisterClassW(&wc);

    HWND hwnd = CreateWindowExW(0, native::kWindowClass, L"Audio Ducker", 0,
                                0, 0, 0, 0, nullptr, nullptr, hInstance, nullptr);
    if (!hwnd) {
        MessageBoxW(nullptr, L"Failed to create the Audio Ducker window.", L"Audio Ducker",
                    MB_OK | MB_ICONERROR);
        return 1;
    }
    g_taskbarCreatedMsg = RegisterWindowMessageW(L"TaskbarCreated");

    AppContext ctx;
    g_ctx = &ctx;
    ctx.config = &config;
    ctx.ducking = new DuckingManager(Logger::Instance(), ctx.audio.Controller());

    // Wire detection + audio.
    ctx.ducking->Configure(settings);
    ctx.audio.SetCallbacks(AudioCallbacks{
        [ducking = ctx.ducking](const SessionInfo& info, AudioSession* session) {
            ducking->OnSessionAdded(info, session);
        },
        [ducking = ctx.ducking](const std::string& id) { ducking->OnSessionRemoved(id); },
        [ducking = ctx.ducking](const std::string& id, float v, bool m) {
            ducking->OnVolumeChanged(id, v, m);
        },
        [ducking = ctx.ducking](const std::string& id, bool active) {
            ducking->OnStateChanged(id, active);
        },
        [ducking = ctx.ducking]() { ducking->OnDeviceChanged(); },
        [ducking = ctx.ducking](int64_t nowMs) { ducking->OnTick(nowMs); },
    });
    ctx.ducking->Attach(&ctx.audio);
    ctx.ducking->SetNotifyCallback([hwnd](int eventId) {
        PostMessageW(hwnd, WM_AUDIO_NOTIFY, static_cast<WPARAM>(eventId), 0);
    });

    ctx.tray.Create(hwnd);
    UpdateTray(ctx);
    ctx.audio.Start();

    SetTimer(hwnd, 1, 500, nullptr);

    Logger::Instance().Info("Running in the system tray.");

    // Message pump.
    MSG msg;
    while (GetMessageW(&msg, nullptr, 0, 0) > 0) {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }

    Logger::Instance().Info("Shutting down.");
    ctx.audio.Stop();
    ctx.tray.Destroy();
    delete ctx.ducking;
    g_ctx = nullptr;
    return 0;
}
