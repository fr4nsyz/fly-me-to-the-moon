#pragma once

namespace moon {

// GM values: IERS Conventions 2010 (Earth) and JPL DE430 (Moon).
inline constexpr double kGmEarth = 3.986004418e14;        // m^3/s^2
inline constexpr double kGmMoon = 4.902800066e12;         // m^3/s^2
inline constexpr double kEarthRadius = 6378137.0;         // m, equatorial
inline constexpr double kMoonRadius = 1737400.0;          // m, mean
inline constexpr double kEarthRotationRate = 7.292115e-5; // rad/s
inline constexpr double kMoonOrbitRadius = 384400000.0;   // m, mean distance
inline constexpr double kObliquity = 0.409092804;         // rad, 23.439 deg, Earth's tilt to the ecliptic
inline constexpr double kMoonInclinationToEcliptic = 0.0897972; // rad, 5.145 deg
inline constexpr double kLaunchLatitude = 0.49869;        // rad, 28.573 deg, Kennedy LC-39A
inline constexpr double kG0 = 9.80665;                    // m/s^2, standard gravity for Isp

// Bart Howard's original "Fly Me to the Moon" sheet music was published in 1954.
inline constexpr double kMissionEpochJd = 2434743.5;      // Julian date of 1954-01-01 00:00 UTC

} // namespace moon
