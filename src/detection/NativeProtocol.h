#pragma once

#include <windows.h>

namespace ducker::native {

// The AudioDuckerHost.exe helper process talks to the main application through
// a WM_COPYDATA message delivered to the hidden main window.
inline constexpr const wchar_t* kWindowClass = L"AudioDuckerMainWindow";
inline constexpr const wchar_t* kHostName = L"com.audiodycker.youtube";
inline constexpr DWORD kMagic = 0xAD4D53C4; // "ADMS"

struct StateMessage {
    DWORD magic;
    DWORD version;
    int count; // number of playing YouTube tabs reported by the extension
};

} // namespace ducker::native
