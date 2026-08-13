#include "TestHarness.h"

#include "core/VolumeSnapshot.h"

using ducker::vol::Clamp01;
using ducker::vol::ComputeDuckTarget;
using ducker::vol::ComputeRestoreVolume;
using ducker::vol::UserIntervened;

TEST(Clamp01BoundsVolumes) {
    EXPECT_EQ(Clamp01(-0.5f), 0.0f);
    EXPECT_EQ(Clamp01(-0.0f), 0.0f);
    EXPECT_EQ(Clamp01(0.25f), 0.25f);
    EXPECT_EQ(Clamp01(1.0f), 1.0f);
    EXPECT_EQ(Clamp01(1.5f), 1.0f);
}

TEST(DuckTargetNeverRaises) {
    // Above the duck level: duck to the configured level.
    EXPECT_NEAR(ComputeDuckTarget(0.8f, 0.25f), 0.25f, 1e-6);
    // Already quieter than the duck level: never raise it.
    EXPECT_NEAR(ComputeDuckTarget(0.15f, 0.25f), 0.15f, 1e-6);
    EXPECT_NEAR(ComputeDuckTarget(0.0f, 0.25f), 0.0f, 1e-6);
    EXPECT_NEAR(ComputeDuckTarget(0.25f, 0.25f), 0.25f, 1e-6);
    EXPECT_NEAR(ComputeDuckTarget(0.9f, 0.05f), 0.05f, 1e-6);
}

TEST(UserIntervenedDetection) {
    EXPECT_FALSE(UserIntervened(0.25f, 0.25f));
    EXPECT_FALSE(UserIntervened(0.26f, 0.25f));        // within 2% tolerance
    EXPECT_FALSE(UserIntervened(0.25f, 0.26f));
    EXPECT_TRUE(UserIntervened(0.5f, 0.25f));
    EXPECT_TRUE(UserIntervened(0.0f, 0.25f));
    // Exact threshold: just over epsilon counts as an intervention.
    EXPECT_TRUE(UserIntervened(0.25f + ducker::vol::kEps * 1.01f, 0.25f));
}

TEST(RestoreHonoursOriginalVolume) {
    // Untouched volume: restore exactly what it was before the duck.
    EXPECT_NEAR(ComputeRestoreVolume(0.8f, 0.25f, 0.25f), 0.8f, 1e-6);
    EXPECT_NEAR(ComputeRestoreVolume(0.8f, 0.25f, 0.26f), 0.8f, 1e-6);
    // User moved it mid-duck: restore to their value, never to the old one.
    EXPECT_NEAR(ComputeRestoreVolume(0.8f, 0.25f, 0.5f), 0.5f, 1e-6);
    EXPECT_NEAR(ComputeRestoreVolume(0.8f, 0.25f, 0.1f), 0.1f, 1e-6);
    EXPECT_NEAR(ComputeRestoreVolume(0.8f, 0.25f, 0.0f), 0.0f, 1e-6);
    // Custom epsilon.
    EXPECT_NEAR(ComputeRestoreVolume(0.8f, 0.25f, 0.3f, 0.1f), 0.8f, 1e-6);
    EXPECT_NEAR(ComputeRestoreVolume(0.8f, 0.25f, 0.4f, 0.1f), 0.4f, 1e-6);
}
