#include "TestHarness.h"

#include "core/VolumeFader.h"

using ducker::VolumeFader;

TEST(FaderInterpolatesOverDuration) {
    VolumeFader f;
    f.Start("a", 0.0f, 1.0f, 0, 1000);

    auto [v0, done0] = f.Step("a", 0);
    EXPECT_NEAR(v0, 0.0f, 1e-6);
    EXPECT_FALSE(done0);

    // Smoothstep is 0.5 at the midpoint, so the volume is exactly half way.
    auto [v500, done500] = f.Step("a", 500);
    EXPECT_NEAR(v500, 0.5f, 1e-6);
    EXPECT_FALSE(done500);

    auto [v1000, done1000] = f.Step("a", 1000);
    EXPECT_NEAR(v1000, 1.0f, 1e-6);
    EXPECT_TRUE(done1000);

    // Completed fades are removed.
    EXPECT_FALSE(f.IsActive("a"));
}

TEST(FaderFadeDown) {
    VolumeFader f;
    f.Start("a", 1.0f, 0.2f, 0, 400);
    auto [v, done] = f.Step("a", 200);
    EXPECT_NEAR(v, 0.6f, 1e-6);
    EXPECT_FALSE(done);
    auto [vEnd, doneEnd] = f.Step("a", 400);
    EXPECT_NEAR(vEnd, 0.2f, 1e-6);
    EXPECT_TRUE(doneEnd);
}

TEST(FaderZeroDurationIsInstant) {
    VolumeFader f;
    f.Start("a", 0.4f, 0.7f, 100, 0);
    auto [v, done] = f.Step("a", 100);
    EXPECT_NEAR(v, 0.7f, 1e-6);
    EXPECT_TRUE(done);
    EXPECT_FALSE(f.IsActive("a"));
}

TEST(FaderClampsNegativeTime) {
    VolumeFader f;
    f.Start("a", 0.0f, 1.0f, 1000, 1000);
    auto [v, done] = f.Step("a", 500); // before start
    EXPECT_NEAR(v, 0.0f, 1e-6);
    EXPECT_FALSE(done);
}

TEST(FaderRestartReplacesFade) {
    VolumeFader f;
    f.Start("a", 0.0f, 1.0f, 0, 1000);
    f.Step("a", 100);
    // A new fade for the same id replaces the old one, restarting from now.
    f.Start("a", 0.5f, 1.0f, 100, 100);
    auto [v, done] = f.Step("a", 150); // t = 0.5 of the new fade
    EXPECT_NEAR(v, 0.75f, 1e-6);
    EXPECT_FALSE(done);
}

TEST(FaderCancelRemoves) {
    VolumeFader f;
    f.Start("a", 0.0f, 1.0f, 0, 1000);
    EXPECT_TRUE(f.IsActive("a"));
    f.Cancel("a");
    EXPECT_FALSE(f.IsActive("a"));
    auto [v, done] = f.Step("a", 0);
    EXPECT_EQ(v, 0.0f);
    EXPECT_TRUE(done);
}

TEST(FaderUnknownIdReportsDone) {
    VolumeFader f;
    auto [v, done] = f.Step("nope", 0);
    EXPECT_EQ(v, 0.0f);
    EXPECT_TRUE(done);
}

TEST(FaderCancelAllClears) {
    VolumeFader f;
    f.Start("a", 0.0f, 1.0f, 0, 100);
    f.Start("b", 1.0f, 0.0f, 0, 100);
    EXPECT_EQ(f.ActiveCount(), 2);
    f.CancelAll();
    EXPECT_EQ(f.ActiveCount(), 0);
}

TEST(FaderIsMonotonic) {
    VolumeFader f;
    f.Start("a", 0.0f, 1.0f, 0, 1000);
    float prev = -1.0f;
    for (int t = 0; t <= 1000; t += 50) {
        auto [v, _] = f.Step("a", t);
        EXPECT_TRUE(v >= prev);
        prev = v;
    }
}

TEST(FaderNegativeDurationTreatedAsInstant) {
    VolumeFader f;
    f.Start("a", 0.3f, 0.9f, 0, -50);
    auto [v, done] = f.Step("a", 0);
    EXPECT_NEAR(v, 0.9f, 1e-6);
    EXPECT_TRUE(done);
}
