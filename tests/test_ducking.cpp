#include "TestHarness.h"

#include "audio/AudioSession.h"
#include "audio/VolumeController.h"
#include "common/Helpers.h"
#include "config/ConfigManager.h"
#include "core/DuckingStateMachine.h"
#include "ducking/DuckingManager.h"
#include "utils/Logger.h"

#include <map>
#include <memory>
#include <string>
#include <vector>

using ducker::AppSettings;
using ducker::AudioSession;
using ducker::DuckingManager;
using ducker::DuckingStateMachine;
using ducker::SessionInfo;
using ducker::VolumeController;

namespace {

// Records every event the state machine emits so tests can assert ordering.
struct RecordingListener : DuckingStateMachine::Listener {
    void OnDuckStarted(int sourceCount) override { ducks.push_back(sourceCount); }
    void OnDuckStopped() override { restores++; }
    void OnTargetAdded(const DuckingStateMachine::TargetInfo& info) override {
        added.push_back(info.id);
        addedVolumes[info.id] = info.original;
        duckTargets[info.id] = info.target;
    }
    void OnTargetUpdated(const DuckingStateMachine::TargetInfo& info) override {
        updated.push_back(info.id);
        updatedVolumes[info.id] = info.original;
    }
    void OnTargetRestore(const DuckingStateMachine::TargetInfo& info) override {
        restored.push_back(info.id);
        restoredTo[info.id] = info.original;
    }
    void OnTargetRemoved(const std::string& id) override { removed.push_back(id); }

    std::vector<int> ducks;
    int restores = 0;
    std::vector<std::string> added, updated, restored, removed;
    std::map<std::string, float> addedVolumes;
    std::map<std::string, float> duckTargets;
    std::map<std::string, float> updatedVolumes;
    std::map<std::string, float> restoredTo;
};

DuckingStateMachine MakeMachine(RecordingListener& l) {
    DuckingStateMachine m;
    m.SetListener(&l);
    return m;
}

} // namespace

TEST(DuckMachineNoDetectionNoDuck) {
    RecordingListener l;
    auto m = MakeMachine(l);
    m.AddSession("spotify", 0.8f, false);
    EXPECT_FALSE(m.IsDucking());
    EXPECT_EQ(m.TargetCount(), static_cast<size_t>(0));
    EXPECT_TRUE(l.ducks.empty());
}

TEST(DuckMachineBasicDuckAndRestore) {
    RecordingListener l;
    auto m = MakeMachine(l);
    m.AddSession("spotify", 0.8f, false);

    m.SetSourceCount("browser-audio", 1);
    EXPECT_TRUE(m.IsDucking());
    EXPECT_EQ(l.ducks.size(), static_cast<size_t>(1));
    EXPECT_EQ(l.ducks[0], 1);
    EXPECT_EQ(l.added.size(), static_cast<size_t>(1));
    EXPECT_EQ(l.added[0], "spotify");
    EXPECT_NEAR(l.duckTargets["spotify"], 0.25f, 1e-6); // default duck volume
    EXPECT_NEAR(l.addedVolumes["spotify"], 0.8f, 1e-6);
    EXPECT_TRUE(m.HasTarget("spotify"));
    EXPECT_FALSE(m.TargetFaded("spotify"));

    // Fade finishes.
    m.OnDuckFadeComplete("spotify", 0.25f);
    EXPECT_TRUE(m.TargetFaded("spotify"));
    EXPECT_TRUE(m.IsSessionTracked("spotify"));

    // Playback stops -> restore to the original volume.
    m.SetSourceCount("browser-audio", 0);
    EXPECT_FALSE(m.IsDucking());
    EXPECT_EQ(l.restores, 1);
    EXPECT_EQ(l.restored.size(), static_cast<size_t>(1));
    EXPECT_NEAR(l.restoredTo["spotify"], 0.8f, 1e-6);

    // Restore fade completes -> target cleared, session kept.
    m.OnRestoreFadeComplete("spotify", 0.8f);
    EXPECT_FALSE(m.HasTarget("spotify"));
    EXPECT_TRUE(m.IsSessionTracked("spotify"));
    EXPECT_TRUE(l.removed.empty());
}

TEST(DuckMachineManualRestoreWhileSourceActive) {
    RecordingListener l;
    auto m = MakeMachine(l);
    m.AddSession("spotify", 0.8f, false);
    m.SetSourceCount("browser-audio", 1);
    m.OnDuckFadeComplete("spotify", 0.25f);
    EXPECT_TRUE(m.IsDucking());

    // Tray "Restore volumes" must restore even though a source is still active.
    m.SetForcedDuck(false);
    EXPECT_FALSE(m.IsDucking());
    EXPECT_EQ(l.restores, 1);
    EXPECT_EQ(l.restored.size(), static_cast<size_t>(1));
    EXPECT_NEAR(l.restoredTo["spotify"], 0.8f, 1e-6);

    // New playback event resumes normal ducking.
    m.OnRestoreFadeComplete("spotify", 0.8f);
    m.SetSourceCount("browser-audio", 0);
    m.SetSourceCount("browser-audio", 1);
    EXPECT_TRUE(m.IsDucking());
    EXPECT_EQ(l.ducks.size(), static_cast<size_t>(2));
    EXPECT_EQ(m.TargetCount(), static_cast<size_t>(1));
}

TEST(DuckMachineManualChangeBecomesBaseline) {
    RecordingListener l;
    auto m = MakeMachine(l);
    m.AddSession("spotify", 0.8f, false);
    m.SetSourceCount("browser-audio", 1);
    m.OnDuckFadeComplete("spotify", 0.25f);

    // User drags the volume to 50% while ducked.
    m.UpdateSessionVolume("spotify", 0.5f);
    EXPECT_EQ(l.updated.size(), static_cast<size_t>(1));
    EXPECT_NEAR(l.updatedVolumes["spotify"], 0.5f, 1e-6);

    // On restore we must honour the manual value, not the old 80%.
    m.SetSourceCount("browser-audio", 0);
    EXPECT_NEAR(l.restoredTo["spotify"], 0.5f, 1e-6);
}

TEST(DuckMachineNoSpuriousUpdateWhenVolumeUnchanged) {
    RecordingListener l;
    auto m = MakeMachine(l);
    m.AddSession("spotify", 0.8f, false);
    m.SetSourceCount("browser-audio", 1);
    m.OnDuckFadeComplete("spotify", 0.25f);
    m.UpdateSessionVolume("spotify", 0.25f); // still at the duck level
    EXPECT_TRUE(l.updated.empty());
}

TEST(DuckMachineUpdateWhileFadingIsIgnored) {
    RecordingListener l;
    auto m = MakeMachine(l);
    m.AddSession("spotify", 0.8f, false);
    m.SetSourceCount("browser-audio", 1);
    // Fade not complete yet: even a big change must not be treated as manual.
    m.UpdateSessionVolume("spotify", 0.9f);
    EXPECT_TRUE(l.updated.empty());
    EXPECT_NEAR(m.TargetVolume("spotify"), 0.25f, 1e-6);
}

TEST(DuckMachineNeverRaisesQuietApps) {
    RecordingListener l;
    auto m = MakeMachine(l);
    m.AddSession("vlc", 0.15f, false); // already below 25%
    m.SetSourceCount("browser-audio", 1);
    EXPECT_NEAR(l.duckTargets["vlc"], 0.15f, 1e-6);
}

TEST(DuckMachineSessionAddedDuringDuck) {
    RecordingListener l;
    auto m = MakeMachine(l);
    m.SetSourceCount("browser-audio", 1);
    EXPECT_TRUE(m.IsDucking());

    m.AddSession("mpv", 0.6f, false);
    EXPECT_TRUE(m.HasTarget("mpv"));
    EXPECT_NEAR(l.duckTargets["mpv"], 0.25f, 1e-6);
}

TEST(DuckMachineMutedSessionUnmutedDuringDuck) {
    RecordingListener l;
    auto m = MakeMachine(l);
    m.AddSession("spotify", 0.6f, true); // muted when duck starts
    m.SetSourceCount("browser-audio", 1);
    EXPECT_TRUE(m.HasTarget("spotify"));

    // Unmuting while ducking keeps the target in place.
    m.UpdateSessionMuted("spotify", false);
    EXPECT_TRUE(m.HasTarget("spotify"));
    EXPECT_TRUE(m.IsDucking());
}

TEST(DuckMachineRemoveWhileDucked) {
    RecordingListener l;
    auto m = MakeMachine(l);
    m.AddSession("spotify", 0.8f, false);
    m.SetSourceCount("browser-audio", 1);

    m.RemoveSession("spotify");
    EXPECT_FALSE(m.HasTarget("spotify"));
    EXPECT_FALSE(m.IsSessionTracked("spotify"));
    EXPECT_EQ(l.removed.size(), static_cast<size_t>(1));
    EXPECT_EQ(l.removed[0], "spotify");

    // Playback stops: nothing left to restore.
    m.SetSourceCount("browser-audio", 0);
    EXPECT_TRUE(l.restored.empty());
    EXPECT_EQ(l.restores, 1);
}

TEST(DuckMachineForcedDuck) {
    RecordingListener l;
    auto m = MakeMachine(l);
    m.AddSession("spotify", 0.8f, false);

    m.SetForcedDuck(true);
    EXPECT_TRUE(m.IsDucking());
    EXPECT_EQ(l.ducks.size(), static_cast<size_t>(1));
    EXPECT_TRUE(m.HasTarget("spotify"));

    m.SetForcedDuck(false);
    EXPECT_FALSE(m.IsDucking());
    EXPECT_EQ(l.restores, 1);
    EXPECT_NEAR(l.restoredTo["spotify"], 0.8f, 1e-6);
}

TEST(DuckMachineMultipleSources) {
    RecordingListener l;
    auto m = MakeMachine(l);
    m.SetSourceCount("browser-audio", 1);
    EXPECT_EQ(m.ActiveSourceCount(), 1);
    m.SetSourceCount("yt:42", 2);
    EXPECT_EQ(m.ActiveSourceCount(), 3);
    EXPECT_TRUE(m.IsDucking());

    // One source stopping keeps us ducking.
    m.SetSourceCount("yt:42", 0);
    EXPECT_TRUE(m.IsDucking());
    EXPECT_EQ(m.ActiveSourceCount(), 1);

    m.SetSourceCount("browser-audio", 0);
    EXPECT_FALSE(m.IsDucking());
}

TEST(DuckMachineDuckStartedOncePerTransition) {
    RecordingListener l;
    auto m = MakeMachine(l);
    m.SetSourceCount("browser-audio", 1);
    m.SetSourceCount("yt:1", 1);
    m.SetSourceCount("yt:1", 2); // more tabs, still ducking
    EXPECT_EQ(l.ducks.size(), static_cast<size_t>(1));
    // The browser tab stopping doesn't end the duck: yt:1 is still active.
    m.SetSourceCount("browser-audio", 0);
    EXPECT_TRUE(m.IsDucking());
    EXPECT_EQ(l.restores, 0);
    // Once the last source stops, exactly one restore is emitted.
    m.SetSourceCount("yt:1", 0);
    EXPECT_FALSE(m.IsDucking());
    EXPECT_EQ(l.restores, 1);
}

TEST(DuckMachineResetAll) {
    RecordingListener l;
    auto m = MakeMachine(l);
    m.AddSession("spotify", 0.8f, false);
    m.SetSourceCount("browser-audio", 1);
    EXPECT_TRUE(m.IsDucking());
    EXPECT_TRUE(m.HasTarget("spotify"));

    m.ResetAll();
    EXPECT_FALSE(m.IsDucking());
    EXPECT_EQ(m.TargetCount(), static_cast<size_t>(0));
    EXPECT_FALSE(m.IsSessionTracked("spotify"));
    EXPECT_EQ(m.ActiveSourceCount(), 0);
    EXPECT_TRUE(m.SourceCounts().empty());
}

TEST(DuckMachineRestoreKeepsSession) {
    RecordingListener l;
    auto m = MakeMachine(l);
    m.AddSession("spotify", 0.8f, false);
    m.SetSourceCount("browser-audio", 1);
    m.SetSourceCount("browser-audio", 0);

    // Session must still be tracked so a future duck uses its current volume.
    // (It stays in the target set until the restore fade finishes, which the
    // test does not advance.)
    EXPECT_TRUE(m.IsSessionTracked("spotify"));
}

TEST(DuckMachineDuckVolumeIsClamped) {
    RecordingListener l;
    auto m = MakeMachine(l);
    m.SetDuckVolume(1.5f);  // clamped to 1.0
    m.SetDuckVolume(-0.5f); // clamped to 0.0
    m.AddSession("a", 0.8f, false);
    m.SetSourceCount("browser-audio", 1);
    EXPECT_NEAR(m.TargetVolume("a"), 0.0f, 1e-6);
}

// ---------------------------------------------------------------------------
// DuckingManager integration: the browser detector's playback count must be
// pushed into the state machine as it changes (regression: SetCallback was
// never registered, so ducking never started in real usage).
// ---------------------------------------------------------------------------

namespace {

struct TestContext {
    TestContext() { dm.Configure(settings); }

    AppSettings settings;
    VolumeController controller;
    DuckingManager dm{ducker::Logger::Instance(), controller};
};

std::unique_ptr<AudioSession> MakeDummySession() { return std::make_unique<AudioSession>(); }

SessionInfo MakeBrowserInfo(const char* id, bool active) {
    SessionInfo info;
    info.id = id;
    info.processId = 4242;
    info.processName = "chrome.exe";
    info.volume = 0.8f;
    info.active = active;
    return info;
}

} // namespace

TEST(DuckingManagerBrowserPlaybackStartsAndStopsDucking) {
    TestContext ctx;
    auto session = MakeDummySession();

    auto info = MakeBrowserInfo("browser-1", false);
    ctx.dm.OnSessionAdded(info, session.get());
    EXPECT_FALSE(ctx.dm.IsDucking());

    ctx.dm.OnStateChanged(info.id, true); // YouTube starts playing
    EXPECT_TRUE(ctx.dm.IsDucking());

    ctx.dm.OnStateChanged(info.id, false); // playback pauses/stops
    EXPECT_FALSE(ctx.dm.IsDucking());

    ctx.dm.OnSessionRemoved(info.id);
}

TEST(DuckingManagerBrowserSessionWithoutAudioDoesNotDuck) {
    TestContext ctx;
    auto session = MakeDummySession();

    // Session exists and reports Active, but produces no audio (peak ~0) - like
    // a paused tab Chrome still holds Active. Must NOT duck (regression: used to
    // duck purely off the stale "active" flag for seconds after pausing).
    auto info = MakeBrowserInfo("browser-1", true);
    ctx.dm.OnSessionAdded(info, session.get());
    EXPECT_FALSE(ctx.dm.IsDucking());

    ctx.dm.OnSessionRemoved(info.id);
}

TEST(DuckingManagerBrowserSilenceGraceTriggersRestore) {
    TestContext ctx;
    auto session = MakeDummySession();

    auto info = MakeBrowserInfo("browser-1", false);
    ctx.dm.OnSessionAdded(info, session.get());
    EXPECT_FALSE(ctx.dm.IsDucking());

    ctx.dm.OnStateChanged(info.id, true); // playback starts
    EXPECT_TRUE(ctx.dm.IsDucking());

    // The session stays "active" but goes silent (dummy peak is always 0), the
    // way a paused tab does. Once quiet has persisted past the grace period the
    // watchdog must stop ducking even though the session never reported
    // Inactive.
    auto t0 = ducker::NowMs();
    ctx.dm.OnTick(t0);          // silence window opens
    ctx.dm.OnTick(t0 + 1300);   // 1.3s quiet > grace
    EXPECT_FALSE(ctx.dm.IsDucking());

    ctx.dm.OnSessionRemoved(info.id);
}

TEST(DuckingManagerBrowserSilenceGraceDoesNotFlickerOnBriefGaps) {
    TestContext ctx;
    auto session = MakeDummySession();

    auto info = MakeBrowserInfo("browser-1", false);
    ctx.dm.OnSessionAdded(info, session.get());
    ctx.dm.OnStateChanged(info.id, true);
    EXPECT_TRUE(ctx.dm.IsDucking());

    // Short quiet blip below the grace period must not stop ducking...
    auto t0 = ducker::NowMs();
    ctx.dm.OnTick(t0);         // goes quiet
    ctx.dm.OnTick(t0 + 300);   // still quiet but < grace
    ctx.dm.OnStateChanged(info.id, true); // ...audio resumes (event)
    EXPECT_TRUE(ctx.dm.IsDucking());

    // ...and the resumed session, still silent on the next tick, keeps ducking
    // until the grace window has actually elapsed since it went quiet again.
    ctx.dm.OnTick(t0 + 600);
    EXPECT_TRUE(ctx.dm.IsDucking());

    ctx.dm.OnSessionRemoved(info.id);
}

TEST(DuckingManagerDisabledIgnoresBrowserPlayback) {
    TestContext ctx;
    ctx.dm.SetEnabled(false); // runs inline: no audio thread attached

    auto session = MakeDummySession();
    auto info = MakeBrowserInfo("browser-1", false);
    ctx.dm.OnSessionAdded(info, session.get());
    ctx.dm.OnStateChanged(info.id, true);
    EXPECT_FALSE(ctx.dm.IsDucking());

    ctx.dm.OnSessionRemoved(info.id);
}

TEST(DuckingManagerUseAudioDetectionOffIgnoresBrowserPlayback) {
    TestContext ctx;
    ctx.settings.useAudioDetection = false;
    ctx.dm.Configure(ctx.settings);

    auto session = MakeDummySession();
    auto info = MakeBrowserInfo("browser-1", true);
    ctx.dm.OnSessionAdded(info, session.get());
    EXPECT_FALSE(ctx.dm.IsDucking());

    ctx.dm.OnSessionRemoved(info.id);
}

TEST(DuckingManagerToggleDetectionOffStopsDucking) {
    TestContext ctx;
    auto session = MakeDummySession();

    auto info = MakeBrowserInfo("browser-1", false);
    ctx.dm.OnSessionAdded(info, session.get());
    ctx.dm.OnStateChanged(info.id, true);
    EXPECT_TRUE(ctx.dm.IsDucking());

    ctx.settings.useAudioDetection = false;
    ctx.dm.Configure(ctx.settings);
    EXPECT_FALSE(ctx.dm.IsDucking());

    ctx.settings.useAudioDetection = true;
    ctx.dm.Configure(ctx.settings);
    ctx.dm.OnStateChanged(info.id, true); // no-op: detector was reset
    EXPECT_FALSE(ctx.dm.IsDucking());

    ctx.dm.OnSessionRemoved(info.id);
}
