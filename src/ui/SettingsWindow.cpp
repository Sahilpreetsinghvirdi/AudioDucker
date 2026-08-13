#include "ui/SettingsWindow.h"

#include <commctrl.h>

#include <cstdlib>
#include <map>
#include <set>

#include "common/Helpers.h"
#include "common/Version.h"

namespace ducker {

namespace {

constexpr int kDuckPresets[] = {5, 10, 15, 20, 25, 30, 40};
constexpr int kFadePresets[] = {0, 250, 500, 750, 1000};

// Control ids
enum {
    IDC_DUCK_COMBO = 1001,
    IDC_DUCK_CUSTOM,
    IDC_DUCK_CUSTOM_PCT,
    IDC_FADE_DOWN,
    IDC_FADE_UP,
    IDC_BROWSER_CHROME,
    IDC_BROWSER_EDGE,
    IDC_BROWSER_FIREFOX,
    IDC_USE_AUDIO,
    IDC_LIST_APPS,
    IDC_BTN_DISCOVER,
    IDC_BTN_REMOVE,
    IDC_DUCK_ALL,
    IDC_STARTUP,
    IDC_VERBOSE,
    IDC_EXT_ID,
    IDC_BTN_REGISTER,
    IDC_BTN_APPLY,
    IDC_BTN_OK,
    IDC_BTN_CANCEL,
};

struct DialogData {
    AppSettings settings;
    SettingsCallbacks callbacks;
    std::vector<DiscoveredApp> apps;
    std::map<std::string, std::wstring> displayNames;
    HFONT font = nullptr;
    bool closed = false;
};

AppSettings CollectSettings(HWND hwnd, DialogData* data);
std::set<std::string, std::less<>> CollectCheckedApps(HWND list);

HFONT MakeUiFont(HWND hwnd) {
    int dpi = GetDpiForWindow(hwnd);
    int height = -MulDiv(9, dpi, 72);
    return CreateFontW(height, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE, DEFAULT_CHARSET,
                       OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
                       DEFAULT_PITCH | FF_DONTCARE, L"Segoe UI");
}

int Scaled(int v, HWND hwnd) {
    int dpi = GetDpiForWindow(hwnd);
    return MulDiv(v, dpi, 96);
}

void SetFontForChildren(HWND dlg, HFONT font) {
    EnumChildWindows(dlg, [](HWND child, LPARAM lp) -> BOOL {
        SendMessageW(child, WM_SETFONT, static_cast<WPARAM>(lp), TRUE);
        return TRUE;
    }, reinterpret_cast<LPARAM>(font));
}

HWND MakeStatic(HWND parent, int id, const wchar_t* text, int x, int y, int w, int h) {
    return CreateWindowExW(0, L"STATIC", text, WS_CHILD | WS_VISIBLE, x, y, w, h, parent,
                           reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)), GetModuleHandleW(nullptr), nullptr);
}

HWND MakeCheck(HWND parent, int id, const wchar_t* text, int x, int y, int w, int h) {
    return CreateWindowExW(0, L"BUTTON", text, WS_CHILD | WS_VISIBLE | BS_AUTOCHECKBOX,
                           x, y, w, h, parent, reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)),
                           GetModuleHandleW(nullptr), nullptr);
}

HWND MakeButton(HWND parent, int id, const wchar_t* text, int x, int y, int w, int h, DWORD extra = 0) {
    DWORD style = WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_PUSHBUTTON;
    return CreateWindowExW(0, L"BUTTON", text, style | extra, x, y, w, h, parent,
                           reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)), GetModuleHandleW(nullptr), nullptr);
}

HWND MakeCombo(HWND parent, int id, int x, int y, int w) {
    return CreateWindowExW(0, L"COMBOBOX", L"", WS_CHILD | WS_VISIBLE | WS_TABSTOP | CBS_DROPDOWNLIST | WS_VSCROLL,
                           x, y, w, 220, parent, reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)),
                           GetModuleHandleW(nullptr), nullptr);
}

HWND MakeEdit(HWND parent, int id, const wchar_t* text, int x, int y, int w, int h) {
    return CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", text,
                           WS_CHILD | WS_VISIBLE | WS_TABSTOP | ES_AUTOHSCROLL | ES_NUMBER,
                           x, y, w, h, parent, reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)),
                           GetModuleHandleW(nullptr), nullptr);
}

void AddComboItem(HWND combo, const wchar_t* text) {
    SendMessageW(combo, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(text));
}

int ComboFind(HWND combo, const wchar_t* text) {
    return (int)SendMessageW(combo, CB_FINDSTRINGEXACT, (WPARAM)-1, reinterpret_cast<LPARAM>(text));
}

void PopulateDuckCombo(HWND combo, float duckVolume) {
    int preset = -1;
    for (int p : kDuckPresets)
        if (duckVolume >= (p - 0.5f) / 100.0f && duckVolume <= (p + 0.5f) / 100.0f) preset = p;

    for (int p : kDuckPresets) AddComboItem(combo, (Utf8ToWide(std::to_string(p)) + L"%").c_str());
    AddComboItem(combo, L"Custom");

    int sel = preset >= 0 ? ComboFind(combo, (Utf8ToWide(std::to_string(preset)) + L"%").c_str()) : -1;
    SendMessageW(combo, CB_SETCURSEL, preset >= 0 ? sel : (WPARAM)-1, 0);
    if (preset < 0) {
        // Select "Custom" if present.
        int custom = ComboFind(combo, L"Custom");
        SendMessageW(combo, CB_SETCURSEL, custom >= 0 ? custom : 0, 0);
    }
}

void PopulateFadeCombo(HWND combo, int value) {
    for (int p : kFadePresets) AddComboItem(combo, Utf8ToWide(std::to_string(p)).c_str());
    int idx = ComboFind(combo, Utf8ToWide(std::to_string(value)).c_str());
    if (idx < 0) idx = 0;
    SendMessageW(combo, CB_SETCURSEL, idx, 0);
}

int ComboValue(HWND combo) {
    int idx = (int)SendMessageW(combo, CB_GETCURSEL, 0, 0);
    if (idx < 0 || idx >= (int)(sizeof(kFadePresets) / sizeof(kFadePresets[0]))) return 500;
    return kFadePresets[idx];
}

void RefreshCustomEdit(HWND dlg, HWND duckCombo) {
    int sel = (int)SendMessageW(duckCombo, CB_GETCURSEL, 0, 0);
    bool custom = sel == (int)(sizeof(kDuckPresets) / sizeof(kDuckPresets[0]));
    EnableWindow(GetDlgItem(dlg, IDC_DUCK_CUSTOM), custom ? TRUE : FALSE);
    EnableWindow(GetDlgItem(dlg, IDC_DUCK_CUSTOM_PCT), custom ? TRUE : FALSE);
}

// --- ListView helpers ------------------------------------------------------

void InitList(HWND list) {
    ListView_SetExtendedListViewStyle(list, LVS_EX_CHECKBOXES | LVS_EX_FULLROWSELECT | LVS_EX_DOUBLEBUFFER);
    LVCOLUMNW col{};
    col.mask = LVCF_TEXT | LVCF_WIDTH | LVCF_FMT;
    col.fmt = LVCFMT_LEFT;

    col.cx = Scaled(230, GetParent(list));
    col.pszText = const_cast<wchar_t*>(L"Application");
    ListView_InsertColumn(list, 0, &col);

    col.cx = Scaled(110, GetParent(list));
    col.pszText = const_cast<wchar_t*>(L"Process");
    ListView_InsertColumn(list, 1, &col);
}

void AddAppRow(HWND list, const DiscoveredApp& app, bool checked) {
    LVITEMW item{};
    item.mask = LVIF_TEXT | LVIF_PARAM;
    item.iItem = ListView_GetItemCount(list);
    std::wstring name = app.displayName.empty() ? Utf8ToWide(app.processName) : app.displayName;
    item.pszText = const_cast<wchar_t*>(name.c_str());
    item.lParam = (LPARAM)0; // process name is stored via subitem; keep map index in a member instead
    int row = ListView_InsertItem(list, &item);

    std::wstring proc = Utf8ToWide(app.processName);
    LVITEMW sub{};
    sub.mask = LVIF_TEXT;
    sub.iItem = row;
    sub.iSubItem = 1;
    sub.pszText = proc.data();
    ListView_SetItem(list, &sub);

    ListView_SetCheckState(list, row, checked ? TRUE : FALSE);
}

struct RowLookup {
    std::string process;
};

void RebuildList(HWND list, DialogData* data) {
    ListView_DeleteAllItems(list);
    int row = 0;
    for (const auto& app : data->apps) {
        bool enabled = data->settings.enabledApps.count(app.processName) != 0;
        LVITEMW item{};
        item.mask = LVIF_TEXT | LVIF_PARAM;
        item.iItem = row;
        std::wstring name = app.displayName.empty() ? Utf8ToWide(app.processName) : app.displayName;
        item.pszText = const_cast<wchar_t*>(name.c_str());
        RowLookup* lookup = new RowLookup{app.processName};
        item.lParam = (LPARAM)lookup;
        ListView_InsertItem(list, &item);

        std::wstring proc = Utf8ToWide(app.processName);
        LVITEMW sub{};
        sub.mask = LVIF_TEXT;
        sub.iItem = row;
        sub.iSubItem = 1;
        sub.pszText = proc.data();
        ListView_SetItem(list, &sub);

        ListView_SetCheckState(list, row, enabled ? TRUE : FALSE);
        row++;
    }
}

std::set<std::string, std::less<>> CollectCheckedApps(HWND list) {
    std::set<std::string, std::less<>> checked;
    int count = ListView_GetItemCount(list);
    for (int i = 0; i < count; i++) {
        if (ListView_GetCheckState(list, i)) {
            LVITEMW item{};
            item.mask = LVIF_PARAM;
            item.iItem = i;
            if (ListView_GetItem(list, &item)) {
                RowLookup* lookup = reinterpret_cast<RowLookup*>(item.lParam);
                if (lookup) checked.insert(lookup->process);
            }
        }
    }
    return checked;
}

// --- Dialog proc ------------------------------------------------------------

LRESULT CALLBACK SettingsProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    DialogData* data = reinterpret_cast<DialogData*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));

    switch (msg) {
        case WM_CREATE: {
            auto* cs = reinterpret_cast<CREATESTRUCTW*>(lParam);
            data = reinterpret_cast<DialogData*>(cs->lpCreateParams);
            SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(data));
            data->font = MakeUiFont(hwnd);
            SetFontForChildren(hwnd, data->font);

            auto S = [hwnd](int v) { return Scaled(v, hwnd); };

            // Ducking group
            CreateWindowExW(0, L"BUTTON", L"Ducking", WS_CHILD | WS_VISIBLE | BS_GROUPBOX,
                            S(12), S(8), S(424), S(144), hwnd,
                            reinterpret_cast<HMENU>(static_cast<INT_PTR>(10)), nullptr, nullptr);
            MakeStatic(hwnd, 0, L"Duck volume:", S(22), S(27), S(90), S(20));
            MakeCombo(hwnd, IDC_DUCK_COMBO, S(120), S(24), S(100));
            MakeStatic(hwnd, 0, L"%", S(226), S(27), S(14), S(20));
            HWND custom = MakeEdit(hwnd, IDC_DUCK_CUSTOM, L"25", S(246), S(24), S(46), S(21));
            MakeStatic(hwnd, IDC_DUCK_CUSTOM_PCT, L"%", S(298), S(27), S(14), S(20));
            EnableWindow(custom, FALSE);

            MakeStatic(hwnd, 0, L"Fade down (ms):", S(22), S(58), S(96), S(20));
            MakeCombo(hwnd, IDC_FADE_DOWN, S(120), S(55), S(100));
            MakeStatic(hwnd, 0, L"Fade up (ms):", S(22), S(90), S(96), S(20));
            MakeCombo(hwnd, IDC_FADE_UP, S(120), S(87), S(100));

            // Detection group
            CreateWindowExW(0, L"BUTTON", L"Playback detection (browsers)", WS_CHILD | WS_VISIBLE | BS_GROUPBOX,
                            S(12), S(158), S(424), S(96), hwnd,
                            reinterpret_cast<HMENU>(static_cast<INT_PTR>(11)), nullptr, nullptr);
            MakeCheck(hwnd, IDC_BROWSER_CHROME, L"Google Chrome", S(22), S(180), S(130), S(20));
            MakeCheck(hwnd, IDC_BROWSER_EDGE, L"Microsoft Edge", S(160), S(180), S(130), S(20));
            MakeCheck(hwnd, IDC_BROWSER_FIREFOX, L"Mozilla Firefox", S(298), S(180), S(130), S(20));
            MakeCheck(hwnd, IDC_USE_AUDIO, L"Detect playback from browser audio sessions (built-in)",
                      S(22), S(212), S(390), S(20));

            // Background apps group
            CreateWindowExW(0, L"BUTTON", L"Background applications to duck", WS_CHILD | WS_VISIBLE | BS_GROUPBOX,
                            S(12), S(260), S(424), S(150), hwnd,
                            reinterpret_cast<HMENU>(static_cast<INT_PTR>(12)), nullptr, nullptr);
            HWND list = CreateWindowExW(WS_EX_CLIENTEDGE, WC_LISTVIEWW, L"",
                                        WS_CHILD | WS_VISIBLE | LVS_REPORT | LVS_SINGLESEL,
                                        S(22), S(280), S(322), S(92), hwnd,
                                        reinterpret_cast<HMENU>(static_cast<INT_PTR>(IDC_LIST_APPS)),
                                        GetModuleHandleW(nullptr), nullptr);
            InitList(list);
            MakeButton(hwnd, IDC_BTN_DISCOVER, L"Discover", S(350), S(280), S(76), S(24));
            MakeButton(hwnd, IDC_BTN_REMOVE, L"Remove", S(350), S(310), S(76), S(24));
            MakeCheck(hwnd, IDC_DUCK_ALL, L"Duck all other applications (except browsers and system sounds)",
                      S(22), S(384), S(400), S(20));

            // Startup group
            CreateWindowExW(0, L"BUTTON", L"Startup & logging", WS_CHILD | WS_VISIBLE | BS_GROUPBOX,
                            S(12), S(416), S(424), S(76), hwnd,
                            reinterpret_cast<HMENU>(static_cast<INT_PTR>(13)), nullptr, nullptr);
            MakeCheck(hwnd, IDC_STARTUP, L"Start Audio Ducker with Windows", S(22), S(438), S(300), S(20));
            MakeCheck(hwnd, IDC_VERBOSE, L"Verbose logging (debug)", S(22), S(464), S(300), S(20));

            // Extension helper group
            CreateWindowExW(0, L"BUTTON", L"YouTube extension helper (optional)", WS_CHILD | WS_VISIBLE | BS_GROUPBOX,
                            S(12), S(498), S(424), S(80), hwnd,
                            reinterpret_cast<HMENU>(static_cast<INT_PTR>(14)), nullptr, nullptr);
            MakeStatic(hwnd, 0, L"Extension ID (Chrome/Edge):", S(22), S(520), S(150), S(20));
            MakeEdit(hwnd, IDC_EXT_ID, L"", S(176), S(518), S(150), S(21));
            MakeButton(hwnd, IDC_BTN_REGISTER, L"Register helper", S(334), S(516), S(88), S(24));
            MakeStatic(hwnd, 0, L"See browser/README.md for loading the extension.", S(22), S(544), S(390), S(24));

            MakeButton(hwnd, IDC_BTN_APPLY, L"Apply", S(196), S(610), S(76), S(26));
            MakeButton(hwnd, IDC_BTN_OK, L"OK", S(278), S(610), S(76), S(26), BS_DEFPUSHBUTTON);
            MakeButton(hwnd, IDC_BTN_CANCEL, L"Cancel", S(360), S(610), S(76), S(26));

            // Populate controls
            PopulateDuckCombo(GetDlgItem(hwnd, IDC_DUCK_COMBO), data->settings.duckVolume);
            PopulateFadeCombo(GetDlgItem(hwnd, IDC_FADE_DOWN), data->settings.fadeDownMs);
            PopulateFadeCombo(GetDlgItem(hwnd, IDC_FADE_UP), data->settings.fadeUpMs);
            RefreshCustomEdit(hwnd, GetDlgItem(hwnd, IDC_DUCK_COMBO));
            if (!data->settings.extensionId.empty())
                SetDlgItemTextW(hwnd, IDC_EXT_ID, Utf8ToWide(data->settings.extensionId).c_str());

            CheckDlgButton(hwnd, IDC_BROWSER_CHROME, data->settings.browserChrome ? BST_CHECKED : BST_UNCHECKED);
            CheckDlgButton(hwnd, IDC_BROWSER_EDGE, data->settings.browserEdge ? BST_CHECKED : BST_UNCHECKED);
            CheckDlgButton(hwnd, IDC_BROWSER_FIREFOX, data->settings.browserFirefox ? BST_CHECKED : BST_UNCHECKED);
            CheckDlgButton(hwnd, IDC_USE_AUDIO, data->settings.useAudioDetection ? BST_CHECKED : BST_UNCHECKED);
            CheckDlgButton(hwnd, IDC_DUCK_ALL, data->settings.duckAllOthers ? BST_CHECKED : BST_UNCHECKED);
            CheckDlgButton(hwnd, IDC_STARTUP, data->settings.startWithWindows ? BST_CHECKED : BST_UNCHECKED);
            CheckDlgButton(hwnd, IDC_VERBOSE, data->settings.verboseLogging ? BST_CHECKED : BST_UNCHECKED);

            // Discover currently running apps and merge with the enabled list.
            if (data->callbacks.discoverApps) {
                auto running = data->callbacks.discoverApps();
                std::set<std::string> seen;
                for (const auto& a : running) {
                    if (seen.insert(LowerAscii(a.processName)).second) {
                        data->apps.push_back(a);
                        data->displayNames[a.processName] = a.displayName;
                    }
                }
            }
            for (const auto& name : data->settings.enabledApps) {
                bool present = false;
                for (const auto& a : data->apps)
                    if (LowerAscii(a.processName) == name) { present = true; break; }
                if (!present) data->apps.push_back(DiscoveredApp{std::string(name), Utf8ToWide(name)});
            }
            RebuildList(list, data);
            return 0;
        }

        case WM_COMMAND: {
            int id = LOWORD(wParam);
            int code = HIWORD(wParam);
            if (id == IDC_DUCK_COMBO && code == CBN_SELCHANGE)
                RefreshCustomEdit(hwnd, GetDlgItem(hwnd, IDC_DUCK_COMBO));
            else if (id == IDC_BTN_DISCOVER && code == BN_CLICKED) {
                if (data && data->callbacks.discoverApps) {
                    auto running = data->callbacks.discoverApps();
                    std::set<std::string> existing;
                    for (const auto& a : data->apps) existing.insert(LowerAscii(a.processName));
                    for (const auto& a : running) {
                        std::string name = LowerAscii(a.processName);
                        if (!existing.count(name)) {
                            data->apps.push_back(a);
                            data->displayNames[a.processName] = a.displayName;
                        }
                    }
                    RebuildList(GetDlgItem(hwnd, IDC_LIST_APPS), data);
                }
            } else if (id == IDC_BTN_REMOVE && code == BN_CLICKED) {
                HWND list = GetDlgItem(hwnd, IDC_LIST_APPS);
                int sel = ListView_GetNextItem(list, -1, LVNI_SELECTED);
                if (sel >= 0) {
                    LVITEMW item{};
                    item.mask = LVIF_PARAM;
                    item.iItem = sel;
                    if (ListView_GetItem(list, &item)) {
                        delete reinterpret_cast<RowLookup*>(item.lParam);
                    }
                    ListView_DeleteItem(list, sel);
                    // Remove from apps so re-render stays consistent.
                    int index = ListView_GetItemCount(list) > sel ? sel : sel - 1;
                    (void)index;
                }
            } else if (id == IDC_BTN_REGISTER && code == BN_CLICKED) {
                if (data) {
                    wchar_t buf[256]{};
                    GetDlgItemTextW(hwnd, IDC_EXT_ID, buf, 256);
                    HostRegistrationResult res = RegisterNativeMessagingHosts(WideToUtf8(buf));
                    MessageBoxW(hwnd, res.message.c_str(), L"Audio Ducker", MB_OK | MB_ICONINFORMATION);
                }
            } else if (id == IDC_BTN_APPLY && code == BN_CLICKED) {
                if (data) data->callbacks.onApply(CollectSettings(hwnd, data));
            } else if (id == IDC_BTN_OK && code == BN_CLICKED) {
                if (data) data->callbacks.onApply(CollectSettings(hwnd, data));
                DestroyWindow(hwnd);
            } else if (id == IDC_BTN_CANCEL && code == BN_CLICKED) {
                DestroyWindow(hwnd);
            }
            return 0;
        }

        case WM_CLOSE:
            DestroyWindow(hwnd);
            return 0;

        case WM_DESTROY: {
            if (data) {
                HWND list = GetDlgItem(hwnd, IDC_LIST_APPS);
                int count = ListView_GetItemCount(list);
                for (int i = 0; i < count; i++) {
                    LVITEMW item{};
                    item.mask = LVIF_PARAM;
                    item.iItem = i;
                    if (ListView_GetItem(list, &item))
                        delete reinterpret_cast<RowLookup*>(item.lParam);
                }
                if (data->font) DeleteObject(data->font);
                delete data;
                SetWindowLongPtrW(hwnd, GWLP_USERDATA, 0);
            }
            PostMessageW(GetParent(hwnd), WM_APP + 5, 0, 0); // tell owner we closed
            return 0;
        }

        default:
            break;
    }
    return DefWindowProcW(hwnd, msg, wParam, lParam);
}

AppSettings CollectSettings(HWND hwnd, DialogData* data) {
    AppSettings s = data->settings;

    HWND duckCombo = GetDlgItem(hwnd, IDC_DUCK_COMBO);
    int sel = (int)SendMessageW(duckCombo, CB_GETCURSEL, 0, 0);
    if (sel >= 0 && sel < (int)(sizeof(kDuckPresets) / sizeof(kDuckPresets[0]))) {
        s.duckVolume = kDuckPresets[sel] / 100.0f;
    } else {
        wchar_t buf[16]{};
        GetDlgItemTextW(hwnd, IDC_DUCK_CUSTOM, buf, 16);
        int pct = _wtoi(buf);
        if (pct < 1) pct = 25;
        if (pct > 95) pct = 95;
        s.duckVolume = pct / 100.0f;
    }
    s.fadeDownMs = ComboValue(GetDlgItem(hwnd, IDC_FADE_DOWN));
    s.fadeUpMs = ComboValue(GetDlgItem(hwnd, IDC_FADE_UP));

    s.browserChrome = IsDlgButtonChecked(hwnd, IDC_BROWSER_CHROME) == BST_CHECKED;
    s.browserEdge = IsDlgButtonChecked(hwnd, IDC_BROWSER_EDGE) == BST_CHECKED;
    s.browserFirefox = IsDlgButtonChecked(hwnd, IDC_BROWSER_FIREFOX) == BST_CHECKED;
    s.useAudioDetection = IsDlgButtonChecked(hwnd, IDC_USE_AUDIO) == BST_CHECKED;
    s.duckAllOthers = IsDlgButtonChecked(hwnd, IDC_DUCK_ALL) == BST_CHECKED;
    s.startWithWindows = IsDlgButtonChecked(hwnd, IDC_STARTUP) == BST_CHECKED;
    s.verboseLogging = IsDlgButtonChecked(hwnd, IDC_VERBOSE) == BST_CHECKED;

    wchar_t ext[256]{};
    GetDlgItemTextW(hwnd, IDC_EXT_ID, ext, 256);
    s.extensionId = Trim(WideToUtf8(ext));

    s.enabledApps = CollectCheckedApps(GetDlgItem(hwnd, IDC_LIST_APPS));
    return s;
}

} // namespace

void ShowSettingsWindow(HWND owner, const AppSettings& initial, SettingsCallbacks cb) {
    auto* data = new DialogData;
    data->settings = initial;
    data->callbacks = std::move(cb);

    HINSTANCE hInst = GetModuleHandleW(nullptr);
    WNDCLASSW wc{};
    wc.lpfnWndProc = SettingsProc;
    wc.hInstance = hInst;
    wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    wc.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_WINDOW + 1);
    wc.lpszClassName = L"AudioDuckerSettingsWnd";
    RegisterClassW(&wc);

    int dpi = owner ? GetDpiForWindow(owner) : 96;
    RECT rc{0, 0, 448 * dpi / 96, 648 * dpi / 96};
    AdjustWindowRectEx(&rc, WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU, FALSE, 0);

    HWND dlg = CreateWindowExW(0, wc.lpszClassName, L"Audio Ducker Settings",
                               WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU,
                               CW_USEDEFAULT, CW_USEDEFAULT, rc.right - rc.left, rc.bottom - rc.top,
                               owner, nullptr, hInst, data);
    if (!dlg) {
        delete data;
        return;
    }

    if (owner) {
        RECT orc{}, crc{};
        GetWindowRect(owner, &orc);
        GetWindowRect(dlg, &crc);
        int w = crc.right - crc.left;
        int h = crc.bottom - crc.top;
        int x = orc.left + (orc.right - orc.left - w) / 2;
        int y = orc.top + (orc.bottom - orc.top - h) / 2;
        SetWindowPos(dlg, nullptr, x, y, 0, 0, SWP_NOSIZE | SWP_NOZORDER);
    }
    ShowWindow(dlg, SW_SHOW);

    // Modal loop.
    MSG msg;
    while (IsWindow(dlg) && GetMessageW(&msg, nullptr, 0, 0) > 0) {
        if (!IsDialogMessageW(dlg, &msg)) {
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
        }
    }
}

} // namespace ducker
