#pragma once

#include <windows.h>
#include <audiopolicy.h>
#include <mmdeviceapi.h>

#include <atomic>
#include <deque>
#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <utility>
#include <vector>

#include "audio/AudioSession.h"
#include "audio/VolumeController.h"
#include "common/ComPtr.h"

namespace ducker {

struct AudioCallbacks {
    std::function<void(const SessionInfo&, AudioSession*)> onSessionAdded;
    std::function<void(const std::string& id)> onSessionRemoved;
    std::function<void(const std::string& id, float volume, bool muted)> onVolumeChanged;
    std::function<void(const std::string& id, bool active)> onStateChanged;
    std::function<void()> onDeviceChanged;
    std::function<void(int64_t nowMs)> onTick;
};

// Owns the audio worker thread. All COM objects (device enumerator, session
// manager, session controls) live on that thread inside an STA apartment, so
// volume writes and event delivery never race. Other threads communicate with
// it through the task queue / blocking queries.
class AudioSessionManager : private IMMNotificationClient, private IAudioSessionNotification {
public:
    AudioSessionManager();
    ~AudioSessionManager();

    bool Start();
    void Stop();

    void SetCallbacks(AudioCallbacks cb) { callbacks_ = std::move(cb); }
    void Post(std::function<void()> task);
    bool IsRunning() const { return running_.load(); }

    VolumeController& Controller() { return controller_; }

    // Blocking query, safe to call from the UI thread.
    std::vector<std::pair<std::wstring, std::string>> QueryActiveAppNames();

    AudioSession* FindSession(const std::string& id);

private:
    // ---- audio thread implementation ------------------------------------
    void ThreadMain();
    bool AcquireDevice();                    // (re)create enumerator + endpoint
    void EnumerateSessions();                // diff current sessions against enumeration
    void AddSessionFromControl(IAudioSessionControl2* control);
    void RemoveSession(const std::string& id, bool notify);
    void NotifyDeviceChanged();
    void PumpMessages();
    void ScheduleHousekeeping();

    // ---- IMMNotificationClient -------------------------------------------
    HRESULT STDMETHODCALLTYPE OnDeviceStateChanged(LPCWSTR, DWORD) override;
    HRESULT STDMETHODCALLTYPE OnDeviceAdded(LPCWSTR) override;
    HRESULT STDMETHODCALLTYPE OnDeviceRemoved(LPCWSTR) override;
    HRESULT STDMETHODCALLTYPE OnDefaultDeviceChanged(EDataFlow, ERole, LPCWSTR) override;
    HRESULT STDMETHODCALLTYPE OnPropertyValueChanged(LPCWSTR, const PROPERTYKEY) override;
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID, void**) override;
    ULONG STDMETHODCALLTYPE AddRef() override;
    ULONG STDMETHODCALLTYPE Release() override;

    // ---- IAudioSessionNotification -----------------------------------------
    HRESULT STDMETHODCALLTYPE OnSessionCreated(IAudioSessionControl*) override;

    std::map<std::string, std::shared_ptr<AudioSession>> sessions_;
    VolumeController controller_;
    AudioCallbacks callbacks_;

    std::thread thread_;
    std::mutex queueMu_;
    std::deque<std::function<void()>> queue_;
    HANDLE queueEvent_ = nullptr;
    std::atomic<bool> stop_{false};
    std::atomic<bool> running_{false};

    com::Ptr<IMMDeviceEnumerator> enumerator_;
    com::Ptr<IMMDevice> device_;
    com::Ptr<IAudioSessionManager2> sessionManager_;
    std::atomic<LONG> refCount_{1};
    int64_t lastHousekeepingMs_ = 0;
    int64_t lastVolumePollMs_ = 0;
};

} // namespace ducker
