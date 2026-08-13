#include "TestHarness.h"

#include "core/DuckingStateMachine.h"

#include <map>
#include <string>
#include <vector>

using ducker::DuckingStateMachine;

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
