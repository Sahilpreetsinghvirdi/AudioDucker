#include "audio/AudioSessionManager.h"

#include <audiopolicy.h>
#include <functiondiscoverykeys_devpkey.h>
#include <mmsystem.h>
#include <objbase.h>

#include <algorithm>
#include <chrono>
#include <future>
#include <set>

#include "common/Helpers.h"
#include "utils/Logger.h"

namespace ducker {

namespace {
constexpr int kFadeTickMs = 10;      // timer resolution while fades are active
constexpr int kIdleTickMs = 250;     // sleep while idle
constexpr int64_t kHousekeepingMs = 250;
constexpr int64_t kVolumePollMs = 1000;
} // namespace

AudioSessionManager::AudioSessionManager() {
    queueEvent_ = CreateEventW(nullptr, FALSE, FALSE, nullptr);
}

AudioSessionManager::~AudioSessionManager() {
    Stop();
    if (queueEvent_) CloseHandle(queueEvent_);
}

bool AudioSessionManager::Start() {
    if (thread_.joinable()) return true;
    stop_ = false;
    thread_ = std::thread(&AudioSessionManager::ThreadMain, this);
    return true;
}

void AudioSessionManager::Stop() {
    if (!thread_.joinable()) return;
    {
        std::lock_guard<std::mutex> lock(queueMu_);
        queue_.push_back([this] { stop_ = true; });
    }
    SetEvent(queueEvent_);
    thread_.join();
}

void AudioSessionManager::Post(std::function<void()> task) {
    {
        std::lock_guard<std::mutex> lock(queueMu_);
        queue_.push_back(std::move(task));
    }
    SetEvent(queueEvent_);
}

std::vector<std::pair<std::wstring, std::string>> AudioSessionManager::QueryActiveAppNames() {
    std::promise<std::vector<std::pair<std::wstring, std::string>>> promise;
    auto future = promise.get_future();
    Post([this, &promise] {
        std::map<std::string, std::pair<std::wstring, std::string>> unique;
        for (const auto& [id, s] : sessions_) {
            if (s->Info().system || s->Info().processId == 0) continue;
            unique[s->Info().processName] = {s->Info().displayName, s->Info().processName};
        }
        std::vector<std::pair<std::wstring, std::string>> out;
        for (const auto& [name, pair] : unique) out.push_back(pair);
        promise.set_value(std::move(out));
    });
    if (future.wait_for(std::chrono::seconds(5)) != std::future_status::ready)
        return {};
    return future.get();
}

AudioSession* AudioSessionManager::FindSession(const std::string& id) {
    auto it = sessions_.find(id);
    return it == sessions_.end() ? nullptr : it->second.get();
}

// ---------------------------------------------------------------------------
// audio thread
// ---------------------------------------------------------------------------

void AudioSessionManager::ThreadMain() {
    CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    timeBeginPeriod(1);
    running_ = true;

    AcquireDevice();
    EnumerateSessions(); // list existing sessions now; later ones arrive via events

    int64_t lastWake = NowMs();
    for (;;) {
        int waitMs = controller_.ActiveCount() > 0 ? kFadeTickMs : kIdleTickMs;
        DWORD r = MsgWaitForMultipleObjects(1, &queueEvent_, FALSE, waitMs, QS_ALLINPUT);

        if (r == WAIT_OBJECT_0) {
            std::deque<std::function<void()>> tasks;
            {
                std::lock_guard<std::mutex> lock(queueMu_);
                tasks.swap(queue_);
            }
            for (auto& task : tasks) task();
            if (stop_) break;
        }

        PumpMessages();
        controller_.Tick(NowMs());

        int64_t now = NowMs();
        if (now - lastWake >= kHousekeepingMs) {
            lastWake = now;
            ScheduleHousekeeping();
        }

        if (stop_) break;
    }

    // Drop everything cleanly.
    for (auto& [id, s] : sessions_) s->Shutdown();
    sessions_.clear();
    controller_.CancelAll();
    if (sessionManager_) sessionManager_->UnregisterSessionNotification(this);
    if (enumerator_) enumerator_->UnregisterEndpointNotificationCallback(this);

    running_ = false;
    timeEndPeriod(1);
    CoUninitialize();
}

bool AudioSessionManager::AcquireDevice() {
    if (enumerator_) enumerator_->UnregisterEndpointNotificationCallback(this);
    enumerator_.Reset();
    device_.Reset();
    sessionManager_.Reset();

    HRESULT hr = CoCreateInstance(__uuidof(MMDeviceEnumerator), nullptr, CLSCTX_ALL,
                                  __uuidof(IMMDeviceEnumerator), reinterpret_cast<void**>(enumerator_.Put()));
    if (FAILED(hr) || !enumerator_) {
        Logger::Instance().Warn("Could not create MMDeviceEnumerator (", hr, ")");
        return false;
    }
    enumerator_->RegisterEndpointNotificationCallback(this);

    hr = enumerator_->GetDefaultAudioEndpoint(eRender, eConsole, device_.Put());
    if (FAILED(hr) || !device_) {
        Logger::Instance().Debug("No default render endpoint yet");
        return false;
    }
    hr = device_->Activate(__uuidof(IAudioSessionManager2), CLSCTX_ALL, nullptr,
                           reinterpret_cast<void**>(sessionManager_.Put()));
    if (FAILED(hr) || !sessionManager_) {
        Logger::Instance().Warn("Could not activate IAudioSessionManager2 (", hr, ")");
        return false;
    }
    sessionManager_->RegisterSessionNotification(this);
    return true;
}

void AudioSessionManager::EnumerateSessions() {
    if (!sessionManager_) {
        Logger::Instance().Debug("EnumerateSessions: no session manager yet");
        return;
    }

    com::Ptr<IAudioSessionEnumerator> enumerator;
    HRESULT hr = sessionManager_->GetSessionEnumerator(enumerator.Put());
    if (FAILED(hr) || !enumerator) {
        Logger::Instance().Debug("GetSessionEnumerator failed (", hr, ")");
        return;
    }

    int count = 0;
    enumerator->GetCount(&count);
    Logger::Instance().Debug("Enumerating audio sessions: ", count);

    std::set<std::string> seen;
    for (int i = 0; i < count; i++) {
        com::Ptr<IAudioSessionControl> control;
        if (FAILED(enumerator->GetSession(i, control.Put())) || !control) continue;

        com::Ptr<IAudioSessionControl2> control2;
        if (FAILED(control->QueryInterface(__uuidof(IAudioSessionControl2),
                                           reinterpret_cast<void**>(control2.Put()))))
            continue;

        LPWSTR instId = nullptr;
        if (FAILED(control2->GetSessionInstanceIdentifier(&instId)) || !instId) continue;
        std::string id = WideToUtf8(instId);
        CoTaskMemFree(instId);
        seen.insert(id);

        if (sessions_.count(id) == 0) AddSessionFromControl(control2.Get());
    }

    // Prune sessions that no longer exist.
    std::vector<std::string> toRemove;
    for (const auto& [id, _] : sessions_)
        if (!seen.count(id)) toRemove.push_back(id);
    for (const auto& id : toRemove) RemoveSession(id, true);
}

void AudioSessionManager::AddSessionFromControl(IAudioSessionControl2* control) {
    auto session = std::make_shared<AudioSession>();
    if (!session->Init(control)) {
        Logger::Instance().Debug("Failed to init audio session");
        return;
    }
    const std::string id = session->Id();
    if (sessions_.count(id)) return;

    session->SetVolumeChangedCallback([this, id](float volume, bool muted) {
        if (controller_.IsFading(id)) return; // echo of our own fade
        if (callbacks_.onVolumeChanged) callbacks_.onVolumeChanged(id, volume, muted);
    });
    session->SetStateChangedCallback([this, id](bool active) {
        if (callbacks_.onStateChanged) callbacks_.onStateChanged(id, active);
    });
    session->SetDisconnectedCallback([this, id] {
        Post([this, id] { RemoveSession(id, true); });
    });

    sessions_[id] = session;
    Logger::Instance().Debug("Session added: ", session->Info().processName,
                             " (pid ", session->ProcessId(), ") active=", session->Info().active,
                             " vol=", Percent(session->Info().volume));

    if (callbacks_.onSessionAdded) callbacks_.onSessionAdded(session->Info(), session.get());
}

void AudioSessionManager::RemoveSession(const std::string& id, bool notify) {
    auto it = sessions_.find(id);
    if (it == sessions_.end()) return;
    it->second->Shutdown();
    controller_.Cancel(id);
    sessions_.erase(it);
    Logger::Instance().Debug("Session removed: ", id);
    if (notify && callbacks_.onSessionRemoved) callbacks_.onSessionRemoved(id);
}

void AudioSessionManager::PumpMessages() {
    MSG msg;
    while (PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE)) {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }
}

void AudioSessionManager::ScheduleHousekeeping() {
    if (!device_ || !sessionManager_) {
        if (AcquireDevice()) EnumerateSessions();
    }

    // Volume poll: catch changes Windows or the app makes without firing events.
    int64_t now = NowMs();
    if (now - lastVolumePollMs_ >= kVolumePollMs) {
        lastVolumePollMs_ = now;
        for (const auto& [id, s] : sessions_) {
            float v = 0.0f;
            bool m = false;
            if (s->GetVolume(v, m)) {
                if (controller_.IsFading(id)) continue; // our own writes
                if (callbacks_.onVolumeChanged) callbacks_.onVolumeChanged(id, v, m);
            }
        }
    }

    if (callbacks_.onTick) callbacks_.onTick(now);
}

// ---------------------------------------------------------------------------
// IMMNotificationClient
// ---------------------------------------------------------------------------

void AudioSessionManager::NotifyDeviceChanged() {
    Logger::Instance().Info("Audio device changed - resetting");
    controller_.CancelAll();
    for (auto& [id, s] : sessions_) s->Shutdown();
    sessions_.clear();
    if (callbacks_.onDeviceChanged) callbacks_.onDeviceChanged();
    device_.Reset();
    sessionManager_.Reset();
    if (AcquireDevice()) EnumerateSessions();
}

HRESULT AudioSessionManager::OnDeviceStateChanged(LPCWSTR id, DWORD) {
    if (device_) {
        LPWSTR devIdStr = nullptr;
        if (SUCCEEDED(device_->GetId(&devIdStr)) && devIdStr) {
            bool matches = wcscmp(id, devIdStr) == 0;
            CoTaskMemFree(devIdStr);
            if (matches) Post([this] { NotifyDeviceChanged(); });
        }
    }
    return S_OK;
}

HRESULT AudioSessionManager::OnDeviceAdded(LPCWSTR) { return S_OK; }

HRESULT AudioSessionManager::OnDeviceRemoved(LPCWSTR) { return S_OK; }

HRESULT AudioSessionManager::OnDefaultDeviceChanged(EDataFlow flow, ERole role, LPCWSTR) {
    if (flow == eRender && role == eConsole) Post([this] { NotifyDeviceChanged(); });
    return S_OK;
}

HRESULT AudioSessionManager::OnPropertyValueChanged(LPCWSTR, const PROPERTYKEY) { return S_OK; }

HRESULT AudioSessionManager::QueryInterface(REFIID iid, void** ppv) {
    if (!ppv) return E_POINTER;
    if (iid == __uuidof(IMMNotificationClient))
        *ppv = static_cast<IMMNotificationClient*>(this);
    else if (iid == __uuidof(IAudioSessionNotification))
        *ppv = static_cast<IAudioSessionNotification*>(this);
    else if (iid == IID_IUnknown)
        *ppv = static_cast<IMMNotificationClient*>(this);
    else {
        *ppv = nullptr;
        return E_NOINTERFACE;
    }
    AddRef();
    return S_OK;
}

ULONG AudioSessionManager::AddRef() { return ++refCount_; }

ULONG AudioSessionManager::Release() {
    LONG r = --refCount_;
    if (r == 0) delete this;
    return r;
}

// ---------------------------------------------------------------------------
// IAudioSessionNotification
// ---------------------------------------------------------------------------

HRESULT AudioSessionManager::OnSessionCreated(IAudioSessionControl* control) {
    if (!control) return S_OK;
    com::Ptr<IAudioSessionControl2> control2;
    if (FAILED(control->QueryInterface(__uuidof(IAudioSessionControl2),
                                       reinterpret_cast<void**>(control2.Put()))))
        return S_OK;
    Post([this, c = control2.Detach()] {
        com::Ptr<IAudioSessionControl2> holder(c);
        LPWSTR instId = nullptr;
        if (FAILED(holder->GetSessionInstanceIdentifier(&instId)) || !instId) return;
        std::string id = WideToUtf8(instId);
        CoTaskMemFree(instId);
        if (sessions_.count(id)) return;
        AddSessionFromControl(holder.Get());
    });
    return S_OK;
}

} // namespace ducker
