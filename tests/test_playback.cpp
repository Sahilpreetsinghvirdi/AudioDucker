#include "TestHarness.h"

#include "core/PlaybackDetector.h"

using ducker::PlaybackDetector;

TEST(DetectorStartsInactive) {
    PlaybackDetector d;
    EXPECT_FALSE(d.IsActive());
    EXPECT_EQ(d.TotalCount(), 0);
}

TEST(DetectorActivatesOnFirstSource) {
    PlaybackDetector d;
    bool active = false;
    int total = -1;
    d.SetCallback([&](bool a, int t) {
        active = a;
        total = t;
    });

    d.SetSourceCount("browser-audio", 1);
    EXPECT_TRUE(active);
    EXPECT_EQ(total, 1);
    EXPECT_TRUE(d.IsActive());
    EXPECT_EQ(d.TotalCount(), 1);
}

TEST(DetectorSumsMultipleSources) {
    PlaybackDetector d;
    d.SetSourceCount("browser-audio", 2);
    d.SetSourceCount("yt:123", 3);
    EXPECT_TRUE(d.IsActive());
    EXPECT_EQ(d.TotalCount(), 5);
}

TEST(DetectorCountZeroRemovesSource) {
    PlaybackDetector d;
    d.SetSourceCount("browser-audio", 2);
    d.SetSourceCount("yt:123", 1);
    EXPECT_EQ(d.TotalCount(), 3);
    d.SetSourceCount("yt:123", 0);
    EXPECT_EQ(d.TotalCount(), 2);
    d.SetSourceCount("browser-audio", -1); // negative is treated as off
    EXPECT_EQ(d.TotalCount(), 0);
    EXPECT_FALSE(d.IsActive());
}

TEST(DetectorRemoveSource) {
    PlaybackDetector d;
    d.SetSourceCount("a", 2);
    d.SetSourceCount("b", 1);
    d.RemoveSource("a");
    EXPECT_EQ(d.TotalCount(), 1);
    EXPECT_TRUE(d.IsActive());
    d.RemoveSource("b");
    EXPECT_EQ(d.TotalCount(), 0);
    EXPECT_FALSE(d.IsActive());
}

TEST(DetectorClear) {
    PlaybackDetector d;
    d.SetSourceCount("a", 4);
    EXPECT_TRUE(d.IsActive());
    d.Clear();
    EXPECT_FALSE(d.IsActive());
    EXPECT_EQ(d.TotalCount(), 0);
    EXPECT_TRUE(d.Snapshot().empty());
}

TEST(DetectorSnapshotContainsCounts) {
    PlaybackDetector d;
    d.SetSourceCount("browser-audio", 1);
    d.SetSourceCount("yt:9", 2);
    auto snap = d.Snapshot();
    EXPECT_EQ(snap["browser-audio"], 1);
    EXPECT_EQ(snap["yt:9"], 2);
    EXPECT_EQ(snap.size(), static_cast<size_t>(2));
}

TEST(DetectorCallbackReportsLatestArgs) {
    PlaybackDetector d;
    int calls = 0;
    d.SetCallback([&](bool, int) { calls++; });
    d.SetSourceCount("a", 1);
    d.SetSourceCount("b", 1);
    d.RemoveSource("a");
    EXPECT_EQ(calls, 3);
}
