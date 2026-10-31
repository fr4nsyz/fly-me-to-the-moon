#include "vec3.hpp"
#include <gtest/gtest.h>

using moon::Vec3;

TEST(Vec3, CrossFollowsRightHandRule) {
    Vec3 z = moon::cross(Vec3{1, 0, 0}, Vec3{0, 1, 0});
    EXPECT_EQ(z.x, 0.0);
    EXPECT_EQ(z.y, 0.0);
    EXPECT_EQ(z.z, 1.0);
}

TEST(Vec3, NormOfThreeFourFiveTriangle) {
    EXPECT_EQ(moon::norm(Vec3{3, 4, 0}), 5.0);
}
