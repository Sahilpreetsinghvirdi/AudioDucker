#pragma once

#include <windows.h>
#include <audiopolicy.h>
#include <mmdeviceapi.h>

#include <atomic>
#include <functional>
#include <mutex>
#include <string>

#include "common/ComPtr.h"

namespace ducker {

class AudioSession;

struct SessionInfo {
    std::string id;           // session instance identifier
    DWORD processId = 0;
    std::string processName;  // lowercase exe file name
    std::wstring displayName;
    float volume = 1.0f;
    bool muted = false;
    bool active = false;      // IAudioSessionControl2 state == Active
    bool system = false;      // system sounds session (never ducked)
};

// IAudioSessionEvents sink used to receive callbacks for one session.
class SessionEventSink : public IAudioSessionEvents {
public:
    explicit SessionEventSink(AudioSession* owner) : owner_(owner) {}

    STDMETHODIMP QueryInterface(REFIID iid, void** ppv) override;
    STDMETHODIMP_(ULONG) AddRef() override;
    STDMETHODIMP_(ULONG) Release() override;

    STDMETHODIMP OnDisplayNameChanged(LPCWSTR, LPCGUID) override { return S_OK; }
    STDMETHODIMP OnIconPathChanged(LPCWSTR, LPCGUID) override { return S_OK; }
    STDMETHODIMP OnSimpleVolumeChanged(float volume, BOOL muted, LPCGUID) override;
    STDMETHODIMP OnChannelVolumeChanged(DWORD, float*, DWORD, LPCGUID) override { return S_OK; }
    STDMETHODIMP OnGroupingParamChanged(LPCGUID, LPCGUID) override { return S_OK; }
    STDMETHODIMP OnStateChanged(AudioSessionState state) override;
    STDMETHODIMP OnSessionDisconnected(AudioSessionDisconnectReason) override;

private:
    AudioSession* owner_ = nullptr;
    std::atomic<ULONG> ref_{1};
};

// Wraps a single Windows audio session: identity, volume read/write and
// event delivery. All methods must be called from the audio worker thread.
class AudioSession {
public:
    using VolumeChanged = std::function<void(float volume, bool muted)>;
    using StateChanged = std::function<void(bool active)>;
    using Disconnected = std::function<void()>;

    AudioSession() = default;
    ~AudioSession();

    bool Init(IAudioSessionControl2* control);
    void Shutdown();

    const std::string& Id() const { return id_; }
    const SessionInfo& Info() const { return info_; }
    DWORD ProcessId() const { return info_.processId; }

    bool GetVolume(float& volume, bool& muted) const;
    void SetVolume(float volume); // self-writes are recorded for suppression
    void SetMute(bool mute);
    bool IsActive() const;        // polls session state

    void SetVolumeChangedCallback(VolumeChanged cb) { volumeCb_ = std::move(cb); }
    void SetStateChangedCallback(StateChanged cb) { stateCb_ = std::move(cb); }
    void SetDisconnectedCallback(Disconnected cb) { disconnectCb_ = std::move(cb); }

    // Invoked by SessionEventSink on the audio thread.
    void OnSimpleVolumeChanged(float volume, bool muted);
    void OnStateChanged(AudioSessionState state);
    void OnDisconnected();

private:
    friend class SessionEventSink;
    com::Ptr<IAudioSessionControl2> control_;
    com::Ptr<ISimpleAudioVolume> volume_;
    com::Ptr<SessionEventSink> sink_;
    mutable SessionInfo info_;
    std::string id_;
    mutable std::mutex mu_;
    bool expectedSet_ = false;   // a SetVolume/SetMute wrote a value we await
    float expectedVolume_ = 0.0f;
    bool expectedMuted_ = false;
    VolumeChanged volumeCb_;
    StateChanged stateCb_;
    Disconnected disconnectCb_;
};

} // namespace ducker
