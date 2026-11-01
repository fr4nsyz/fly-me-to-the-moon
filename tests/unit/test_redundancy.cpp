#include "redundancy.hpp"
#include <gtest/gtest.h>
#include <limits>

using namespace moon;

namespace {

const EngineCommand kGood{0.42, Vec3{0.0, 0.0, 1.0}};
const EngineCommand kBad{9.13, Vec3{0.0, 0.0, 1.0}};
const int kNoRecord[kComputers] = {0, 0, 0};

void fill(EngineCommand (&cores)[kCores], const EngineCommand& c) {
    for (EngineCommand& core : cores) core = c;
}

} // namespace

TEST(Redundancy, AllCoresAgreeUsesComputerZeroWithoutCorrection) {
    EngineCommand cores[kCores];
    fill(cores, kGood);
    const Selection s = select_command(cores, kNoRecord);
    EXPECT_EQ(s.computer, 0);
    EXPECT_FALSE(s.corrected);
    EXPECT_FALSE(s.detected);
}

TEST(Redundancy, OneBadCoreSilencesItsComputer) {
    EngineCommand cores[kCores];
    fill(cores, kGood);
    cores[3] = kBad;
    const Selection s = select_command(cores, kNoRecord);
    EXPECT_TRUE(same_command(cores[2 * s.computer], kGood));
    EXPECT_FALSE(s.trusted[1]);
    EXPECT_TRUE(s.trusted[0]);
    EXPECT_TRUE(s.trusted[2]);
    EXPECT_TRUE(s.corrected);
    EXPECT_FALSE(s.detected);
}

TEST(Redundancy, SelfConsistentBadComputerIsOutvoted) {
    EngineCommand cores[kCores];
    fill(cores, kGood);
    cores[0] = kBad;
    cores[1] = kBad;
    const Selection s = select_command(cores, kNoRecord);
    EXPECT_TRUE(same_command(cores[2 * s.computer], kGood));
    EXPECT_FALSE(s.trusted[0]);
    EXPECT_TRUE(s.corrected);
}

TEST(Redundancy, TwoHealthyComputersThatDisagreeUseTheLongerRecord) {
    EngineCommand cores[kCores];
    fill(cores, kGood);
    cores[0] = kBad;
    cores[2] = kBad;
    cores[3] = kBad;
    const int record[kComputers] = {0, 3, 7};
    const Selection s = select_command(cores, record);
    EXPECT_EQ(s.computer, 2);
    EXPECT_TRUE(s.corrected);
}

TEST(Redundancy, NoComputerPassingLockstepIsDetected) {
    EngineCommand cores[kCores];
    fill(cores, kGood);
    cores[1] = kBad;
    cores[3] = kBad;
    cores[5] = kBad;
    const Selection s = select_command(cores, kNoRecord);
    EXPECT_TRUE(s.detected);
}

TEST(Redundancy, IdenticalNanCommandsCompareEqualByBits) {
    const double nan = std::numeric_limits<double>::quiet_NaN();
    EXPECT_TRUE(same_command(EngineCommand{nan, Vec3{}}, EngineCommand{nan, Vec3{}}));
}
