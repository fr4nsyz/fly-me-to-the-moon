#pragma once

#include <cmath>

namespace moon {

struct Vec3 {
    double x = 0.0;
    double y = 0.0;
    double z = 0.0;
};

inline Vec3 operator+(Vec3 a, Vec3 b) { return {a.x + b.x, a.y + b.y, a.z + b.z}; }
inline Vec3 operator-(Vec3 a, Vec3 b) { return {a.x - b.x, a.y - b.y, a.z - b.z}; }
inline Vec3 operator*(double s, Vec3 v) { return {s * v.x, s * v.y, s * v.z}; }
inline Vec3 operator*(Vec3 v, double s) { return s * v; }
inline Vec3 operator/(Vec3 v, double s) { return {v.x / s, v.y / s, v.z / s}; }

inline double dot(Vec3 a, Vec3 b) { return a.x * b.x + a.y * b.y + a.z * b.z; }

inline Vec3 cross(Vec3 a, Vec3 b) {
    return {a.y * b.z - a.z * b.y,
            a.z * b.x - a.x * b.z,
            a.x * b.y - a.y * b.x};
}

inline double norm(Vec3 v) { return std::sqrt(dot(v, v)); }

} // namespace moon
