#include "audio/AudioSession.h"

#include <objbase.h>

#include "common/Helpers.h"

namespace ducker {

namespace {
constexpr float kVolumeEps = 1e-4f;
} // namespace

// ---------------------------------------------------------------------------
// SessionEventSink (IAudioSessionEvents)
// ---------------------------------------------------------------------------

HRESULT SessionEventSink::QueryInterface(REFIID iid, void** ppv) {
    if (!ppv) return E_POINTER;
    if (iid == __uuidof(IAudioSessionEvents) || iid == IID_IUnknown) {
        *ppv = static_cast<IAudioSessionEvents*>(this);
        AddRef();
        return S_OK;
    }
    *ppv = nullptr;
    return E_NOINTERFACE;
}

ULONG SessionEventSink::AddRef() { return ++ref_; }
ULONG SessionEventSink::Release() {
    ULONG r = --ref_;
    if (r == 0) delete this;
    return r;
}

HRESULT SessionEventSink::OnSimpleVolumeChanged(float volume, BOOL muted, LPCGUID) {
    if (owner_) owner_->OnSimpleVolumeChanged(volume, muted != FALSE);
    return S_OK;
}

HRESULT SessionEventSink::OnStateChanged(AudioSessionState state) {
    if (owner_) owner_->OnStateChanged(state);
    return S_OK;
}

HRESULT SessionEventSink::OnSessionDisconnected(AudioSessionDisconnectReason) {
    if (owner_) owner_->OnDisconnected();
    return S_OK;
}

// ---------------------------------------------------------------------------
// AudioSession
// ---------------------------------------------------------------------------

AudioSession::~AudioSession() { Shutdown(); }

bool AudioSession::Init(IAudioSessionControl2* control) {
    if (!control) return false;
    control_ = control; // manager holds a live reference; safe to alias

    HRESULT hr = control->QueryInterface(__uuidof(ISimpleAudioVolume),
                                         reinterpret_cast<void**>(volume_.Put()));
    if (FAILED(hr) || !volume_) return false;

    LPWSTR instId = nullptr;
    if (SUCCEEDED(control->GetSessionInstanceIdentifier(&instId)) && instId) {
        id_ = WideToUtf8(instId);
        CoTaskMemFree(instId);
    }
    if (id_.empty()) id_ = Str("sess-", reinterpret_cast<uintptr_t>(control));
    info_.id = id_;

    DWORD pid = 0;
    if (SUCCEEDED(control->GetProcessId(&pid))) info_.processId = pid;
    if (auto name = ProcessNameByPid(info_.processId)) info_.processName = *name;

    info_.system = control->IsSystemSoundsSession() == S_OK;

    LPWSTR nameBuf = nullptr;
    if (SUCCEEDED(control->GetDisplayName(&nameBuf)) && nameBuf) {
        info_.displayName = nameBuf;
        CoTaskMemFree(nameBuf);
    }

    info_.muted = false;
    GetVolume(info_.volume, info_.muted);
    info_.active = IsActive();

    sink_ = new SessionEventSink(this);
    control->RegisterAudioSessionNotification(sink_.Get());
    return true;
}

void AudioSession::Shutdown() {
    if (control_ && sink_) control_->UnregisterAudioSessionNotification(sink_.Get());
    sink_.Reset();
    volume_.Reset();
    control_.Reset();
}

bool AudioSession::GetVolume(float& volume, bool& muted) const {
    if (!volume_) return false;
    std::lock_guard<std::mutex> lock(mu_);
    float v = 0.0f;
    BOOL m = FALSE;
    HRESULT hr = volume_->GetMasterVolume(&v);
    if (FAILED(hr)) return false;
    volume_->GetMute(&m);
    info_.volume = v;
    info_.muted = m != FALSE;
    volume = v;
    muted = info_.muted;
    return true;
}

void AudioSession::SetVolume(float volume) {
    if (!volume_) return;
    if (volume < 0.0f) volume = 0.0f;
    if (volume > 1.0f) volume = 1.0f;
    std::lock_guard<std::mutex> lock(mu_);
    expectedSet_ = true;
    expectedVolume_ = volume;
    expectedMuted_ = info_.muted;
    info_.volume = volume;
    volume_->SetMasterVolume(volume, &GUID_NULL);
}

void AudioSession::SetMute(bool mute) {
    if (!volume_) return;
    std::lock_guard<std::mutex> lock(mu_);
    expectedSet_ = true;
    expectedMuted_ = mute;
    expectedVolume_ = info_.volume;
    info_.muted = mute;
    volume_->SetMute(mute ? TRUE : FALSE, &GUID_NULL);
}

bool AudioSession::IsActive() const {
    if (!control_) return false;
    std::lock_guard<std::mutex> lock(mu_);
    AudioSessionState state = AudioSessionStateInactive;
    control_->GetState(&state);
    return state == AudioSessionStateActive;
}

void AudioSession::OnSimpleVolumeChanged(float volume, bool muted) {
    std::lock_guard<std::mutex> lock(mu_);
    info_.volume = volume;
    info_.muted = muted;

    // Suppress the echo of our own SetVolume/SetMute calls.
    if (expectedSet_) {
        bool matches = std::fabs(volume - expectedVolume_) < kVolumeEps && muted == expectedMuted_;
        if (matches) {
            expectedSet_ = false;
            return;
        }
        // Not our own write: fall through and report it.
    }

    if (volumeCb_) volumeCb_(volume, muted);
}

void AudioSession::OnStateChanged(AudioSessionState state) {
    bool active = state == AudioSessionStateActive;
    {
        std::lock_guard<std::mutex> lock(mu_);
        info_.active = active;
    }
    if (stateCb_) stateCb_(active);
}

void AudioSession::OnDisconnected() {
    if (disconnectCb_) disconnectCb_();
}

} // namespace ducker
